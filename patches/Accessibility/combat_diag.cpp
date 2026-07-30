#include "combat_diag.h"
#include "combat_diag_internal.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "combat_queue.h"    // OnEngineActionAdded — authoritative queue announce
#include "engine_app.h"      // GetClientApp
#include "engine_offsets.h"
#include "engine_panels.h"   // ResolveMainInterface
#include "engine_player.h"   // GetPlayerServerCreature, GetClientLeader
#include "log.h"
#include "narrated_target.h"
#include "engine_rebase.h"

namespace acc::combat_diag {

namespace {

// CSWCCreature combat-mode bit (set by CSWCCreature::SetCombatMode @0x00610a10).
// Bit 0 of field200_0x440 is the chain/overwrite knob DoPersonalAction +
// DoTargetAction branch on.
constexpr size_t kCSWCCreatureCombatModeOffset = 0x440;

// Engine accessors used by DoPersonalAction's chain branch.
const uintptr_t kAddrCClientExoAppGetAutoPaused = acc::addr::R(0x005edef0);
const uintptr_t kAddrCClientExoAppGetPauseState = acc::addr::R(0x005ed640);

// main_interface.field1_0x64 — the engine target handle SetTarget stamps.
// Resolved through engine_panels' ResolveMainInterface so we can compare
// per-press.
constexpr size_t kMainInterfaceTargetHandleOff = 0x64;

typedef unsigned long (__thiscall* PFN_GetAutoPaused)(void* this_);
typedef unsigned long (__thiscall* PFN_GetPauseState)(void* this_, uint8_t which);

uint8_t ReadCombatModeBit(void* clientLeader) {
    if (!clientLeader) return 0xff;
    __try {
        uint8_t v = *(reinterpret_cast<uint8_t*>(clientLeader) +
                      kCSWCCreatureCombatModeOffset);
        return static_cast<uint8_t>(v & 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xff;
    }
}

// Queued-action count on a CSWSCombatRound directly. 0 means "nothing
// queued" — including the case where the round has no action list at all,
// which is the engine's own TEST EAX,EAX / JZ bail condition at the top of
// RemoveAllActions. -1 means "couldn't tell" (null round, or a faulted read).
//
// Callers that need to distinguish "empty" from "unknown" must check for -1;
// the RemoveAllActions hook relies on exactly that, so do not collapse the
// two.
int ReadRoundActionCount(void* combatRound) {
    if (!combatRound) return -1;
    __try {
        void* listPtr = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(combatRound) +
            kCombatRoundActionsOffset);
        if (!listPtr) return 0;
        // Fast path — read internal.count directly (engine's own size
        // field). Avoids the walker entirely. Walk path stays as a
        // backup for filtered-count semantics.
        void* internalPtr = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(listPtr) +
            kListInternalOffset);
        if (!internalPtr) return 0;
        int rawCount = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(internalPtr) +
            kListInternalCountOffset);
        if (rawCount < 0 || rawCount > 64) return -1;  // sanity
        return rawCount;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int ReadQueueSize(void* serverCreature) {
    if (!serverCreature) return -1;
    __try {
        void* combatRound = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kCreatureCombatRoundOffset);
        return ReadRoundActionCount(combatRound);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Read the OUTER game-action queue size at CSWSObject.action_nodes
// @+0xfc. The list is INLINE on CSWSObject (CExoLinkedList<T>, not a
// pointer), so the first deref yields the CExoLinkedListInternal*
// directly; +8 on that gives the engine-authoritative count field.
//
// This is the queue the strip UI iterates first (UpdateActionQueue
// @0x68a010). Returns the raw count (no type filter — outer nodes
// have action_type at +0 of CSWSObjectActionNode, not the dispatcher
// placeholder convention the inner queue uses). -1 on read fault.
int ReadOuterQueueSize(void* serverCreature) {
    if (!serverCreature) return -1;
    __try {
        constexpr size_t kObjectActionNodesOffset = 0xfc;
        // The inline CExoLinkedList<T> at +0xfc starts with the
        // internal pointer (offset 0 = kListInternalOffset).
        void* internalPtr = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kObjectActionNodesOffset);
        if (!internalPtr) return 0;
        int rawCount = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(internalPtr) +
            kListInternalCountOffset);
        if (rawCount < 0 || rawCount > 64) return -1;
        return rawCount;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int CallGetAutoPaused(void* exoApp) {
    if (!exoApp) return -1;
    __try {
        return static_cast<int>(
            reinterpret_cast<PFN_GetAutoPaused>(
                kAddrCClientExoAppGetAutoPaused)(exoApp));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int CallGetPauseState(void* exoApp) {
    if (!exoApp) return -1;
    __try {
        return static_cast<int>(
            reinterpret_cast<PFN_GetPauseState>(
                kAddrCClientExoAppGetPauseState)(exoApp, 0));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

uint32_t ReadMainInterfaceTarget(void* exoApp) {
    if (!exoApp) return 0;
    void* mi = acc::engine::ResolveMainInterface();
    if (!mi) return 0;
    __try {
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(mi) +
            kMainInterfaceTargetHandleOff);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

struct State {
    uint8_t  cm;          // combat-mode bit (0/1, 0xff on fault)
    int      qs;          // INNER queue size (combat_round.actions)
    int      oq;          // OUTER queue size (object.action_nodes) —
                          // where chained presses actually stack and
                          // what the sighted strip UI reflects
    int      ap;          // auto-paused
    int      ps;          // pause-state(0)
    uint32_t tgt;         // main_interface target handle
};

void ReadState(State& s) {
    s = {};
    s.cm = 0xff;
    s.qs = -1;
    s.oq = -1;
    s.ap = -1;
    s.ps = -1;
    s.tgt = 0;

    void* server = acc::engine::GetPlayerServerCreature();
    void* client = acc::engine::GetClientLeader();
    void* exoApp = acc::engine::GetClientApp();

    s.cm  = ReadCombatModeBit(client);
    s.qs  = ReadQueueSize(server);
    s.oq  = ReadOuterQueueSize(server);
    s.ap  = CallGetAutoPaused(exoApp);
    s.ps  = CallGetPauseState(exoApp);
    s.tgt = ReadMainInterfaceTarget(exoApp);
}

State g_last  = {0xff, -1, -1, -1, -1, 0};
bool  g_armed = false;

}  // namespace

void Tick() {
    // Player-loaded gate. Same pattern combat.cpp uses — no point reading
    // engine state mid-load.
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    State now;
    ReadState(now);

    if (!g_armed) {
        g_last  = now;
        g_armed = true;
        acclog::Write("Combat.Diag",
            "INIT cm=%u qs=%d oq=%d ap=%d ps=%d tgt=0x%08x",
            now.cm, now.qs, now.oq, now.ap, now.ps, now.tgt);
        return;
    }

    if (now.cm != g_last.cm) {
        acclog::Write("Combat.Diag", "DELTA combat_mode %u -> %u",
                      g_last.cm, now.cm);
    }
    if (now.qs != g_last.qs) {
        acclog::Write("Combat.Diag", "DELTA queue_size %d -> %d",
                      g_last.qs, now.qs);
    }
    if (now.oq != g_last.oq) {
        acclog::Write("Combat.Diag", "DELTA outer_queue %d -> %d",
                      g_last.oq, now.oq);
    }
    if (now.ap != g_last.ap) {
        acclog::Write("Combat.Diag", "DELTA autopause %d -> %d",
                      g_last.ap, now.ap);
    }
    if (now.ps != g_last.ps) {
        acclog::Write("Combat.Diag", "DELTA pause_state %d -> %d",
                      g_last.ps, now.ps);
    }
    if (now.tgt != g_last.tgt) {
        acclog::Write("Combat.Diag", "DELTA target 0x%08x -> 0x%08x",
                      g_last.tgt, now.tgt);
    }
    g_last = now;
}

void LogPreFire(const char* label) {
    State s;
    ReadState(s);
    acclog::Write("Combat.Diag",
        "PRE  %s cm=%u qs=%d oq=%d ap=%d ps=%d tgt=0x%08x",
        label ? label : "?", s.cm, s.qs, s.oq, s.ap, s.ps, s.tgt);
}

void LogPostFire(const char* label) {
    State s;
    ReadState(s);
    acclog::Write("Combat.Diag",
        "POST %s cm=%u qs=%d oq=%d ap=%d ps=%d tgt=0x%08x",
        label ? label : "?", s.cm, s.qs, s.oq, s.ap, s.ps, s.tgt);
}

namespace {

// Resolve the player's CSWSCombatRound at hook-fire time so we can tag
// each ADD/CLEAR event with whether it belongs to the user's creature
// (vs companion / enemy combat rounds, which also use the same engine
// surfaces). Cheap — the chain is already cached by GetPlayerServerCreature.
void* GetPlayerCombatRound() {
    void* server = acc::engine::GetPlayerServerCreature();
    if (!server) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(server) +
            kCreatureCombatRoundOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

// Declared in combat_diag_internal.h - combat_queue_hooks.cpp tags its log
// lines with the same role, so this one helper is not file-static.
const char* RoleTag(void* combatRound) {
    return combatRound == GetPlayerCombatRound() ? "PLAYER" : "other";
}

}  // namespace acc::combat_diag


extern "C" void __cdecl OnCombatRoundRemoveAllActions(void* this_combatRound) {
    // Skip no-op clears. The engine's very next instruction after our cut is
    // TEST EAX,EAX / JZ — a clear against a round with no queued actions
    // removes nothing and returns immediately. This hook exists to answer
    // "did the engine silently cull queued entries?", and a clear that culled
    // nothing never answers it.
    //
    // The one thing this does give up: a clear dispatched against an already-
    // empty queue is no longer visible, so "the engine took the overwrite
    // path from an empty queue" now looks the same as "it chained". That
    // distinction has no observable effect (there was nothing to overwrite)
    // and the following ADD line still records the outcome.
    //
    // It matters because the engine re-clears every live combat round once
    // per frame while combat is being torn down for a cutscene handover. On
    // the run-up to the turret sequence that was six rounds a frame for seven
    // seconds — 2542 log lines at ~360/s, each one a formatted write plus an
    // fflush under a lock on the game thread (patch-20260729-085456.log,
    // 08:58:00-08:58:07). Everywhere else in that two-hour session the same
    // line fired 1-3 times a second.
    //
    // -1 is "couldn't read the count", not "empty" — log those, or a genuine
    // clear disappears whenever the read faults.
    int actions = acc::combat_diag::ReadRoundActionCount(this_combatRound);
    if (actions == 0) return;

    acclog::Write("Combat.Diag",
        "CLEAR [%s] round=%p actions=%d",
        acc::combat_diag::RoleTag(this_combatRound),
        this_combatRound, actions);
}

extern "C" void __cdecl OnCombatRoundSetCurrentAction(void* this_combatRound,
                                                     void* esp_byte_addr) {
    uint8_t byte_param = 0xff;
    __try {
        if (esp_byte_addr) {
            byte_param = *reinterpret_cast<uint8_t*>(esp_byte_addr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    acclog::Write("Combat.Diag",
        "SETCUR [%s] round=%p byte=%u",
        acc::combat_diag::RoleTag(this_combatRound),
        this_combatRound, static_cast<unsigned>(byte_param));
}

extern "C" void __cdecl OnCombatRoundRemoveLastAction(void* this_combatRound) {
    acclog::Write("Combat.Diag",
        "REMLAST [%s] round=%p",
        acc::combat_diag::RoleTag(this_combatRound),
        this_combatRound);
}
