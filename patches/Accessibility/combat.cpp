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
#include "strings.h"
#include "tolk.h"

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
    tolk::Speak(phrase, /*interrupt=*/false);
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

// ============================================================================
// Phase 4A — per-attack callout (poll-based skeleton).
//
// Reads the player creature's combat_round.attacks_list[7] each tick;
// on a 0->non-zero transition of attack_result, builds a localised
// announcement and speaks it.
// ============================================================================

namespace {

// One snapshot row per attack-data slot. We track the previous attack_result
// + base_damage + react_object so a 0->resolved transition is unambiguous,
// and a same-slot reuse for a different attack (round wraps around) doesn't
// silently miss.
//
// `initialized` discriminates "first observation since DLL load / area
// transition" from "we have a real prior snapshot". Without it, slots
// containing leftover non-zero `attack_result` from heap reuse on
// area-load false-trigger the resolved-edge announce — the cause of the
// 2026-05-09 STATUS_STACK_BUFFER_OVERRUN crash that fired on the very
// first tick after entering Endar Spire (uninitialised attacks_list[7]
// got interpreted as a deflected attack against a garbage handle, which
// corrupted the stack canary in acclog::Write down the line).
struct AttackSnap {
    bool     initialized;
    int      lastResult;
    short    lastBaseDamage;
    uint32_t lastTarget;
};

AttackSnap g_attackSnaps[kCombatAttackDataCount] = {};

// Read fields out of attacks_list[i] at base = combat_round.
bool ReadAttackData(void* combatRound, int slot, AttackSnap& out,
                    short& outBaseDamage, uint32_t& outTarget,
                    int& outResult, int& outDeflected, int& outCriticalThreat)
{
    if (!combatRound) return false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(combatRound);
        unsigned char* slotBase = base + kCombatRoundAttacksListOffset +
                                  slot * kCombatAttackDataStride;
        outResult         = *reinterpret_cast<int*>(
            slotBase + kAttackDataAttackResultOffset);
        outBaseDamage     = *reinterpret_cast<short*>(
            slotBase + kAttackDataBaseDamageOffset);
        outTarget         = *reinterpret_cast<uint32_t*>(
            slotBase + kAttackDataReactObjectOffset);
        outDeflected      = *reinterpret_cast<int*>(
            slotBase + kAttackDataAttackDeflectedOffset);
        outCriticalThreat = *reinterpret_cast<int*>(
            slotBase + kAttackDataCriticalThreatOffset);
        out.lastResult     = outResult;
        out.lastBaseDamage = outBaseDamage;
        out.lastTarget     = outTarget;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Read CSWSCreature.combat_round @+0x9c8 (CSWSCombatRound*).
void* ReadCombatRound(void* serverCreature) {
    if (!serverCreature) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kCreatureCombatRoundOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Pull HP via direct field read (CSWSObject.hit_points @+0xe0 is
// well-documented). Skeleton currently doesn't fetch max — the engine
// accessor address `GetMaxHitPoints @0x4ed310` IS in Lane's symbol
// table (verified 2026-05-10 against
// docs/llm-docs/re/k1_win_gog_swkotor.exe.xml), so this is upgrade-
// ready, but the auto-firing tick path keeps the smaller surface area
// for the initial validation pass.
void ReadCreatureHpDirect(void* creature, int& outCur, int& outMax) {
    outCur = outMax = 0;
    if (!creature) return;
    __try {
        outCur = static_cast<int>(*reinterpret_cast<short*>(
            reinterpret_cast<unsigned char*>(creature) +
            kObjectHitPointsOffset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outCur = 0;
    }
}

void SpeakAttackOutcome(int result, short damage, int deflected,
                        int criticalThreat,
                        const char* attacker, const char* target,
                        int targetHpCur) {
    using S = acc::strings::Id;
    char msg[256];
    if (result == kAttackResultMiss) {
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(S::FmtAttackMiss), attacker, target);
    } else if (deflected != 0 || result == kAttackResultDeflected) {
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(S::FmtAttackDeflected),
                      attacker, target);
    } else if (criticalThreat != 0 || result == kAttackResultCrit) {
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(S::FmtAttackCrit),
                      attacker, target, (int)damage);
    } else {
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(S::FmtAttackHit),
                      attacker, target, (int)damage);
    }
    // Speech intentionally OFF — the OnAppendToMsgBuffer hook now
    // delivers the engine's own much richer combat-log text live, with
    // attack name, full to-hit math, defense math, and damage
    // breakdown. The synthesized "X hits Y for N" line is strictly less
    // informative and would duplicate every attack. Keep the log entry
    // so the cross-check remains available in patch logs.
    acclog::Write("Combat.Attack", "result=%d dmg=%d defl=%d crit=%d "
                  "atk=[%s] tgt=[%s] tgtHp=%d -> [%s] (silent)",
                  result, (int)damage, deflected, criticalThreat,
                  attacker, target, targetHpCur, msg);
}

}  // namespace

void TickAttackResolutions() {
    // Gate 1: only walk attacks_list when the engine itself reports
    // combat-active. attacks_list[7] is an inline fixed array that holds
    // leftover heap bytes between rounds — outside combat, every "field"
    // we read is garbage. Without this gate the very first tick after
    // area-load falsely fired against uninitialised slots and walked
    // wild handles into the engine resolver chain (caused the
    // STATUS_STACK_BUFFER_OVERRUN crash on 2026-05-09).
    int combatMode = 0;
    if (!ReadCombatMode(combatMode) || combatMode == 0) {
        // Reset all slot snapshots so the first round after combat-on
        // re-adopts fresh baselines without re-firing.
        for (int s = 0; s < kCombatAttackDataCount; ++s) {
            g_attackSnaps[s].initialized = false;
        }
        return;
    }

    void* player = acc::engine::GetPlayerServerCreature();
    if (!player) return;
    void* round = ReadCombatRound(player);
    if (!round) return;

    char attackerName[64] = "";
    if (!acc::engine::GetPlayerCharacterName(attackerName,
                                             sizeof(attackerName)) ||
        attackerName[0] == '\0') {
        // Could be unnamed PC during chargen. Use a generic placeholder so
        // the speech still fires.
        std::snprintf(attackerName, sizeof(attackerName), "%s",
                      acc::strings::Get(acc::strings::Id::CategoryNpc));
    }

    for (int slot = 0; slot < kCombatAttackDataCount; ++slot) {
        short    baseDamage = 0;
        uint32_t targetHandle = 0;
        int      result = 0, deflected = 0, criticalThreat = 0;
        AttackSnap fresh{};
        if (!ReadAttackData(round, slot, fresh, baseDamage, targetHandle,
                            result, deflected, criticalThreat)) {
            continue;
        }
        AttackSnap& snap = g_attackSnaps[slot];
        fresh.initialized = true;

        // Skip unused slots — engine sentinel for "no attack here" is -1
        // (validated 2026-05-10 against attacks_list reads on game start;
        // every empty slot returned result=-1 dmg=0 defl=-1 crit=-1).
        if (result == -1) {
            snap = fresh;
            continue;
        }

        // Gate 2: first observation per slot since combat just engaged —
        // adopt as baseline silently. Otherwise a slot whose `result`
        // happens to be non-zero on the very first read fires immediately.
        if (!snap.initialized) {
            snap = fresh;
            continue;
        }

        // Edge: any change in (target, result, baseDamage) tuple where
        // the new result is a valid resolved value. The original
        // pending->resolved-only check missed every attack after the
        // first because the engine reuses the slot transitioning
        // resolved->resolved without going back through pending
        // (validated 2026-05-10 — only one Combat.Attack line for an
        // entire combat session in patch-20260510-000722.log).
        bool tupleChanged = (snap.lastTarget     != targetHandle ||
                             snap.lastResult     != result        ||
                             snap.lastBaseDamage != baseDamage);
        bool resultIsResolved = (result >= kAttackResultHit &&
                                 result <= kAttackResultDeflected);

        // Damage-settle gate. The engine sometimes publishes
        // attack_result before base_damage finishes computing — leaves
        // base_damage at the -1 sentinel for one tick (verified
        // 2026-05-10 in patch-20260510-003647.log: 4 separate hit
        // entries spoke "for -1 Schaden"). Only Hit / Crit care about
        // the damage value; for those, suppress the announce when
        // baseDamage < 0 and let the next-tick observation pick up the
        // settled value (snap.lastBaseDamage is updated to -1 below,
        // so when the engine fills in the real damage next tick, the
        // tuple-change check fires on the diff). Miss / Deflected
        // don't carry damage, so they bypass this gate.
        bool needsDamage = (result == kAttackResultHit ||
                            result == kAttackResultCrit);
        bool damageSettled = !needsDamage || baseDamage >= 0;
        if (tupleChanged && resultIsResolved && damageSettled) {
            // Gate 3: validate the target before speaking. The handle has
            // to resolve to an actual server object AND name out cleanly
            // — otherwise we're reading a stale slot, not a real attack.
            void* tgt = acc::engine::ResolveServerObjectHandle(targetHandle);
            if (!tgt) {
                snap = fresh;
                continue;
            }
            char tgtName[64] = "";
            // Try localized display name via the engine's universal
            // accessor first; fall back to the offset-based reader.
            if (!acc::engine::GetObjectDisplayNameByHandle(
                    targetHandle, tgtName, sizeof(tgtName)) ||
                tgtName[0] == '\0') {
                if (!acc::engine::GetObjectName(tgt, tgtName,
                                                sizeof(tgtName)) ||
                    tgtName[0] == '\0') {
                    snap = fresh;
                    continue;
                }
            }
            int tgtHpCur = 0, tgtHpMax = 0;
            ReadCreatureHpDirect(tgt, tgtHpCur, tgtHpMax);
            SpeakAttackOutcome(result, baseDamage, deflected, criticalThreat,
                               attackerName, tgtName, tgtHpCur);
        }
        snap = fresh;
    }
}

// ============================================================================
// Phase 4B — saving-throw poll skeleton.
//
// We don't have a clean event for "save just rolled" without a hook, so
// this skeleton tracks creature_stats save fields per tick and announces
// when one drops or rises by a noticeable margin (likely a buff/debuff
// resolution from a save).
//
// Real implementation needs hooks at SavingThrowRoll @0x5b92b0 or
// BroadcastSavingThrowData @0x4ec760 for roll/DC values. Marked TODO.
// ============================================================================

void TickSavingThrows() {
    // Skeleton no-op: the real signal needs a hook. Polling for save
    // changes per-tick is too noisy to be useful (every effect cleanup
    // / equip / level-up touches the totals).
    //
    // Once a hook on SavingThrowRoll lands, this Tick is unused — the
    // hook handler speaks directly via FmtSavingThrowSucceeded /
    // FmtSavingThrowFailed.
    return;
}

}  // namespace acc::combat

