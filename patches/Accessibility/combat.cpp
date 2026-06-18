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
#include "unified_action_menu.h" // ForceDisarm — auto-close the queueing menu
                                 // when combat ends (experimental)
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

    // Two distinct signals (see action-menu-and-combat.md "combat-mode"):
    //   leaderInCombat — the engine global (GetCombatMode). It only mirrors the
    //     *controlled* leader's combat bit and is re-synced to the new leader on
    //     every Tab (CSWParty::SetLeader → SetCombatMode), so it flips to peace
    //     when you switch to a not-yet-engaged member even mid-encounter.
    //   partyInCombat — OR of every party member's per-creature combat bit. This
    //     is the true "is the encounter active" state, immune to which member
    //     happens to be controlled. We drive the strong begin/end cue (and the
    //     menu auto-close) off THIS so a party switch can't fake a combat end.
    bool leaderInCombat = (mode != 0);
    bool partyInCombat  = acc::engine::IsAnyPartyMemberInCombat() || leaderInCombat;

    DWORD now = GetTickCount();

    // ---- Strong cue: real encounter begin/end, debounced on partyInCombat. ----
    static int   s_lastParty    = -1;        // -1 = first tick, suppress
    static int   s_pendingParty = -1;
    static DWORD s_partyChanged  = 0;

    if (s_lastParty < 0) {
        s_lastParty    = partyInCombat ? 1 : 0;
        s_pendingParty = s_lastParty;
        s_partyChanged = now;
        acclog::Write("Combat.Mode", "first-tick suppress; mode=%d party=%d",
                      mode, partyInCombat);
    } else {
        int desired = partyInCombat ? 1 : 0;
        if (desired != s_pendingParty) {
            s_pendingParty = desired;
            s_partyChanged = now;
        }
        if (s_pendingParty != s_lastParty &&
            (now - s_partyChanged) >= kCombatModeQuietMs) {
            bool nowInCombat = (s_pendingParty == 1);
            auto id = nowInCombat ? acc::strings::Id::CombatBegins
                                  : acc::strings::Id::CombatEnds;
            const char* phrase = acc::strings::Get(id);
            prism::Speak(phrase, /*interrupt=*/false);
            acclog::Write("Combat.Mode", "%s -> [%s] (debounced %ums)",
                          nowInCombat ? "entering" : "leaving",
                          phrase, static_cast<unsigned>(now - s_partyChanged));
            s_lastParty = s_pendingParty;

            // Auto-close the unified action menu the moment the *encounter*
            // ends — it's a persistent paused queueing surface that otherwise
            // lingers across the combat→explore boundary, so the first
            // post-fight Enter lands on a menu entry instead of the world
            // object the user meant to use (patch-20260617-215141.log: Enter
            // queued "Heilen" instead of using the Versorgungsstation). Gated
            // on partyInCombat, not the leader global, so a Tab to a peaceful
            // member no longer tears the menu down (and releases its pause)
            // mid-fight.
            if (!nowInCombat && acc::unified_menu::IsActive()) {
                acc::unified_menu::ForceDisarm("combat-end");
            }
        }
    }

    // ---- Subtle cue: the controlled leader is at peace while the encounter
    //      continues (Tab to a not-yet-engaged member, or the leader breaking
    //      off while companions still fight). Debounced on the leader global so
    //      a flicker doesn't spam; only the falling edge while the party is
    //      still fighting is surprising enough to call out. The inverse (Tab
    //      onto a fighting member) is covered by the leader-name announce. ----
    static int   s_lastLeader    = -1;
    static int   s_pendingLeader = -1;
    static DWORD s_leaderChanged  = 0;

    if (s_lastLeader < 0) {
        s_lastLeader    = leaderInCombat ? 1 : 0;
        s_pendingLeader = s_lastLeader;
        s_leaderChanged = now;
    } else {
        int desired = leaderInCombat ? 1 : 0;
        if (desired != s_pendingLeader) {
            s_pendingLeader = desired;
            s_leaderChanged = now;
        }
        if (s_pendingLeader != s_lastLeader &&
            (now - s_leaderChanged) >= kCombatModeQuietMs) {
            bool wasLeaving = (s_pendingLeader == 0);
            s_lastLeader = s_pendingLeader;
            // Only speak when the leader dropped to peace but the encounter is
            // still live (partyInCombat). On a real end both fall together and
            // partyInCombat is already false here, so the strong "Kampf
            // beendet" fires instead and this stays silent.
            if (wasLeaving && partyInCombat) {
                const char* phrase =
                    acc::strings::Get(acc::strings::Id::CombatLeaderAtPeace);
                prism::Speak(phrase, /*interrupt=*/false);
                acclog::Write("Combat.Mode",
                              "leader at peace while party fights -> [%s]",
                              phrase);
            }
        }
    }
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
    char dmg_type[32];         // primary damage type label, e.g. "Energie" / "Physisch"
    char status[96];           // applied status from summary tail, e.g. "betäubt" ("" if none)
};

