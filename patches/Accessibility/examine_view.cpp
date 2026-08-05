#include "examine_view.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"
#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_reads.h"   // LookupTlk — strref → localized text, both games
#include "engine_app.h"     // GetClientApp
#include "engine_player.h"
#include "engine_subscreen.h" // Begin/EndOverlayPause — freeze the world while open
#include "hotkeys.h"
#include "log.h"
#include "strings.h"
#include "prism.h"
#include "engine_rebase.h"

namespace acc::examine_view {

namespace {

// Flat array of pre-composed strings. Recomposed on each Up/Down step
// (cheap) to keep HP and distance live without per-tick churn.

constexpr int kMaxRows = 64;   // ~10 fixed + 5 effects + 30 feats

struct State {
    bool      active        = false;
    int       focusIdx      = 0;
    int       rowCount      = 0;
    char      rows[kMaxRows][192];
    uint32_t  targetHandle  = 0;
    void*     targetObj     = nullptr;  // last resolved server obj — for refresh
};

State g_state;

typedef void* (__thiscall* PFN_GetFeat)(void* rules, unsigned short featIdx);

void* GetCSWRules() {
    __try {
        // CSWRules is at offset 0 of CSWSRules — same pointer for both.
        return *reinterpret_cast<void**>(kAddrRulesGlobal);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}


uint32_t ReadLastTargetHandle();  // forward decl

bool IsSentinel(uint32_t handle) {
    return handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u;
}

void* ReadCreatureStats(void* serverCreature) {
    if (!serverCreature) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kCreatureStatsPtrOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

int ReadHpCurrent(void* obj) {
    if (!obj) return -1;
    __try {
        return static_cast<int>(*reinterpret_cast<short*>(
            reinterpret_cast<unsigned char*>(obj) +
            kObjectHitPointsOffset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Manual-fire engine accessors. Each is deterministic, no-side-effect.
typedef int (__thiscall* PFN_GetIntThis)(void* this_);
typedef int (__thiscall* PFN_GetIntThisInt)(void* this_, int arg);

int CallIntThis(void* this_, uintptr_t addr) {
    if (!this_) return -1;
    __try {
        auto fn = reinterpret_cast<PFN_GetIntThis>(addr);
        return fn(this_);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int CallIntThisInt(void* this_, int arg, uintptr_t addr) {
    if (!this_) return -1;
    __try {
        auto fn = reinterpret_cast<PFN_GetIntThisInt>(addr);
        return fn(this_, arg);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int ReadHpMax(void* serverCreature) {
    // GetMaxHitPoints(param_1=1) — includes Toughness / class HP totals.
    int v = CallIntThisInt(serverCreature, 1,
                           kAddrCSWSCreatureGetMaxHitPoints);
    if (v < 0 || v > 0x4000) return -1;
    return v;
}

int ReadLevel(void* serverCreature) {
    void* stats = ReadCreatureStats(serverCreature);
    // GetLevel(0) — raw total class levels (no negative-level subtract).
    int v = CallIntThisInt(stats, 0,
                           kAddrCSWSCreatureStatsGetLevel);
    if (v < 0 || v > 60) return -1;
    return v;
}

int ReadDamageLevel(void* obj) {
    // Mask to the low byte: GetDamageLevel returns ulong but only AL holds the
    // 0..5 bucket; buckets 0..3 carry flag garbage in the upper bytes that
    // otherwise trips the range check (see ReadDamageLevelDirect in
    // combat_query.cpp / decompile @0x4cb020).
    int v = CallIntThis(obj, kAddrCSWSObjectGetDamageLevel) & 0xFF;
    if (v < 0 || v > 5) return -1;
    return v;
}

bool ReadDeadFlag(void* serverCreature) {
    int v = CallIntThis(serverCreature, kAddrCSWSCreatureGetDead);
    return v != 0 && v != -1;
}

// NOTE: there is intentionally no ReadInvisibleFlag here. The engine's
// CSWSCreature::GetInvisible @0x00501950 is NOT a pure read — its SANCTUARY
// branch rolls a saving throw and mutates perception state (PacifyCreature /
// AddToVisibleList), so it must not be called from this passive readout. And it
// is redundant: AppendEffectRows below already walks the effect list read-only
// and names an active INVISIBILITY effect (type 47 → "Unsichtbar"). Decompile of
// GetInvisible: param_1 is the OBSERVER; it answers "is this creature invisible
// to that observer" (Stealth/Sanctuary, gated by the observer's see-invisibility
// flags + a Will save). See git history for the full analysis.

bool ReadBlindFlag(void* serverCreature) {
    int v = CallIntThis(serverCreature, kAddrCSWSCreatureGetBlind);
    return v != 0 && v != -1;
}

acc::strings::Id DamageLevelStringId(int level) {
    using S = acc::strings::Id;
    switch (level) {
        case 0:  return S::DamageLevel0Healthy;
        case 1:  return S::DamageLevel1Light;
        case 2:  return S::DamageLevel2Wounded;
        case 3:  return S::DamageLevel3Badly;
        case 4:  return S::DamageLevel4Dying;
        case 5:  return S::DamageLevel5Dead;
        default: return S::DamageLevel0Healthy;
    }
}

int ReadFactionId(void* serverCreature) {
    void* stats = ReadCreatureStats(serverCreature);
    if (!stats) return -1;
    __try {
        unsigned short raw = *reinterpret_cast<unsigned short*>(
            reinterpret_cast<unsigned char*>(stats) +
            kStatsFactionIdOffset);
        if (raw == 0xFFFF) return -1;
        return static_cast<int>(raw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

acc::strings::Id FactionWordIdFor(int factionId) {
    using S = acc::strings::Id;
    switch (factionId) {
        case 0:   return S::FactionFriendly;       // PLAYER
        case 1:   return S::FactionHostile;        // HOSTILE_1
        case 2:   return S::FactionFriendly;       // FRIENDLY_1
        case 3:   return S::FactionHostile;        // HOSTILE_2
        case 4:   return S::FactionFriendly;       // FRIENDLY_2
        case 5:   return S::FactionNeutral;        // NEUTRAL
        case 6:   return S::FactionHostile;        // INSANE
        case 7:   return S::FactionHostile;        // PTAT_TUSKAN
        case 8:   return S::FactionHostile;        // GLB_XOR
        case 9:                                    // SURRENDER_1
        case 10:  return S::FactionNeutral;        // SURRENDER_2
        case 11:  return S::FactionHostile;        // PREDATOR
        case 12:  return S::FactionNeutral;        // PREY
        case 13:  return S::FactionHostile;        // TRAP
        case 15:  return S::FactionHostile;        // RANCOR
        case 16:                                   // GIZKA_1
        case 17:  return S::FactionNeutral;        // GIZKA_2
        default:  return S::FactionNeutral;
    }
}

int Read2DDistanceMeters(void* obj) {
    if (!obj) return -1;
    Vector tgt{};
    if (!acc::engine::GetObjectPosition(obj, tgt)) return -1;
    Vector pc{};
    if (!acc::engine::GetPlayerPosition(pc)) return -1;
    float dx = tgt.x - pc.x;
    float dy = tgt.y - pc.y;
    float d = static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
    return static_cast<int>(d + 0.5f);
}

// Read an equipped item's display name by walking the inventory ulong
// handle at the given slot offset (kInventory*HandleOffset). Same path
// combat_query::ReadEquippedItemName uses — duplicated to avoid a
// header cycle between examine_view and combat_query.
bool ReadEquippedItemNameAtSlot(void* serverCreature, size_t slotOffset,
                                char* outBuf, size_t outBufSize) {
    if (!serverCreature || !outBuf || outBufSize < 2) return false;
    outBuf[0] = '\0';
    void* inventory = nullptr;
    __try {
        inventory = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kCreatureInventoryOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!inventory) return false;

    uint32_t handle = 0;
    __try {
        handle = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(inventory) + slotOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (IsSentinel(handle)) return false;

    return acc::engine::GetObjectDisplayNameByHandle(
        handle, outBuf, outBufSize);
}

// Walk CSWSObject.effects (CExoArrayList<CGameEffect*> @+0x124) and emit
// rows for up to `cap` effects. Returns the number of rows actually
// written (could be < count if buffer is full).
int AppendEffectRows(void* serverObject, char rows[][192],
                     int& outIdx, int rowCap) {
    if (!serverObject) return 0;
    int wrote = 0;
    __try {
        auto* lst = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(serverObject) +
            kObjectEffectsOffset);
        if (!lst || !lst->data || lst->size <= 0) return 0;
        int n = lst->size > 64 ? 64 : lst->size;
        for (int i = 0; i < n && outIdx < rowCap; ++i) {
            void* eff = lst->data[i];
            if (!eff) continue;
            int type = static_cast<int>(*reinterpret_cast<unsigned short*>(
                reinterpret_cast<unsigned char*>(eff) +
                kGameEffectTypeOffset));
            const char* name = EffectName(type);
            if (name) {
                std::snprintf(rows[outIdx], sizeof(rows[0]),
                              acc::strings::Get(
                                  acc::strings::Id::FmtExamineRowEffect),
                              name);
            } else {
                std::snprintf(rows[outIdx], sizeof(rows[0]),
                              acc::strings::Get(
                                  acc::strings::Id::FmtExamineRowEffectUnknown),
                              type);
            }
            ++outIdx;
            ++wrote;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Silent — partial data is fine.
    }
    return wrote;
}

// Walk CSWSCreatureStats.feats (CExoArrayList<ushort> @+0x0) and emit
// rows for up to `cap` feats with localized names from CSWRules::GetFeat.
int AppendFeatRows(void* serverCreature, char rows[][192],
                   int& outIdx, int rowCap) {
    void* stats = ReadCreatureStats(serverCreature);
    if (!stats) return 0;
    int wrote = 0;
    __try {
        auto* lst = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(stats) +
            kStatsFeatsListOffset);
        if (!lst || !lst->data || lst->size <= 0) return 0;
        int n = lst->size > 0x100 ? 0x100 : lst->size;
        auto* ids = reinterpret_cast<unsigned short*>(lst->data);
        for (int i = 0; i < n && outIdx < rowCap; ++i) {
            unsigned short featId = ids[i];
            char nameBuf[128] = "";
            if (ResolveFeatName(featId, nameBuf, sizeof(nameBuf)) &&
                nameBuf[0] != '\0') {
                std::snprintf(rows[outIdx], sizeof(rows[0]),
                              acc::strings::Get(
                                  acc::strings::Id::FmtExamineRowFeat),
                              nameBuf);
            } else {
                std::snprintf(rows[outIdx], sizeof(rows[0]),
                              acc::strings::Get(
                                  acc::strings::Id::FmtExamineRowFeatUnknown),
                              (int)featId);
            }
            ++outIdx;
            ++wrote;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Partial is fine.
    }
    return wrote;
}

// Reuse the same LastTarget read path combat_query uses. Local copy to
// avoid header cycle with combat_query.
typedef uint32_t (__thiscall* PFN_GetLastTarget)(void* this_);
const uintptr_t kAddrCClientExoAppGetLastTargetLocal = acc::addr::R(0x005EDD80);

uint32_t ReadLastTargetHandle() {
    void* exoApp = acc::engine::GetClientApp();
    if (!exoApp) return 0;
    __try {
        auto fn = reinterpret_cast<PFN_GetLastTarget>(
            kAddrCClientExoAppGetLastTargetLocal);
        return fn(exoApp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Row building.

// (Re)build the row list for the cached target. Returns row count
// (0 if target unresolvable, view should disarm).
int BuildRows() {
    using S = acc::strings::Id;

    void* obj = nullptr;
    if (!IsSentinel(g_state.targetHandle)) {
        obj = acc::engine::ResolveClientObjectHandle(g_state.targetHandle);
        if (!obj) {
            obj = acc::engine::ResolveServerObjectHandle(
                g_state.targetHandle);
        }
    }
    g_state.targetObj = obj;
    if (!obj) return 0;

    int kind = acc::engine::GetObjectKind(obj);
    int idx = 0;

    // Row: Name.
    char name[96] = "";
    if (!acc::engine::GetObjectDisplayNameByHandle(
            g_state.targetHandle, name, sizeof(name)) || name[0] == '\0') {
        acc::engine::GetObjectName(obj, name, sizeof(name));
    }
    if (name[0] == '\0') std::strncpy(name, "?", sizeof(name) - 1);
    std::snprintf(g_state.rows[idx], sizeof(g_state.rows[0]),
                  acc::strings::Get(S::FmtExamineRowName), name);
    ++idx;

    // Creature-only rows.
    bool isCreature = (kind ==
        static_cast<int>(acc::engine::GameObjectKind::Creature));

    // Small helper to append a single optional equipment-slot row.
    auto appendEquipRow = [&](size_t slotOffset, S fmtId) {
        if (idx >= kMaxRows) return;
        char item[96] = "";
        if (ReadEquippedItemNameAtSlot(obj, slotOffset, item, sizeof(item)) &&
            item[0] != '\0') {
            std::snprintf(g_state.rows[idx], sizeof(g_state.rows[0]),
                          acc::strings::Get(fmtId), item);
            ++idx;
        }
    };

    if (isCreature) {
        // Row: Faction.
        int factionId = ReadFactionId(obj);
        acc::strings::Id factionWord = FactionWordIdFor(factionId);
        std::snprintf(g_state.rows[idx], sizeof(g_state.rows[0]),
                      acc::strings::Get(S::FmtExamineRowFaction),
                      acc::strings::Get(factionWord));
        ++idx;

        // Row: Condition (damage level — visible wound state).
        if (idx < kMaxRows) {
            int dl = ReadDamageLevel(obj);
            if (dl >= 0) {
                std::snprintf(g_state.rows[idx], sizeof(g_state.rows[0]),
                              acc::strings::Get(S::FmtExamineRowCondition),
                              acc::strings::Get(DamageLevelStringId(dl)));
                ++idx;
            }
        }

        // Row: HP — full "cur of max" when GetMaxHitPoints resolves,
        // otherwise the older single-value form.
        if (idx < kMaxRows) {
            int hpCur = ReadHpCurrent(obj);
            int hpMax = ReadHpMax(obj);
            if (hpCur >= 0 && hpMax > 0) {
                std::snprintf(g_state.rows[idx], sizeof(g_state.rows[0]),
                              acc::strings::Get(S::FmtExamineRowHpFull),
                              hpCur, hpMax);
                ++idx;
            } else if (hpCur >= 0) {
                std::snprintf(g_state.rows[idx], sizeof(g_state.rows[0]),
                              acc::strings::Get(S::FmtExamineRowHp), hpCur);
                ++idx;
            }
        }

        // Row: Level.
        if (idx < kMaxRows) {
            int lvl = ReadLevel(obj);
            if (lvl > 0) {
                std::snprintf(g_state.rows[idx], sizeof(g_state.rows[0]),
                              acc::strings::Get(S::FmtExamineRowLevel), lvl);
                ++idx;
            }
        }
    }

    // Row: Distance (always shown for resolvable target).
    int dist = Read2DDistanceMeters(obj);
    if (dist >= 0 && idx < kMaxRows) {
        std::snprintf(g_state.rows[idx], sizeof(g_state.rows[0]),
                      acc::strings::Get(S::FmtExamineRowDistance), dist);
        ++idx;
    }

    if (isCreature) {
        // Status flags — only emit a row when the flag is set, so a
        // healthy normal creature doesn't get a row of "not invisible".
        // (Invisible is covered read-only by the effect list — see the note on
        // ReadBlindFlag; the engine GetInvisible call was removed.)
        if (idx < kMaxRows && ReadBlindFlag(obj)) {
            std::strncpy(g_state.rows[idx],
                         acc::strings::Get(S::ExamineRowStatusBlind),
                         sizeof(g_state.rows[0]) - 1);
            g_state.rows[idx][sizeof(g_state.rows[0]) - 1] = '\0';
            ++idx;
        }

        // Rows: Equipment — main hand, off hand, head, torso, hands.
        // Only emit rows for occupied slots (sighted players just see
        // what's worn, not a list of empty slots).
        if (idx < kMaxRows) {
            char wpn[96] = "";
            if (ReadEquippedItemNameAtSlot(obj,
                    kInventoryRightWeaponHandleOffset,
                    wpn, sizeof(wpn)) && wpn[0] != '\0') {
                std::snprintf(g_state.rows[idx], sizeof(g_state.rows[0]),
                              acc::strings::Get(S::FmtExamineRowWeapon),
                              wpn);
            } else {
                std::strncpy(g_state.rows[idx],
                             acc::strings::Get(S::ExamineRowWeaponNone),
                             sizeof(g_state.rows[0]) - 1);
                g_state.rows[idx][sizeof(g_state.rows[0]) - 1] = '\0';
            }
            ++idx;
        }
        appendEquipRow(kInventoryLeftWeaponHandleOffset, S::FmtExamineRowOffHand);
        appendEquipRow(kInventoryHeadHandleOffset,       S::FmtExamineRowHead);
        appendEquipRow(kInventoryTorsoHandleOffset,      S::FmtExamineRowTorso);
        appendEquipRow(kInventoryHandsHandleOffset,      S::FmtExamineRowHands);

        // Rows: Effects (one per active effect). Append "No effects" if
        // none — gives the user a confirmed-empty signal vs absence.
        int effectStart = idx;
        AppendEffectRows(obj, g_state.rows, idx, kMaxRows);
        if (idx == effectStart && idx < kMaxRows) {
            std::strncpy(g_state.rows[idx],
                         acc::strings::Get(S::ExamineRowNoEffects),
                         sizeof(g_state.rows[0]) - 1);
            g_state.rows[idx][sizeof(g_state.rows[0]) - 1] = '\0';
            ++idx;
        }

        // Rows: Feats.
        int featStart = idx;
        AppendFeatRows(obj, g_state.rows, idx, kMaxRows);
        if (idx == featStart && idx < kMaxRows) {
            std::strncpy(g_state.rows[idx],
                         acc::strings::Get(S::ExamineRowNoFeats),
                         sizeof(g_state.rows[0]) - 1);
            g_state.rows[idx][sizeof(g_state.rows[0]) - 1] = '\0';
            ++idx;
        }
    }

    return idx;
}

void SpeakRow(int idx) {
    if (idx < 0 || idx >= g_state.rowCount) return;
    char out[256];
    std::snprintf(out, sizeof(out),
                  acc::strings::Get(acc::strings::Id::FmtExamineRowOf),
                  g_state.rows[idx], idx + 1, g_state.rowCount);
    prism::Speak(out, /*interrupt=*/true);
}

}  // namespace

// Public — true iff serverObject is a Creature whose faction classifies as
// hostile. Reuses the same faction table that drives the examine panel's
// Faction row (ReadFactionId + FactionWordIdFor), so the classification stays
// consistent with what the user hears elsewhere. SEH-guarded via the readers.
// Companions (faction 0/2/4 → Friendly) and neutral townsfolk are excluded;
// a neutral-until-provoked enemy would also read as non-hostile until it turns.
bool IsHostileCreature(void* serverObject) {
    if (!serverObject) return false;
    if (acc::engine::GetObjectKind(serverObject) !=
        static_cast<int>(acc::engine::GameObjectKind::Creature)) {
        return false;
    }
    int faction = ReadFactionId(serverObject);
    if (faction < 0) return false;
    return FactionWordIdFor(faction) == acc::strings::Id::FactionHostile;
}

// Public — same shape as ResolveFeatName below, but for the spells
// array. Reads Rules->spells (CSWSpellArray* at +kRulesSpellsOffset),
// resolves a CSWSpell* via GetSpell(spell_id), then reads the name strref
// off the row and resolves it through the dual-game TLK path (the retired
// CSWSpell::GetSpellNameText call was a bare fetch of the same strref, but
// KOTOR 1-only — on K2 it resolved to 0 and every queued power spoke as the
// generic "Macht einsetzen"). SEH-guarded at every dereference. Used by
// combat::queue's row speech for action_type=9 (Cast Force Power) entries.
bool ResolveSpellName(int spellId, char* outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 2) return false;
    outBuf[0] = '\0';
    void* rules = GetCSWRules();
    if (!rules) return false;

    void* spellArray = nullptr;
    __try {
        spellArray = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(rules) +
            kRulesSpellsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!spellArray) return false;

    using PFN_GetSpell = void* (__thiscall*)(void* spells, int spellId);
    void* spell = nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_GetSpell>(kAddrCSWSpellArrayGetSpell);
        spell = fn(spellArray, spellId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!spell) return false;

    uint32_t nameStrRef = 0;
    __try {
        nameStrRef = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(spell) + kSpellNameStrRefOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (nameStrRef == 0 || nameStrRef == 0xffffffff) return false;
    return acc::engine::LookupTlk(nameStrRef, outBuf, outBufSize);
}

// Public — declared in examine_view.h. Lives outside the anonymous
// namespace so combat::queue (and any other future caller) can link to
// it. Internal helpers (GetCSWRules, PFN_GetFeat,
// kAddrCSWRulesGetFeat) reach through the anon-namespace using-directive
// that the enclosing acc::examine_view namespace implicitly maintains.
// SEH-guarded — feats-table walk.
bool ResolveFeatName(unsigned short featIdx, char* outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 2) return false;
    outBuf[0] = '\0';
    void* rules = GetCSWRules();
    if (!rules) return false;

    void* feat = nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_GetFeat>(kAddrCSWRulesGetFeat);
        feat = fn(rules, featIdx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!feat) return false;

    // Read the name strref off the feat row and resolve it through the
    // dual-game TLK path. This replaced the CSWFeat::GetNameText call
    // (which is all that accessor does with the same +0x8 strref) because
    // its address was KOTOR 1-only — on K2 the call resolved to 0, the SEH
    // swallowed the fault, and every queued feat spoke as the generic
    // "Talent einsetzen" (2026-08-04 K2 session). Offset is Same(0x08),
    // witnessed on K2 in the abilities OnEnterFeat twin.
    uint32_t nameStrRef = 0;
    __try {
        nameStrRef = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(feat) + kFeatNameStrRefOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (nameStrRef == 0 || nameStrRef == 0xffffffff) return false;
    return acc::engine::LookupTlk(nameStrRef, outBuf, outBufSize);
}


bool IsActive() { return g_state.active; }

void ForceDisarm(const char* reason) {
    if (!g_state.active) return;
    acclog::Write("Examine.View", "disarm reason=%s",
                  reason ? reason : "?");
    g_state.active = false;
    acc::engine::EndOverlayPause(acc::engine::OverlayPauseOwner::ExamineView);
    g_state.focusIdx = 0;
    g_state.rowCount = 0;
    g_state.targetHandle = 0;
    g_state.targetObj = nullptr;
}

bool Open() {
    uint32_t handle = ReadLastTargetHandle();
    if (IsSentinel(handle)) {
        prism::Speak(acc::strings::Get(acc::strings::Id::ExamineNoTarget),
                    /*interrupt=*/true);
        acclog::Write("Examine.View", "Open -- no target (handle=0x%08x)",
                      handle);
        return false;
    }

    g_state.targetHandle = handle;
    int n = BuildRows();
    if (n <= 0) {
        prism::Speak(acc::strings::Get(acc::strings::Id::ExamineFailed),
                    /*interrupt=*/true);
        acclog::Write("Examine.View",
                      "Open -- BuildRows returned 0 (handle=0x%08x)",
                      handle);
        g_state.targetHandle = 0;
        return false;
    }

    g_state.rowCount = n;
    g_state.focusIdx = 0;
    g_state.active = true;
    acc::engine::BeginOverlayPause(acc::engine::OverlayPauseOwner::ExamineView);

    // Pull the target name out of row 0 for the opener cue. Row 0 format
    // is "Name: <name>" so skip the prefix to get just the name; if the
    // localization changes the prefix we still fall back gracefully on
    // the whole row.
    const char* shortName = g_state.rows[0];
    const char* colon = std::strchr(shortName, ':');
    if (colon && colon[1] == ' ') shortName = colon + 2;

    char opener[256];
    std::snprintf(opener, sizeof(opener),
                  acc::strings::Get(acc::strings::Id::FmtExamineOpened),
                  shortName, n);
    prism::Speak(opener, /*interrupt=*/true);

    acclog::Write("Examine.View",
                  "ARMED handle=0x%08x rows=%d opener=[%s]",
                  handle, n, opener);

    SpeakRow(0);
    return true;
}

bool HandleInputEvent(int code, int value) {
    if (!g_state.active) return false;
    if (value == 0) return false;  // press-edge only

    switch (code) {
        case kInputNavUp:
            if (g_state.focusIdx > 0) --g_state.focusIdx;
            // Rebuild on every step so HP / distance refresh live.
            g_state.rowCount = BuildRows();
            if (g_state.rowCount == 0) {
                ForceDisarm("target-lost-on-up");
                return true;
            }
            if (g_state.focusIdx >= g_state.rowCount) {
                g_state.focusIdx = g_state.rowCount - 1;
            }
            SpeakRow(g_state.focusIdx);
            return true;
        case kInputNavDown:
            g_state.rowCount = BuildRows();
            if (g_state.rowCount == 0) {
                ForceDisarm("target-lost-on-down");
                return true;
            }
            if (g_state.focusIdx + 1 < g_state.rowCount) {
                ++g_state.focusIdx;
            }
            SpeakRow(g_state.focusIdx);
            return true;
        case kInputEnter1:
        case kInputEnter2:
            // Enter = close (read-only view; nothing to commit).
            prism::Speak(acc::strings::Get(acc::strings::Id::ExamineViewClosed),
                        /*interrupt=*/true);
            acclog::Write("Examine.View", "Enter -> close");
            ForceDisarm("enter");
            return true;
        case kInputEsc1:
        case kInputEsc2:
            prism::Speak(acc::strings::Get(acc::strings::Id::ExamineViewClosed),
                        /*interrupt=*/true);
            acclog::Write("Examine.View", "Esc -> close");
            ForceDisarm("esc");
            return true;
        default:
            return false;
    }
}

// Engine-panel open/close logger. The engine's CSWGuiExamine is a generic
void Tick() {
    if (!g_state.active) return;
    // Self-disarm if the player loses connection to the world (area
    // transition / load). Cheap probe.
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) {
        ForceDisarm("player-pos-unresolvable");
        return;
    }
}

void PollWin32Hotkey() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::ExamineOpen)) return;

    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    // Toggle: pressing Ö while the view is open closes it.
    if (g_state.active) {
        prism::Speak(acc::strings::Get(acc::strings::Id::ExamineViewClosed),
                    /*interrupt=*/true);
        acclog::Write("Examine.View", "OEM_3 (Oe) -> close (toggle)");
        ForceDisarm("toggle");
        return;
    }

    Open();
}

}  // namespace acc::examine_view