// ============================================================================
// CGuiInGame::AppendToMsgBuffer @0x0062b5c0 — live combat-log row append.
//
// SARIF xref analysis showed CSWGuiInGameMessages::AddMessages has only
// one caller (ShowDialogEntry, dialog path). patch-20260521-095251.log
// confirmed: hooking AddMessages produced zero fires during a live fight
// even though 64 rows landed in messages_listbox at review-screen open.
// AppendToMsgBuffer is the actual live-combat surface — it writes each
// engine-emitted feedback string into a 64-slot, 16-byte-stride ring
// buffer at this[+0xF8], write-index at this[+0x100]. The Messages
// review panel rebuilds messages_listbox from this ring on mount.
//
// Function signature: `void __thiscall AppendToMsgBuffer(CExoString* msg)`.
// We hook at function entry @0x0062b5c0 with a 7-byte cut covering
// PUSH EDI + MOV EDI,ECX + MOV ECX,[ESP+8]. All register/memory-relative
// — position-independent. At hook entry:
//
//   ECX     = this (CGuiInGame*)
//   [ESP+4] = param_1 (CExoString*) — the row text to append
//
// `source = "esp+4"` emits LEA per project_kpatchmanager_lea_bug.md, so
// the handler receives the *address* of the stack slot and dereferences
// once to get the CExoString*.
extern "C" void __cdecl OnAppendToMsgBuffer(void* /*guiInGame*/,
                                            void* esp_param1_addr) {
    CExoString* exoStr = nullptr;
    __try {
        if (esp_param1_addr) {
            exoStr = *reinterpret_cast<CExoString**>(esp_param1_addr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    if (!exoStr) {
        acclog::Write("Combat.MsgBuf", "fire: param=null");
        return;
    }

    const char* cstr   = nullptr;
    uint32_t    length = 0;
    __try {
        cstr   = exoStr->c_string;
        length = exoStr->length;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    if (!cstr || length == 0) {
        acclog::Write("Combat.MsgBuf", "fire: empty (cstr=%p len=%u)",
                      cstr, length);
        return;
    }

    // Cap to a sane upper bound — engine combat-log rows are typically
    // under 200 chars. 480 leaves headroom for the longest "Schadens-
    // statistik" lines and stays within typical TTS sentence buffers.
    char text[512];
    size_t n = length < sizeof(text) - 1 ? length : sizeof(text) - 1;
    __try {
        memcpy(text, cstr, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Combat.MsgBuf", "fire: copy fault cstr=%p len=%u",
                      cstr, length);
        return;
    }
    text[n] = '\0';

    tolk::Speak(text, /*interrupt=*/false);
    acclog::Write("Combat.MsgBuf", "fire: [%.300s]", text);
}