AttackBlock g_pending = {};

// ---- Damage-absorption burst coalescing.
// One engine line fires per blocked hit; a sustained shield emits dozens.
// We accumulate a burst (running total + count) and flush a single spoken
// line once kAbsorbQuietMs passes with no new absorb. namepart/suffix are
// captured verbatim from the engine line so the spoken total stays locale-
// agnostic: "<namepart>absorbiert <total><suffix>".
constexpr DWORD kAbsorbQuietMs   = 600;    // flush after this long with no new absorb
constexpr DWORD kAbsorbMaxHoldMs = 2500;   // ...or this long since the burst began
char  g_absorb_namepart[160] = {};   // text before the anchor (incl. trailing space)
char  g_absorb_suffix[64]    = {};   // text after the number, up to " :" or end
int   g_absorb_total         = 0;
int   g_absorb_count         = 0;
DWORD g_absorb_last_tick     = 0;
DWORD g_absorb_first_tick    = 0;

// ---- Ability / grenade / force-power effect merging.
// A power's use, each target's saving throw, the damage, and any applied
// status arrive as separate engine lines (interleaved with other combat
// chatter). We remember the last "benutzt <action>" and accumulate per-
// target outcomes into slots, flushed as one merged line on a debounce.
constexpr DWORD kAbilityWindowMs = 5000;   // "benutzt" stays attributable this long
constexpr DWORD kFxQuietMs       = 500;    // flush a target's effect after this gap
constexpr DWORD kFxMaxHoldMs     = 2500;
constexpr int   kMaxFx           = 12;

struct LastAbility {
    char  actor[96];
    char  action[96];
    DWORD tick;
    bool  valid;
};
LastAbility g_lastAbility = {};

struct EffectTarget {
    bool  used;
    char  target[96];
    char  action[96];   // attributed power/grenade ("" if unknown)
    char  caster[96];   // who used it ("" if unknown)
    bool  has_save;
    bool  saved;
    char  save_type[40];  // "Reflex" / "Tapferkeit" / "Willensstärke"
    bool  has_dmg;
    int   dmg;
    char  dmg_type[32];
    char  status[64];     // applied status, e.g. "betäubt"
    DWORD first_tick;
    DWORD last_tick;
};
EffectTarget g_fx[kMaxFx] = {};

EffectTarget* FxFind(const char* target) {
    for (int i = 0; i < kMaxFx; ++i)
        if (g_fx[i].used && strcmp(g_fx[i].target, target) == 0) return &g_fx[i];
    return nullptr;
}

