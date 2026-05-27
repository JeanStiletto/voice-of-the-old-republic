#include "examine_view.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"
#include "engine_input.h"
#include "engine_manager.h"   // kAddrGuiManagerPtr — engine-panel watcher
#include "engine_offsets.h"
#include "engine_panels.h"    // PanelKind / IdentifyPanel — engine-panel watcher
#include "engine_player.h"
#include "hotkeys.h"
#include "log.h"
#include "strings.h"
#include "prism.h"
#include "transitions.h"      // IsModuleLoadPending — engine-panel watcher gate

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
typedef void* (__thiscall* PFN_GetFeatNameText)(void* feat, void* outExoString);

// CExoString layout (matches CExoLocString byte-wise for our use):
//   +0x0 char*  c_string
//   +0x4 uint32 length
struct ExoStringRaw {
    char*    c_string;
    uint32_t length;
};

void* GetCSWRules() {
    __try {
        // CSWRules is at offset 0 of CSWSRules — same pointer for both.
        return *reinterpret_cast<void**>(kAddrRulesGlobal);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// True on non-empty result. SEH-guarded — feats-table walk.
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

    ExoStringRaw exo{nullptr, 0};
    __try {
        auto fn = reinterpret_cast<PFN_GetFeatNameText>(
            kAddrCSWFeatGetNameText);
        fn(feat, &exo);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!exo.c_string || exo.c_string[0] == '\0') {
        // Leak the heap alloc — destruction across the DLL/EXE boundary
        // risks a CRT mismatch on ~CExoString.
        return false;
    }
    std::strncpy(outBuf, exo.c_string, outBufSize - 1);
    outBuf[outBufSize - 1] = '\0';
    return true;
}

// EFFECT_TYPES enum (110 entries) → display name. Unmapped types fall
// back to "Effect #N" in the caller. Localized inline rather than burning
// a strings.h ID per enum entry.

const char* EffectNameEn(int type) {
    switch (type) {
        case 1:   return "Haste";
        case 2:   return "Damage resistance";
        case 3:   return "Slow";
        case 5:   return "Disease";
        case 7:   return "Regeneration";
        case 8:   return "State";
        case 10:  return "Attack bonus";
        case 11:  return "Attack penalty";
        case 12:  return "Damage reduction";
        case 13:  return "Damage bonus";
        case 14:  return "Damage penalty";
        case 15:  return "Temporary hit points";
        case 16:  return "Damage immunity";
        case 17:  return "Damage vulnerability";
        case 18:  return "Entangled";
        case 19:  return "Death";
        case 20:  return "Knocked down";
        case 21:  return "Deaf";
        case 22:  return "Immunity";
        case 25:  return "Arcane spell failure";
        case 26:  return "Saving-throw bonus";
        case 27:  return "Saving-throw penalty";
        case 28:  return "Movement speed bonus";
        case 29:  return "Movement speed penalty";
        case 33:  return "Force resistance bonus";
        case 34:  return "Force resistance penalty";
        case 35:  return "Poisoned";
        case 36:  return "Ability bonus";
        case 37:  return "Ability penalty";
        case 38:  return "Damage";
        case 39:  return "Heal";
        case 43:  return "Droid stunned";
        case 44:  return "Modify number of attacks";
        case 45:  return "Cursed";
        case 46:  return "Silenced";
        case 47:  return "Invisible";
        case 48:  return "Armor class bonus";
        case 49:  return "Armor class penalty";
        case 50:  return "Spell immunity";
        case 53:  return "Taunted";
        case 55:  return "Skill bonus";
        case 56:  return "Skill penalty";
        case 60:  return "Force push";
        case 61:  return "Damage shield";
        case 62:  return "Disguised";
        case 63:  return "Sanctuary";
        case 64:  return "Time stop";
        case 73:  return "Blinded";
        case 75:  return "Miss chance";
        case 76:  return "Concealment";
        case 82:  return "Negative level";
        case 84:  return "Wounding";
        case 87:  return "Disarmed";
        case 90:  return "Force drain";
        case 91:  return "Temporary force points";
        case 92:  return "Blaster deflection bonus";
        case 93:  return "Blaster deflection penalty";
        case 94:  return "Horrified";
        case 95:  return "Force point damage";
        case 96:  return "Force point heal";
        case 97:  return "Choked";
        case 99:  return "Psychic static";
        case 100: return "Lightsaber throw";
        case 101: return "Assured hit";
        case 104: return "Assured deflection";
        case 107: return "Force shield";
        case 108: return "Pure good powers";
        case 109: return "Pure evil powers";
        default:  return nullptr;
    }
}

const char* EffectNameDe(int type) {
    switch (type) {
        case 1:   return "Eile";
        case 2:   return "Schadensresistenz";
        case 3:   return "Verlangsamung";
        case 5:   return "Krankheit";
        case 7:   return "Regeneration";
        case 8:   return "Zustand";
        case 10:  return "Angriffsbonus";
        case 11:  return "Angriffsmalus";
        case 12:  return "Schadensreduktion";
        case 13:  return "Schadensbonus";
        case 14:  return "Schadensmalus";
        case 15:  return "Tempor\xE4re Lebenspunkte";
        case 16:  return "Schadensimmunit\xE4t";
        case 17:  return "Schadensanf\xE4lligkeit";
        case 18:  return "Verwickelt";
        case 19:  return "Tod";
        case 20:  return "Niedergeschlagen";
        case 21:  return "Taub";
        case 22:  return "Immunit\xE4t";
        case 25:  return "Machtversagen";
        case 26:  return "Rettungswurfbonus";
        case 27:  return "Rettungswurfmalus";
        case 28:  return "Tempobonus";
        case 29:  return "Tempomalus";
        case 33:  return "Machtresistenzbonus";
        case 34:  return "Machtresistenzmalus";
        case 35:  return "Vergiftet";
        case 36:  return "Attributsbonus";
        case 37:  return "Attributsmalus";
        case 38:  return "Schaden";
        case 39:  return "Heilung";
        case 43:  return "Droid bet\xE4ubt";
        case 44:  return "Anzahl Angriffe ge\xE4ndert";
        case 45:  return "Verflucht";
        case 46:  return "Verstummt";
        case 47:  return "Unsichtbar";
        case 48:  return "R\xFCstungsbonus";
        case 49:  return "R\xFCstungsmalus";
        case 50:  return "Machtimmunit\xE4t";
        case 53:  return "Verspottet";
        case 55:  return "Talentbonus";
        case 56:  return "Talentmalus";
        case 60:  return "Machtsto\xDF";
        case 61:  return "Schadensschild";
        case 62:  return "Verkleidet";
        case 63:  return "Heiligtum";
        case 64:  return "Zeitstopp";
        case 73:  return "Blind";
        case 75:  return "Verfehlchance";
        case 76:  return "Tarnung";
        case 82:  return "Negative Stufe";
        case 84:  return "Verwundend";
        case 87:  return "Entwaffnet";
        case 90:  return "Machtentzug";
        case 91:  return "Tempor\xE4re Machtpunkte";
        case 92:  return "Blasterabwehrbonus";
        case 93:  return "Blasterabwehrmalus";
        case 94:  return "Entsetzt";
        case 95:  return "Machtpunktschaden";
        case 96:  return "Machtpunktheilung";
        case 97:  return "Erstickt";
        case 99:  return "Psychische Statik";
        case 100: return "Laserschwert werfen";
        case 101: return "Sicherer Treffer";
        case 104: return "Sichere Abwehr";
        case 107: return "Machtschild";
        case 108: return "Reine gute Mächte";
        case 109: return "Reine böse Mächte";
        default:  return nullptr;
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
    int v = CallIntThis(obj, kAddrCSWSObjectGetDamageLevel);
    if (v < 0 || v > 5) return -1;
    return v;
}

bool ReadDeadFlag(void* serverCreature) {
    int v = CallIntThis(serverCreature, kAddrCSWSCreatureGetDead);
    return v != 0 && v != -1;
}

bool ReadInvisibleFlag(void* serverCreature) {
    int v = CallIntThis(serverCreature, kAddrCSWSCreatureGetInvisible);
    return v != 0 && v != -1;
}

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
constexpr uintptr_t kAddrCClientExoAppGetLastTargetLocal = 0x005EDD80;

void* GetClientExoAppLocal() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

uint32_t ReadLastTargetHandle() {
    void* exoApp = GetClientExoAppLocal();
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
        if (idx < kMaxRows && ReadInvisibleFlag(obj)) {
            std::strncpy(g_state.rows[idx],
                         acc::strings::Get(S::ExamineRowStatusInvisible),
                         sizeof(g_state.rows[0]) - 1);
            g_state.rows[idx][sizeof(g_state.rows[0]) - 1] = '\0';
            ++idx;
        }
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

const char* EffectName(int type) {
    if (acc::strings::GetLanguage() == acc::strings::Lang::De) {
        return EffectNameDe(type);
    }
    return EffectNameEn(type);
}

bool IsActive() { return g_state.active; }

void ForceDisarm(const char* reason) {
    if (!g_state.active) return;
    acclog::Write("Examine.View", "disarm reason=%s",
                  reason ? reason : "?");
    g_state.active = false;
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
// TLK message-box panel — not the synthetic in-DLL list view above. Speech
// for the engine panel is owned by menus_listbox::kExamineSpec (rows on
// Up/Down) and HotkeyShiftH-equivalents at press time; this watcher just
// emits open/close edges + a row-count diagnostic so the panel lifecycle
// is visible in patch.log.
//
// Runs unconditionally — independent of the synthetic-view active gate
// below; the engine panel may open from the radial menu without us
// arming the in-DLL view.
void TickEnginePanelLifecycle() {
    // Module-load latch — examine reads engine accessors on the focused
    // server-object; the cutscene-load transient can leave that handle
    // stale.
    if (acc::transitions::IsModuleLoadPending()) return;
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return;

    void* examinePanel = nullptr;
    int n = panelCount > 16 ? 16 : panelCount;
    for (int i = 0; i < n; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        if (acc::engine::IdentifyPanel(p) ==
            acc::engine::PanelKind::Examine) {
            examinePanel = p;
            break;
        }
    }

    static void* s_lastPanel = nullptr;
    if (!examinePanel) {
        if (s_lastPanel) {
            acclog::Write("Examine.Panel", "closed");
            s_lastPanel = nullptr;
        }
        return;
    }
    if (examinePanel == s_lastPanel) return;
    s_lastPanel = examinePanel;

    // Row count is logged once for diagnostics — useful to confirm the
    // engine populated the listbox (rowCount > 0) vs left it empty
    // (which would mean vtable[27] didn't fire as expected).
    int rowCount = -1;
    __try {
        void* lb = reinterpret_cast<unsigned char*>(examinePanel) +
                   kExaminePanelListBoxOffset;
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(lb) +
            kListBoxControlsOffset);
        rowCount = (lbList && lbList->data) ? lbList->size : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rowCount = -1;
    }
    acclog::Write("Examine.Panel",
                  "opened panel=%p rows=%d (speech via menus_listbox "
                  "kExamineSpec)",
                  examinePanel, rowCount);
}

void Tick() {
    TickEnginePanelLifecycle();

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

    // Toggle: pressing Shift+H while the view is open closes it.
    if (g_state.active) {
        prism::Speak(acc::strings::Get(acc::strings::Id::ExamineViewClosed),
                    /*interrupt=*/true);
        acclog::Write("Examine.View", "Shift+H -> close (toggle)");
        ForceDisarm("toggle");
        return;
    }

    Open();
}

}  // namespace acc::examine_view
