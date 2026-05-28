#include "combat.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"      // GetObjectName, ResolveServerObjectHandle
#include "engine_manager.h"   // kAddrGuiManagerPtr, kMgrPanels*Offset
#include "engine_offsets.h"
#include "engine_panels.h"    // PanelKind, IdentifyPanel
#include "engine_player.h"    // GetPlayerServerCreature
#include "engine_reads.h"
#include "log.h"
#include "menus_extract.h"    // FromControl — listbox row text reader
#include "same_name_suffix.h" // AppendSuffix for same-LocName disambiguator
#include "strings.h"
#include "prism.h"
#include "transitions.h"      // IsModuleLoadPending — gate during cutscene-load
                              // transient (engine LYT loader use-after-free)

namespace acc::combat {

namespace {

// ============================================================================
// Phase 1A — combat-mode poll with stability debounce.
// Mirrors the pattern in turn_announce.cpp: a pending state is tracked
// per tick; only after kQuietMs of unchanged-pending do we speak.
// ============================================================================

// CClientExoApp::GetCombatMode — __thiscall, takes no args, returns int
// (0 = peace, !=0 = combat). The CClientExoApp facade is 8 bytes
// (vtable + internal); the flag itself lives on the internal struct,
// but the accessor on the facade walks it for us.
typedef int (__thiscall* PFN_GetCombatMode)(void* this_);

bool ReadCombatMode(int& outMode) {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return false;
        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return false;
        auto fn = reinterpret_cast<PFN_GetCombatMode>(kAddrGetCombatMode);
        outMode = fn(exoApp);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

constexpr DWORD kCombatModeQuietMs = 200;  // collapse oscillation near edges

}  // namespace

bool IsCombatActive() {
    int mode = 0;
    if (!ReadCombatMode(mode)) return false;
    return mode != 0;
}

void TickCombatMode() {
    // Module-load latch — see transitions.h IsModuleLoadPending. Combat
    // mode is read via a CClientExoApp engine accessor; probing it
    // through a stunt-module cutscene load tripped the engine's LYT
    // loader on the previous module's freed resref arena (dump
    // swkotor.exe(1).23224.dmp, 2026-05-26).
    if (acc::transitions::IsModuleLoadPending()) return;

    int mode = 0;
    if (!ReadCombatMode(mode)) return;

    bool inCombatNow = (mode != 0);

    static int   s_lastSpoken = -1;          // -1 = first tick, suppress
    static int   s_pending    = -1;
    static DWORD s_changedAt  = 0;

    DWORD now = GetTickCount();

    if (s_lastSpoken < 0) {
        s_lastSpoken = inCombatNow ? 1 : 0;
        s_pending    = s_lastSpoken;
        s_changedAt  = now;
        acclog::Write("Combat.Mode", "first-tick suppress; mode=%d", mode);
        return;
    }

    int desired = inCombatNow ? 1 : 0;
    if (desired != s_pending) {
        s_pending   = desired;
        s_changedAt = now;
    }
    if (s_pending == s_lastSpoken) return;
    if (now - s_changedAt < kCombatModeQuietMs) return;

    auto id = inCombatNow ? acc::strings::Id::CombatBegins
                          : acc::strings::Id::CombatEnds;
    const char* phrase = acc::strings::Get(id);
    prism::Speak(phrase, /*interrupt=*/false);
    acclog::Write("Combat.Mode", "%s -> [%s] (debounced %ums)",
                  s_lastSpoken ? "leaving" : "entering",
                  phrase, static_cast<unsigned>(now - s_changedAt));
    s_lastSpoken = s_pending;
}

// ============================================================================
// Phase 1B — combat-log poll on CSWGuiInGameMessages.messages_listbox.
// ============================================================================

namespace {

// CGuiInGame.in_game_messages slot @+0x1c — the engine's persistent
// CSWGuiInGameMessages instance. Allocated once when CGuiInGame is
// constructed (start of game session) and lives the whole time, so
// AddMessages writes to it during live combat even though the review
// screen isn't mounted. The earlier panels[]-walk only found this panel
// when the user had the Messages screen open, missing every live
// combat-log row in the process.
constexpr size_t kCGuiInGameInGameMessagesOffset = 0x1c;

// Resolve the persistent combat-log panel via the CGuiInGame singleton.
// nullptr until CGuiInGame is constructed (DLL attach / title screen).
void* FindInGameMessagesPanel() {
    void* gui = acc::engine::ResolveGuiInGame();
    if (!gui) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(gui) +
            kCGuiInGameInGameMessagesOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Read a CSWGuiListBox's row count without dereferencing each row pointer.
int ReadListBoxRowCount(void* lb) {
    if (!lb) return 0;
    __try {
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
        if (!lbList || !lbList->data) return 0;
        return lbList->size;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Read row[i] as a control pointer. nullptr on miss / fault.
void* ReadListBoxRow(void* lb, int i) {
    if (!lb || i < 0) return nullptr;
    __try {
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
        if (!lbList || !lbList->data) return nullptr;
        if (i >= lbList->size) return nullptr;
        return lbList->data[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

void TickCombatLog() {
    // Module-load latch (cutscene-load transient). FindInGameMessagesPanel
    // walks CSWGuiManager.panels[] which the engine is in the middle of
    // tearing down during a module transition.
    if (acc::transitions::IsModuleLoadPending()) return;

    void* panel = FindInGameMessagesPanel();
    if (!panel) {
        // Panel not mounted — keep a fresh baseline so the next open
        // doesn't replay history.
        return;
    }

    auto* base = reinterpret_cast<unsigned char*>(panel);
    void* lb = base + kInGameMessagesMessagesListBoxOffset;
    int   rows = ReadListBoxRowCount(lb);

    static void* s_lastLb     = nullptr;
    static int   s_lastRows   = 0;

    if (lb != s_lastLb) {
        // First sight of this listbox — adopt the current row count as
        // baseline; do NOT replay history.
        s_lastLb   = lb;
        s_lastRows = rows;
        acclog::Write("Combat.Log", "armed lb=%p rows=%d", lb, rows);
        return;
    }
    if (rows == s_lastRows) return;

    if (rows < s_lastRows) {
        // Listbox was reset (engine clear / reopen). Re-adopt baseline,
        // skip the spurious "delta" interpretation.
        acclog::Write("Combat.Log", "reset lb=%p rows %d -> %d", lb,
                      s_lastRows, rows);
        s_lastRows = rows;
        return;
    }

    // Delta path: log each newly-appended row. Speech is intentionally
    // OFF here — patch-20260521-093926.log proved that this listbox is
    // filled lazily when the review screen opens (all rows arrived in
    // one burst at panel-open time), not during live combat. Speaking
    // the burst would re-narrate the entire fight at review time. The
    // OnAddMessages hook is the live-narration source; this poll stays
    // as a sanity check on what messages_listbox ends up containing.
    int firstNew = s_lastRows;
    int delta    = rows - firstNew;
    acclog::Write("Combat.Log", "delta lb=%p +%d (rows %d -> %d)",
                  lb, delta, firstNew, rows);
    for (int i = firstNew; i < rows; ++i) {
        void* row = ReadListBoxRow(lb, i);
        if (!row) {
            acclog::Write("Combat.Log", "row %d null", i);
            continue;
        }
        char text[512];
        if (!acc::menus::extract::FromControl(row, text, sizeof(text))) {
            acclog::Write("Combat.Log", "row %d extract failed", i);
            continue;
        }
        if (text[0] == '\0') {
            acclog::Write("Combat.Log", "row %d empty", i);
            continue;
        }
        acclog::Write("Combat.Log", "row %d -> [%.300s]", i, text);
    }
    s_lastRows = rows;
}

}  // namespace acc::combat

// ============================================================================
// Combat-log message-buffer rules.
//
// Registered into acc::msg::Router on first hook fire. Subscribes to
// AppendToMsgBuffer events to:
//
//   * Merge the 3-or-4-line attack sequence
//     (Summary → Angriffsstatistik → [Bedrohungsstatistik] →
//      Abwehrstatistik → [Schadensstatistik]) into a single compact line.
//   * Speak Bedrohungsstatistik as-is mid-block (so the threat-roll cue
//     stays audible without disrupting the buffer).
//
// Everything else (heals, item-use, kill/XP, loot, world feedback) falls
// through to the router's raw-speech fallback. Sequence established
// empirically from patch-20260521-100345.log. The block opens on a
// summary line, closes on Schadensstatistik (hit) or on any
// unrecognized line (miss).

#include "msg_router.h"
#include "combat_strings.h"
#include "party_cache.h"

namespace acc::combat {
namespace {

struct AttackBlock {
    bool has_summary;
    bool has_angriff;
    bool has_abwehr;
    bool has_schaden;
    bool hit;
    bool crit_tag;       // "Kritischer Treffer!" in summary
    bool auto_hit;       // "Automatischer Treffer!"
    bool auto_fail;      // "Automatischer Fehlschlag!"
    char actor[96];
    char target[96];
    char feat[96];       // optional, e.g. "Starke Explosion"
    int  sum_attack;
    int  sum_defense;
    int  sum_damage;
    char angriff_hand[32];     // "Haupthand" / "Nebenhand"
    char angriff_comps[256];   // pre-formatted: "W14+Basis 1+Reichweite 10+..."
    char abwehr_nums[64];      // pre-formatted: "10+3+2" (labels dropped)
    char schaden_text[128];    // full-breakdown form: "Energie 6+Bonus 5 x2"
    char schaden_short[128];   // short-form: "6 Energie + 5 Bonus"  (value first, " + " separator)
};

AttackBlock g_pending = {};

bool MsgStartsWith(const char* s, const char* p) {
    while (*p) { if (*s++ != *p++) return false; }
    return true;
}

void CopyRange(char* dst, size_t cap, const char* start, const char* end) {
    while (start < end && *start == ' ') ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '.')) --end;
    size_t n = (size_t)(end - start);
    if (n >= cap) n = cap - 1;
    memcpy(dst, start, n);
    dst[n] = '\0';
}

bool ParseSummary(const char* text, AttackBlock& b) {
    const auto& L = acc::combat::loc::Get();
    const char* hit_at  = strstr(text, L.phrase_hit);
    const char* miss_at = strstr(text, L.phrase_miss);
    if (!hit_at && !miss_at) return false;
    const char* anchor = hit_at ? hit_at : miss_at;
    size_t alen = strlen(hit_at ? L.phrase_hit : L.phrase_miss);
    b = {};
    b.hit = (hit_at != nullptr);
    CopyRange(b.actor, sizeof(b.actor), text, anchor);

    const char* after_auf = anchor + alen;
    const char* mit_at = strstr(after_auf, L.phrase_mit);
    if (!mit_at) return false;
    size_t mit_len = strlen(L.phrase_mit);

    const char* target_dot = strchr(after_auf, '.');
    if (!target_dot || target_dot > mit_at) return false;
    CopyRange(b.target, sizeof(b.target), after_auf, target_dot);

    const char* feat_start = target_dot + 1;
    while (feat_start < mit_at && *feat_start == ' ') ++feat_start;
    if (feat_start < mit_at) {
        const char* feat_end = strstr(feat_start, L.feat_marker);
        if (feat_end && feat_end < mit_at) {
            CopyRange(b.feat, sizeof(b.feat), feat_start, feat_end);
        }
    }

    const char* p = mit_at + mit_len;
    b.sum_attack = atoi(p);

    const char* vert_at = strstr(p, L.word_verteidigung);
    if (vert_at) b.sum_defense = atoi(vert_at + strlen(L.word_verteidigung));

    const char* schaden_at = strstr(p, L.word_schaden_colon);
    if (schaden_at) b.sum_damage = atoi(schaden_at + strlen(L.word_schaden_colon));

    if (strstr(text, L.tag_krit_summary)) b.crit_tag  = true;
    if (strstr(text, L.tag_auto_hit))     b.auto_hit  = true;
    if (strstr(text, L.tag_auto_fail))    b.auto_fail = true;

    b.has_summary = true;
    return true;
}

bool ParseAngriff(const char* text, AttackBlock& b) {
    const auto& L = acc::combat::loc::Get();
    if (!MsgStartsWith(text, L.prefix_angriff)) return false;
    const char* p = text + strlen(L.prefix_angriff);

    const char* hand_end = p;
    while (*hand_end && *hand_end != ' ') ++hand_end;
    CopyRange(b.angriff_hand, sizeof(b.angriff_hand), p, hand_end);
    p = hand_end;
    while (*p == ' ') ++p;

    if (*p == '-') ++p;
    while (*p >= '0' && *p <= '9') ++p;
    while (*p == ' ') ++p;
    if (*p != '=') return false;
    ++p;
    while (*p == ' ') ++p;

    struct Map { const char* from; size_t flen; const char* to; size_t tlen; };
    const Map kMap[] = {
        { L.token_wuerfel,    strlen(L.token_wuerfel),    L.short_wuerfel,    strlen(L.short_wuerfel) },
        { L.token_gesch_mod,  strlen(L.token_gesch_mod),  L.short_gesch,      strlen(L.short_gesch) },
        { L.token_entfernung, strlen(L.token_entfernung), L.short_reichweite, strlen(L.short_reichweite) },
        { L.token_effekt,     strlen(L.token_effekt),     L.short_effekt,     strlen(L.short_effekt) },
    };

    char out[256];
    size_t out_len = 0;
    bool first = true;

    while (*p) {
        const char* comp_end = strstr(p, "+ ");
        if (!comp_end) comp_end = p + strlen(p);

        // Truncate the W-value component before any auto-hit/auto-fail tail
        // (engine glues "Würfelergebnis 1 Automatischer Fehlschlag!" with no
        // "+" separator). Back up one to include the preceding space.
        const char* auto_h = strstr(p, L.tag_auto_hit);
        const char* auto_f = strstr(p, L.tag_auto_fail);
        const char* auto_at = nullptr;
        if (auto_h && auto_f)      auto_at = (auto_h < auto_f) ? auto_h : auto_f;
        else if (auto_h)           auto_at = auto_h;
        else if (auto_f)           auto_at = auto_f;
        if (auto_at && auto_at > p && auto_at[-1] == ' ') --auto_at;
        if (auto_at && auto_at < comp_end) comp_end = auto_at;

        if (!first && out_len < sizeof(out) - 1) {
            out[out_len++] = '+';
        }
        first = false;

        const Map* m_hit = nullptr;
        for (const auto& m : kMap) {
            if ((size_t)(comp_end - p) >= m.flen &&
                memcmp(p, m.from, m.flen) == 0) {
                m_hit = &m;
                break;
            }
        }
        if (m_hit) {
            size_t copy = m_hit->tlen;
            if (out_len + copy < sizeof(out)) {
                memcpy(out + out_len, m_hit->to, copy);
                out_len += copy;
            }
            const char* tail = p + m_hit->flen;
            copy = (size_t)(comp_end - tail);
            if (out_len + copy >= sizeof(out)) copy = sizeof(out) - 1 - out_len;
            memcpy(out + out_len, tail, copy);
            out_len += copy;
        } else {
            size_t copy = (size_t)(comp_end - p);
            if (out_len + copy >= sizeof(out)) copy = sizeof(out) - 1 - out_len;
            memcpy(out + out_len, p, copy);
            out_len += copy;
        }

        if (auto_at && auto_at == comp_end) break;
        if (!*comp_end) break;
        p = comp_end + 2;
    }
    out[out_len] = '\0';

    // Prepend hand label only when it isn't the default (Nebenhand etc).
    if (b.angriff_hand[0] && strcmp(b.angriff_hand, L.hand_main) != 0) {
        char tmp[300];
        snprintf(tmp, sizeof(tmp), "%s %s", b.angriff_hand, out);
        size_t n = strlen(tmp);
        if (n >= sizeof(b.angriff_comps)) n = sizeof(b.angriff_comps) - 1;
        memcpy(b.angriff_comps, tmp, n);
        b.angriff_comps[n] = '\0';
    } else {
        size_t n = out_len;
        if (n >= sizeof(b.angriff_comps)) n = sizeof(b.angriff_comps) - 1;
        memcpy(b.angriff_comps, out, n);
        b.angriff_comps[n] = '\0';
    }
    b.has_angriff = true;
    return true;
}

bool ParseAbwehr(const char* text, AttackBlock& b) {
    const auto& L = acc::combat::loc::Get();
    if (!MsgStartsWith(text, L.prefix_abwehr)) return false;
    const char* p = text + strlen(L.prefix_abwehr);
    const char* eq = strstr(p, " = ");
    if (!eq) return false;
    p = eq + 3;

    char out[64];
    size_t out_len = 0;
    bool first = true;

    while (*p) {
        const char* comp_end = strstr(p, "+ ");
        if (!comp_end) comp_end = p + strlen(p);

        // Number is whatever follows the last space inside the component.
        const char* num_start = comp_end;
        while (num_start > p && *(num_start - 1) != ' ') --num_start;

        if (!first && num_start[0] != '-' && out_len < sizeof(out) - 1) {
            out[out_len++] = '+';
        }
        first = false;
        size_t n = (size_t)(comp_end - num_start);
        if (out_len + n >= sizeof(out)) n = sizeof(out) - 1 - out_len;
        memcpy(out + out_len, num_start, n);
        out_len += n;

        if (!*comp_end) break;
        p = comp_end + 2;
    }
    out[out_len] = '\0';
    memcpy(b.abwehr_nums, out, out_len + 1);
    b.has_abwehr = true;
    return true;
}

bool ParseSchaden(const char* text, AttackBlock& b) {
    const auto& L = acc::combat::loc::Get();
    if (!MsgStartsWith(text, L.prefix_schaden)) return false;
    const char* p = text + strlen(L.prefix_schaden);

    int mult = 0;
    if (MsgStartsWith(p, L.krit_x_prefix)) {
        p += strlen(L.krit_x_prefix);
        mult = atoi(p);
        const char* fuer = strstr(p, L.phrase_fuer);
        if (fuer) p = fuer + strlen(L.phrase_fuer);
    }

    const char* eq = strstr(p, " = ");
    if (!eq) return false;
    p = eq + 3;

    // Two parallel outputs:
    //   out      — full-breakdown format "Energie 6+Bonus 5"   (type first, +-joined)
    //   shortBuf — short-form format     "6 Energie + 5 Bonus" (value first, " + "-joined)
    char out[128];      size_t out_len = 0;
    char shortBuf[128]; size_t short_len = 0;
    bool first = true;

    auto EmitTo = [](char* dst, size_t cap, size_t& dst_len, const char* s, size_t n) {
        if (dst_len + n >= cap) n = cap - 1 - dst_len;
        memcpy(dst + dst_len, s, n);
        dst_len += n;
    };
    auto Emit      = [&](const char* s, size_t n) { EmitTo(out,      sizeof(out),      out_len,   s, n); };
    auto EmitShort = [&](const char* s, size_t n) { EmitTo(shortBuf, sizeof(shortBuf), short_len, s, n); };

    while (*p) {
        const char* comp_end = strstr(p, " + ");
        if (!comp_end) comp_end = p + strlen(p);

        if (!first) { Emit("+", 1); EmitShort(" + ", 3); }
        first = false;

        // Resolve a (type-label, value-string) pair for this component.
        // Either "Energie: 6" (colon split) or "Bonusschaden 5" (no colon,
        // last-space split with label shortening).
        const char* type_start = p;
        const char* type_end   = nullptr;
        const char* val_start  = nullptr;
        const char* val_end    = comp_end;

        const char* colon = nullptr;
        for (const char* q = p; q + 1 < comp_end; ++q) {
            if (q[0] == ':' && q[1] == ' ') { colon = q; break; }
        }

        // Per-component label shortening replacement for the type label;
        // applied AFTER we determine where the type substring sits.
        const char* type_replacement     = nullptr;
        size_t      type_replacement_len = 0;

        if (colon) {
            type_end  = colon;
            val_start = colon + 2;
        } else {
            // No colon — Bonusschaden style. Number is whatever follows the
            // last space.
            const char* space = comp_end;
            while (space > p && *(space - 1) != ' ') --space;
            type_end = (space > p) ? (space - 1) : p;
            val_start = space;

            const size_t kBonusFromLen = strlen(L.token_bonusschaden);
            if ((size_t)(type_end - type_start) == kBonusFromLen &&
                memcmp(type_start, L.token_bonusschaden, kBonusFromLen) == 0) {
                type_replacement     = L.short_bonus;
                type_replacement_len = strlen(L.short_bonus);
            }
        }

        // Full-breakdown form: "<Type> <value>"
        if (type_replacement) {
            Emit(type_replacement, type_replacement_len);
        } else {
            Emit(type_start, (size_t)(type_end - type_start));
        }
        Emit(" ", 1);
        Emit(val_start, (size_t)(val_end - val_start));

        // Short-form: "<value> <Type>"
        EmitShort(val_start, (size_t)(val_end - val_start));
        EmitShort(" ", 1);
        if (type_replacement) {
            EmitShort(type_replacement, type_replacement_len);
        } else {
            EmitShort(type_start, (size_t)(type_end - type_start));
        }

        if (!*comp_end) break;
        p = comp_end + 3;  // skip " + "
    }

    if (mult > 0) {
        char tail[16];
        int n = snprintf(tail, sizeof(tail), " x%d", mult);
        if (n > 0) Emit(tail, (size_t)n);
    }
    out[out_len]            = '\0';
    shortBuf[short_len]     = '\0';
    memcpy(b.schaden_text,  out,      out_len   + 1);
    memcpy(b.schaden_short, shortBuf, short_len + 1);
    b.has_schaden = true;
    return true;
}

void BuildCompact(const AttackBlock& b, char* out, size_t cap) {
    const auto& L = acc::combat::loc::Get();
    const char* verb = b.hit ? L.verb_hit : L.verb_miss;

    char crit_part[32] = "";
    if (b.hit && b.crit_tag) snprintf(crit_part, sizeof(crit_part), " %s", L.word_critical);

    char feat_part[128] = "";
    if (b.feat[0]) snprintf(feat_part, sizeof(feat_part), " (%s)", b.feat);

    char auto_part[64] = "";
    if (b.auto_hit)       snprintf(auto_part, sizeof(auto_part), " %s", L.word_auto_hit);
    else if (b.auto_fail) snprintf(auto_part, sizeof(auto_part), " %s", L.word_auto_fail);

    if (b.has_angriff && b.has_abwehr && b.has_schaden) {
        snprintf(out, cap,
                 "%s %s%s %s%s. %s %d (%s) %s %s %d (%s). %s.%s",
                 b.actor, verb, crit_part, b.target, feat_part,
                 L.word_angriff, b.sum_attack, b.angriff_comps,
                 L.word_gg, L.word_vert, b.sum_defense, b.abwehr_nums,
                 b.schaden_text, auto_part);
    } else if (b.has_angriff && b.has_abwehr) {
        if (b.hit) {
            snprintf(out, cap,
                     "%s %s%s %s%s. %s %d (%s) %s %s %d (%s). %s %d.%s",
                     b.actor, verb, crit_part, b.target, feat_part,
                     L.word_angriff, b.sum_attack, b.angriff_comps,
                     L.word_gg, L.word_vert, b.sum_defense, b.abwehr_nums,
                     L.word_schaden, b.sum_damage, auto_part);
        } else {
            snprintf(out, cap,
                     "%s %s%s %s%s. %s %d (%s) %s %s %d (%s).%s",
                     b.actor, verb, crit_part, b.target, feat_part,
                     L.word_angriff, b.sum_attack, b.angriff_comps,
                     L.word_gg, L.word_vert, b.sum_defense, b.abwehr_nums,
                     auto_part);
        }
    } else if (b.has_angriff) {
        snprintf(out, cap,
                 "%s %s%s %s%s. %s %d (%s) %s %s %d.%s",
                 b.actor, verb, crit_part, b.target, feat_part,
                 L.word_angriff, b.sum_attack, b.angriff_comps,
                 L.word_gg, L.word_vert, b.sum_defense, auto_part);
    } else {
        if (b.hit) {
            snprintf(out, cap,
                     "%s %s%s %s%s. %s %d %s %s %d. %s %d.%s",
                     b.actor, verb, crit_part, b.target, feat_part,
                     L.word_angriff, b.sum_attack,
                     L.word_gg, L.word_vert, b.sum_defense,
                     L.word_schaden, b.sum_damage, auto_part);
        } else {
            snprintf(out, cap,
                     "%s %s%s %s%s. %s %d %s %s %d.%s",
                     b.actor, verb, crit_part, b.target, feat_part,
                     L.word_angriff, b.sum_attack,
                     L.word_gg, L.word_vert, b.sum_defense, auto_part);
        }
    }
}

// Short-form announcement for vanilla hits landing on a party member.
// Format: "<target>: <value> <type>[ + <value> <type>...] von <attacker>[, kritisch]".
void BuildShortForm(const AttackBlock& b, char* out, size_t cap) {
    const auto& L = acc::combat::loc::Get();

    char tail[64] = "";
    if (b.crit_tag)       snprintf(tail, sizeof(tail), ", %s", L.word_critical);
    else if (b.auto_hit)  snprintf(tail, sizeof(tail), ", %s", L.word_auto_hit);

    snprintf(out, cap, "%s: %s %s %s%s",
             b.target,
             b.has_schaden ? b.schaden_short : "",
             L.word_von,
             b.actor,
             tail);
}

// Filter + emit decision for a fully-buffered attack block.
//
//   feat-use            → full breakdown (BuildCompact)
//   vanilla hit on party → short form    (BuildShortForm)
//   anything else       → suppress speech (still log for grepping)
//
// "Party" = active roster of CSWPartyTable (PC + up to 2 followers in
// normal play). Cross-fire between two NPCs, vanilla swings the PC or
// party makes, and all misses fall under "anything else".
void FlushPending() {
    if (!g_pending.has_summary) return;
    auto& r = acc::msg::Router::Instance();

    const bool is_feat         = (g_pending.feat[0] != 0);
    const bool target_is_party = IsPartyMember(g_pending.target);

    char buf[640];
    if (is_feat) {
        BuildCompact(g_pending, buf, sizeof(buf));
        r.Speak(buf);
        r.LogEmit("emit-feat", buf);
    } else if (g_pending.hit && target_is_party) {
        BuildShortForm(g_pending, buf, sizeof(buf));
        r.Speak(buf);
        r.LogEmit("emit-short", buf);
    } else {
        BuildCompact(g_pending, buf, sizeof(buf));
        r.LogEmit("emit-suppressed", buf);
    }
    g_pending = {};
}

// Router rules. Each returns true to claim the line (suppress raw speech).

bool RuleSummary(const char* text) {
    AttackBlock probe = {};
    if (!ParseSummary(text, probe)) return false;
    if (g_pending.has_summary) FlushPending();
    g_pending = probe;
    return true;
}

bool RuleAngriff(const char* text) {
    if (!g_pending.has_summary) return false;
    return ParseAngriff(text, g_pending);
}

bool RuleAbwehr(const char* text) {
    if (!g_pending.has_summary) return false;
    return ParseAbwehr(text, g_pending);
}

bool RuleSchaden(const char* text) {
    if (!g_pending.has_summary) return false;
    if (!ParseSchaden(text, g_pending)) return false;
    FlushPending();
    return true;
}

bool RuleBedrohung(const char* text) {
    if (!g_pending.has_summary) return false;
    const auto& L = acc::combat::loc::Get();
    if (!MsgStartsWith(text, L.prefix_bedrohung)) return false;
    acc::msg::Router::Instance().Speak(text);
    return true;  // claimed: speak handled here, don't fall through
}

void OnUnmatched(const char* /*text*/) {
    // Unknown mid-block line is a clean boundary — emit whatever
    // breakdown we have (typically a miss whose Schadensstatistik
    // never arrives) before the router speaks the new line raw.
    if (g_pending.has_summary) FlushPending();
}

}  // namespace

void RegisterCombatMsgRules() {
    auto& r = acc::msg::Router::Instance();
    r.AddRule("CombatSummary",   RuleSummary);
    r.AddRule("CombatAngriff",   RuleAngriff);
    r.AddRule("CombatAbwehr",    RuleAbwehr);
    r.AddRule("CombatSchaden",   RuleSchaden);
    r.AddRule("CombatBedrohung", RuleBedrohung);
    r.AddOnUnmatched(OnUnmatched);
}

}  // namespace acc::combat