// Find-or-allocate a slot for target. On first allocation, attribute the
// most recent ability use if it's still fresh. Returns nullptr if the table
// is full (caller then leaves the line on the raw-speech fallback).
EffectTarget* FxAlloc(const char* target) {
    EffectTarget* e = FxFind(target);
    if (e) return e;
    for (int i = 0; i < kMaxFx; ++i) {
        if (g_fx[i].used) continue;
        e = &g_fx[i];
        *e = {};
        e->used = true;
        size_t n = strnlen(target, sizeof(e->target) - 1);
        memcpy(e->target, target, n); e->target[n] = '\0';
        DWORD now = GetTickCount();
        e->first_tick = now;
        e->last_tick  = now;
        if (g_lastAbility.valid && (now - g_lastAbility.tick) <= kAbilityWindowMs) {
            memcpy(e->action, g_lastAbility.action, strnlen(g_lastAbility.action, sizeof(e->action) - 1) + 1);
            memcpy(e->caster, g_lastAbility.actor,  strnlen(g_lastAbility.actor,  sizeof(e->caster) - 1) + 1);
        }
        return e;
    }
    return nullptr;
}

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

    // Status tail: an applied effect rides the end of the summary as
    // "<target> ist <status>" (e.g. "Kath-Hund ist betäubt"). The target
    // name only re-appears here in that clause — "mit Angriff auf <target>."
    // is followed by '.', not " ist " — so a plain search for "<target> ist "
    // pins the status without colliding with the opening "<actor> ist
    // erfolgreich". Captures everything after to end-of-line.
    if (b.target[0]) {
        char needle[112];
        int nn = snprintf(needle, sizeof(needle), "%s%s", b.target, L.status_ist_marker);
        if (nn > 0 && nn < (int)sizeof(needle)) {
            const char* st = strstr(text, needle);
            if (st) {
                const char* s = st + nn;
                const char* e = s + strlen(s);
                CopyRange(b.status, sizeof(b.status), s, e);
            }
        }
    }

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

        // Primary damage type = first component's type label (the elemental
        // type: Energie / Physisch / Ionen / ...). Bonus components
        // (Spezialwaffe, Stärke-Mod., Bonusschaden) are additive, not types,
        // and always follow — so the first label is the one to speak.
        if (b.dmg_type[0] == '\0' && !type_replacement && type_end > type_start) {
            CopyRange(b.dmg_type, sizeof(b.dmg_type), type_start, type_end);
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

// ---- Phase-2 short results-only lines.
//
// The verbose stat breakdown (Angriffs-/Abwehr-/Schadensstatistik) stays in
// the engine message log; only the urgent result is spoken. Actor-led for
// the party's own special moves, victim-led for incoming hits — the
// "who's hurt" name leads when damage lands on us.
//
// Common tail: ", kritisch" on a crit and ", <status>" when an effect was
// applied. Damage type keeps the engine's casing (German type nouns are
// capitalised: "Energie", "Physisch", "Ionen").
namespace {
void BuildResultTail(const AttackBlock& b, char* tail, size_t cap) {
    const auto& L = acc::combat::loc::Get();
    char crit[24] = "";
    if (b.crit_tag) snprintf(crit, sizeof(crit), ", %s", L.word_critical);
    char status[112] = "";
    if (b.status[0]) snprintf(status, sizeof(status), ", %s", b.status);
    snprintf(tail, cap, "%s%s", crit, status);
}
}  // namespace

// The party's own attack. Feats name the move; plain auto-attacks omit it
// (a feat replaces the auto-attack for that swing, so the two never collide):
//   feat hit    → "<actor>, <feat>, <dmg> <type>[, kritisch][, status]"
//   feat miss   → "<actor>, fehlgeschlagen, <feat>"
//   plain hit   → "<actor>, <dmg> <type>[, kritisch][, status]"
//   plain miss  → (not emitted — caller suppresses)
void BuildOutgoingLine(const AttackBlock& b, char* out, size_t cap) {
    const auto& L = acc::combat::loc::Get();
    const bool has_feat = (b.feat[0] != 0);
    if (!b.hit) {
        // Only feats reach a spoken miss; plain misses are suppressed upstream.
        snprintf(out, cap, "%s, %s, %s", b.actor, L.word_failed, b.feat);
        return;
    }
    char tail[140]; BuildResultTail(b, tail, sizeof(tail));
    char feat_part[110] = "";
    if (has_feat) snprintf(feat_part, sizeof(feat_part), ", %s", b.feat);
    if (b.dmg_type[0]) {
        snprintf(out, cap, "%s%s, %d %s%s",
                 b.actor, feat_part, b.sum_damage, b.dmg_type, tail);
    } else {
        snprintf(out, cap, "%s%s, %d%s",
                 b.actor, feat_part, b.sum_damage, tail);
    }
}

// Incoming hit on a party member: "<target>: <dmg> <type> von <actor>[, ...]".
void BuildIncomingLine(const AttackBlock& b, char* out, size_t cap) {
    const auto& L = acc::combat::loc::Get();
    char tail[140]; BuildResultTail(b, tail, sizeof(tail));
    if (b.dmg_type[0]) {
        snprintf(out, cap, "%s: %d %s %s %s%s",
                 b.target, b.sum_damage, b.dmg_type, L.word_von, b.actor, tail);
    } else {
        snprintf(out, cap, "%s: %d %s %s%s",
                 b.target, b.sum_damage, L.word_von, b.actor, tail);
    }
}

// A status condition applied to a party member (stunned, poisoned, paralysed,
// feared — whatever the engine echoes in "<target> ist <status>") is a
// disabling state the player needs to act on, so it shouldn't wait behind
// queued combat chatter. When `victim` is a party member and `status` is
// non-empty, deliver it as its own urgent cue on the SAPI channel
// ("<victim> ist <status>") — the same channel a kill uses — and return true
// so the caller drops the status from the normal-priority damage/effect line.
// No new information: the status word is moved to the urgent channel, not
// duplicated; the (non-urgent) damage stays on the normal channel.
bool MaybeSpeakStatusUrgent(const char* victim, const char* status) {
    if (!status || !status[0] || !IsPartyMember(victim)) return false;
    const auto& L = acc::combat::loc::Get();
    char line[160];
    snprintf(line, sizeof(line), "%s%s%s", victim, L.status_ist_marker, status);
    prism::SpeakUrgent(line);
    acc::msg::Router::Instance().LogEmit("emit-status-urgent", line);
    return true;
}

// Filter + emit decision for a fully-buffered attack block.
//
//   party attack (feat, or plain hit)  → actor-led result line
//     - feats announce on hit or miss (a deliberate move that whiffs matters)
//     - plain auto-attacks announce on hit only (the damage is what kills;
//       a plain miss isn't urgent)
//   incoming hit (party target)         → victim-led result line
//   anything else                       → suppress speech (still log)
//
// "Party" = active roster of CSWPartyTable (PC + followers). NPC-vs-NPC
// crossfire and the party's plain *misses* stay silent — full detail
// remains in the engine message log.
void FlushPending() {
    if (!g_pending.has_summary) return;
    auto& r = acc::msg::Router::Instance();

    const bool is_feat         = (g_pending.feat[0] != 0);
    const bool actor_is_party  = IsPartyMember(g_pending.actor);
    const bool target_is_party = IsPartyMember(g_pending.target);
    const bool announce_outgoing = actor_is_party && (is_feat || g_pending.hit);

    char buf[640];
    if (announce_outgoing) {
        BuildOutgoingLine(g_pending, buf, sizeof(buf));
        r.Speak(buf);
        r.LogEmit("emit-outgoing", buf);
    } else if (g_pending.hit && target_is_party) {
        // Peel any applied status off the damage line and re-speak it
        // urgently (see MaybeSpeakStatusUrgent). The damage stays normal.
        if (MaybeSpeakStatusUrgent(g_pending.target, g_pending.status))
            g_pending.status[0] = '\0';
        BuildIncomingLine(g_pending, buf, sizeof(buf));
        r.Speak(buf);
        r.LogEmit("emit-incoming", buf);
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

// "Auswirkungsstatistik:<target> ist <status>" — the engine's trailing status
// echo. Two cases:
//   * A force power applied it: an effect slot for <target> already exists —
//     attach the status so the merged effect line speaks it.
//   * A weapon attack applied it: no slot exists; the status was already
//     folded into the attack line from the summary tail — drop the duplicate.
// Either way we claim the line (no raw speech).
bool RuleAuswirkung(const char* text) {
    const auto& L = acc::combat::loc::Get();
    if (!MsgStartsWith(text, L.prefix_auswirkung)) return false;
    const char* p   = text + strlen(L.prefix_auswirkung);
    const char* ist = strstr(p, L.status_ist_marker);
    if (ist) {
        char target[96];
        CopyRange(target, sizeof(target), p, ist);
        EffectTarget* e = FxFind(target);
        if (e) {
            const char* s = ist + strlen(L.status_ist_marker);
            CopyRange(e->status, sizeof(e->status), s, s + strlen(s));
            e->last_tick = GetTickCount();
        }
    }
    return true;  // claimed, no speech
}

// "<actor> benutzt <action>[.  Investierte Machtpunkte: ...]" — a power/grenade
// /item use. Record it for effect attribution, and speak a shortened cue
// (dropping the Machtpunkte math) only for the party's own casts/throws —
// incoming enemy powers announce via their effect line instead.
bool RuleAbilityUse(const char* text) {
    const auto& L = acc::combat::loc::Get();
    const char* m = strstr(text, L.ability_use_marker);   // " benutzt "
    if (!m) return false;
    char actor[96];
    CopyRange(actor, sizeof(actor), text, m);
    const char* as  = m + strlen(L.ability_use_marker);
    const char* dot = strchr(as, '.');
    const char* ae  = dot ? dot : as + strlen(as);
    char action[96];
    CopyRange(action, sizeof(action), as, ae);
    if (!actor[0] || !action[0]) return false;

    DWORD now = GetTickCount();
    memcpy(g_lastAbility.actor,  actor,  strnlen(actor,  sizeof(g_lastAbility.actor)  - 1) + 1);
    memcpy(g_lastAbility.action, action, strnlen(action, sizeof(g_lastAbility.action) - 1) + 1);
    g_lastAbility.tick  = now;
    g_lastAbility.valid = true;

    auto& r = acc::msg::Router::Instance();
    if (IsPartyMember(actor)) {
        char line[200];
        snprintf(line, sizeof(line), "%s%s%s", actor, L.ability_use_marker, action);
        r.Speak(line);
        r.LogEmit("emit-use", line);
    } else {
        r.LogEmit("emit-use-suppressed", action);
    }
    return true;  // claimed (suppress the verbose raw use line)
}

// "<saver> <SaveType>-Rettungswurf. Erfolg!|Misserfolg! ..." — buffer the
// outcome into the saver's effect slot; flushed merged on the debounce.
bool RuleSaveThrow(const char* text) {
    const auto& L = acc::combat::loc::Get();
    const char* sm = strstr(text, L.save_marker);   // "-Rettungswurf."
    if (!sm) return false;
    const char* ts = sm;
    while (ts > text && ts[-1] != ' ') --ts;        // start of the SaveType word
    char save_type[40];
    CopyRange(save_type, sizeof(save_type), ts, sm);
    char saver[96];
    CopyRange(saver, sizeof(saver), text, ts);
    if (!saver[0]) return false;

    bool saved;
    if (strstr(sm, L.save_success))   saved = true;
    else if (strstr(sm, L.save_fail)) saved = false;
    else return false;

    EffectTarget* e = FxAlloc(saver);
    if (!e) return false;  // table full — let it speak raw
    e->has_save = true;
    e->saved    = saved;
    memcpy(e->save_type, save_type, strnlen(save_type, sizeof(e->save_type) - 1) + 1);
    e->last_tick = GetTickCount();
    return true;
}

// "<actor> verletzt <target>: N Schaden (Type: N)" — direct/ability damage.
bool RuleDirectDamage(const char* text) {
    const auto& L = acc::combat::loc::Get();
    const char* dm = strstr(text, L.damage_marker);  // " verletzt "
    if (!dm) return false;
    char actor[96];
    CopyRange(actor, sizeof(actor), text, dm);
    const char* tstart = dm + strlen(L.damage_marker);
    const char* colon  = strchr(tstart, ':');
    if (!colon) return false;
    char target[96];
    CopyRange(target, sizeof(target), tstart, colon);
    if (!actor[0] || !target[0]) return false;

    const char* p = colon + 1;
    while (*p == ' ') ++p;
    int dmg = atoi(p);

    char dtype[32] = "";
    const char* paren = strchr(p, '(');
    if (paren) {
        const char* tt = paren + 1;
        const char* tc = strchr(tt, ':');
        if (tc) CopyRange(dtype, sizeof(dtype), tt, tc);
    }

    EffectTarget* e = FxAlloc(target);
    if (!e) return false;
    e->has_dmg = true;
    e->dmg     = dmg;
    if (dtype[0]) memcpy(e->dmg_type, dtype, strnlen(dtype, sizeof(e->dmg_type) - 1) + 1);
    if (!e->caster[0]) memcpy(e->caster, actor, strnlen(actor, sizeof(e->caster) - 1) + 1);
    e->last_tick = GetTickCount();
    return true;
}

// Build one merged per-target effect line. Empty `out` = nothing to say.
void BuildEffectLine(const EffectTarget& e, char* out, size_t cap) {
    const auto& L = acc::combat::loc::Get();
    out[0] = '\0';

    char dmg_clause[48] = "";
    if (e.has_dmg && e.dmg > 0) {
        if (e.dmg_type[0]) snprintf(dmg_clause, sizeof(dmg_clause), "%d %s", e.dmg, e.dmg_type);
        else               snprintf(dmg_clause, sizeof(dmg_clause), "%d", e.dmg);
    }
    char von_action[120] = "";   // " von <action>"
    if (e.action[0]) snprintf(von_action, sizeof(von_action), " %s %s", L.word_von, e.action);
    char action_only[110] = "";  // " <action>"
    if (e.action[0]) snprintf(action_only, sizeof(action_only), " %s", e.action);

    if (e.has_save && e.saved) {
        // Resisted: "<target> widersteht <action>[, <dmg>], <SaveType>"
        if (dmg_clause[0])
            snprintf(out, cap, "%s %s%s, %s, %s",
                     e.target, L.word_resists, action_only, dmg_clause, e.save_type);
        else
            snprintf(out, cap, "%s %s%s, %s",
                     e.target, L.word_resists, action_only, e.save_type);
    } else if (e.has_save && !e.saved) {
        // Failed save: damage and/or status, tagged "<SaveType> misslungen".
        if (dmg_clause[0] && e.status[0])
            snprintf(out, cap, "%s: %s%s, %s, %s %s",
                     e.target, dmg_clause, von_action, e.status, e.save_type, L.word_save_failed);
        else if (dmg_clause[0])
            snprintf(out, cap, "%s: %s%s, %s %s",
                     e.target, dmg_clause, von_action, e.save_type, L.word_save_failed);
        else if (e.status[0])
            snprintf(out, cap, "%s %s%s, %s %s",
                     e.target, e.status, von_action, e.save_type, L.word_save_failed);
        else
            snprintf(out, cap, "%s%s, %s %s",
                     e.target, action_only, e.save_type, L.word_save_failed);
    } else {
        // No save (direct damage / status): "<target>: <dmg> von <action>[, <status>]"
        char status_clause[80] = "";
        if (e.status[0]) snprintf(status_clause, sizeof(status_clause), ", %s", e.status);
        if (dmg_clause[0])
            snprintf(out, cap, "%s: %s%s%s", e.target, dmg_clause, von_action, status_clause);
        else if (e.status[0])
            snprintf(out, cap, "%s%s%s", e.target, action_only, status_clause);
        // else: a bare save-less, damage-less, status-less line — nothing to say.
    }
}

// Speak (and clear) one accumulated effect slot. Party-relevant only — an
// NPC-vs-NPC power exchange is logged but not spoken, matching the weapon path.
void FlushEffect(EffectTarget& e) {
    // A force/grenade status on a party member: lift it to the urgent channel
    // before building the line, so the disabling state cuts through (same as
    // the weapon path in FlushPending). Status moved, not duplicated.
    if (MaybeSpeakStatusUrgent(e.target, e.status)) e.status[0] = '\0';
    char line[300];
    BuildEffectLine(e, line, sizeof(line));
    if (line[0]) {
        auto& r = acc::msg::Router::Instance();
        // Suppress ONLY a positively-identified NPC-vs-NPC effect: a known
        // non-party caster acting on a non-party target. An unknown caster
        // (powers that emit no "benutzt" line — e.g. a Betäuben queued during
        // pause) defaults to announce: in the player's own fight an
        // unattributed effect is almost always something we caused.
        bool caster_is_party = (e.caster[0] && IsPartyMember(e.caster));
        bool target_is_party = IsPartyMember(e.target);
        bool known_npc_vs_npc =
            (e.caster[0] && !caster_is_party && !target_is_party);
        if (!known_npc_vs_npc) { r.Speak(line); r.LogEmit("emit-effect", line); }
        else                   { r.LogEmit("emit-effect-suppressed", line); }
    }
    e = {};  // clears used
}

// Flush the pending absorb burst as one spoken total. Reconstructed from the
// captured namepart + engine anchor + accumulated total + suffix.
void FlushAbsorb() {
    if (g_absorb_count == 0) return;
    const auto& L = acc::combat::loc::Get();
    char line[256];
    snprintf(line, sizeof(line), "%s%s %d%s",
             g_absorb_namepart, L.absorb_anchor, g_absorb_total, g_absorb_suffix);
    auto& r = acc::msg::Router::Instance();
    r.Speak(line);
    r.LogEmit("emit-absorb", line);
    g_absorb_namepart[0] = '\0';
    g_absorb_suffix[0]   = '\0';
    g_absorb_total = 0;
    g_absorb_count = 0;
}

// Accumulate a damage-absorption line into the pending burst (claimed — no
// raw speech). TickCombatAbsorb flushes it on the debounce.
bool RuleAbsorb(const char* text) {
    const auto& L = acc::combat::loc::Get();
    const char* a = strstr(text, L.absorb_anchor);
    if (!a) return false;
    const char* p = a + strlen(L.absorb_anchor);
    while (*p == ' ') ++p;
    if (*p < '0' || *p > '9') return false;   // unexpected shape — let it speak raw
    int pts = atoi(p);
    const char* numEnd = p;
    while (*numEnd >= '0' && *numEnd <= '9') ++numEnd;

    char namepart[160];
    size_t nlen = (size_t)(a - text);
    if (nlen >= sizeof(namepart)) nlen = sizeof(namepart) - 1;
    memcpy(namepart, text, nlen);
    namepart[nlen] = '\0';

    char suffix[64];
    const char* cut = strstr(numEnd, " :");        // drop "...: M Punkte verbleiben"
    const char* e   = cut ? cut : numEnd + strlen(numEnd);
    size_t slen = (size_t)(e - numEnd);
    if (slen >= sizeof(suffix)) slen = sizeof(suffix) - 1;
    memcpy(suffix, numEnd, slen);
    suffix[slen] = '\0';

    // A different absorber than the pending burst → flush the old one first.
    if (g_absorb_count > 0 && strcmp(namepart, g_absorb_namepart) != 0) {
        FlushAbsorb();
    }
    DWORD now = GetTickCount();
    if (g_absorb_count == 0) g_absorb_first_tick = now;
    memcpy(g_absorb_namepart, namepart, nlen + 1);
    memcpy(g_absorb_suffix,   suffix,   slen + 1);
    g_absorb_total += pts;
    ++g_absorb_count;
    g_absorb_last_tick = now;
    return true;  // claimed
}

// Bedrohungsstatistik is the critical-hit *confirmation* roll. Its only
// actionable outcome — did the hit crit — is already carried by the summary's
// "Kritischer Treffer!" tag, which the merged attack line speaks as
// ", kritisch". The roll math (threat range, confirmation attack vs defense)
// is review-log detail, so claim + suppress it instead of speaking it raw.
bool RuleBedrohung(const char* text) {
    const auto& L = acc::combat::loc::Get();
    if (!MsgStartsWith(text, L.prefix_bedrohung)) return false;
    return true;  // claimed, no speech
}

// "<actor> neutralisiert <target>: N EPs" — a defeat. Speak it verbatim, but
// route through the urgent SAPI channel so a kill cuts through queued combat
// chatter instead of waiting behind it.
bool RuleKill(const char* text) {
    const auto& L = acc::combat::loc::Get();
    if (!strstr(text, L.kill_marker)) return false;
    prism::SpeakUrgent(text);
    return true;  // claimed: urgent speech handled here
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
    r.AddRule("CombatAuswirkung", RuleAuswirkung);
    r.AddRule("CombatAbsorb",    RuleAbsorb);
    r.AddRule("CombatAbilityUse", RuleAbilityUse);
    r.AddRule("CombatSaveThrow", RuleSaveThrow);
    r.AddRule("CombatDirectDamage", RuleDirectDamage);
    r.AddRule("CombatKill",      RuleKill);
    r.AddRule("CombatBedrohung", RuleBedrohung);
    r.AddOnUnmatched(OnUnmatched);
}

void TickCombatAbsorb() {
    if (g_absorb_count == 0) return;
    DWORD now = GetTickCount();
    bool quiet  = (now - g_absorb_last_tick)  >= kAbsorbQuietMs;
    bool maxAge = (now - g_absorb_first_tick) >= kAbsorbMaxHoldMs;
    if (quiet || maxAge) FlushAbsorb();
}

void TickCombatEffects() {
    DWORD now = GetTickCount();
    for (int i = 0; i < kMaxFx; ++i) {
        if (!g_fx[i].used) continue;
        bool quiet  = (now - g_fx[i].last_tick)  >= kFxQuietMs;
        bool maxAge = (now - g_fx[i].first_tick) >= kFxMaxHoldMs;
        if (quiet || maxAge) FlushEffect(g_fx[i]);
    }
}

}  // namespace acc::combat
