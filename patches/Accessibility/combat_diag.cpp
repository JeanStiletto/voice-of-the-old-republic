#include "combat_diag.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "combat_queue.h"    // OnEngineActionAdded — authoritative queue announce
#include "engine_offsets.h"
#include "engine_player.h"   // GetPlayerServerCreature, GetClientLeader,
                             // kAddrAppManagerPtr, kAppManagerClientAppOffset
#include "log.h"
#include "narrated_target.h"

namespace acc::combat_diag {

namespace {

// CSWCCreature combat-mode bit (set by CSWCCreature::SetCombatMode @0x00610a10).
// Bit 0 of field200_0x440 is the chain/overwrite knob DoPersonalAction +
// DoTargetAction branch on.
constexpr size_t kCSWCCreatureCombatModeOffset = 0x440;

// Engine accessors used by DoPersonalAction's chain branch.
constexpr uintptr_t kAddrCClientExoAppGetAutoPaused = 0x005edef0;
constexpr uintptr_t kAddrCClientExoAppGetPauseState = 0x005ed640;

// main_interface.field1_0x64 — the engine target handle SetTarget stamps.
// Resolved through the standard chain so we can compare per-press.
constexpr size_t kGuiInGameMainInterfaceOffset = 0x90;
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

int ReadQueueSize(void* serverCreature) {
    if (!serverCreature) return -1;
    __try {
        void* combatRound = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kCreatureCombatRoundOffset);
        if (!combatRound) return -1;
        void* listPtr = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(combatRound) +
            kCombatRoundActionsOffset);
        if (!listPtr) return -1;
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

void* GetExoApp() {
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
    __try {
        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(exoApp) +
            kClientExoAppInternalOffset);
        if (!internal) return 0;
        void* guiInGame = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) + 0x040);
        if (!guiInGame) return 0;
        void* mi = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(guiInGame) +
            kGuiInGameMainInterfaceOffset);
        if (!mi) return 0;
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
    void* exoApp = GetExoApp();

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

const char* RoleTag(void* combatRound) {
    return combatRound == GetPlayerCombatRound() ? "PLAYER" : "other";
}

}  // namespace

}  // namespace acc::combat_diag

// ============================================================================
// Detour entry points — wired via hooks.toml + exports.def.
// ============================================================================

extern "C" void __cdecl OnCombatRoundAddAction(void* this_combatRound,
                                               void* esp_action_addr,
                                               void* esp_param2_addr) {
    // Deref the stack slots per project_kpatchmanager_lea_bug.md — `source =
    // "esp+N"` emits LEA so the handler receives the *address* of the slot.
    void* action = nullptr;
    int   param2 = 0;
    __try {
        if (esp_action_addr) {
            action = *reinterpret_cast<void**>(esp_action_addr);
        }
        if (esp_param2_addr) {
            param2 = *reinterpret_cast<int*>(esp_param2_addr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    // Read the action_type byte for context. CSWSCombatRoundAction.action_type
    // is at +0x10 (engine_offsets kCombatRoundActionTypeOffset). Reuse the
    // verbs table from combat_queue: 1=attack, 6=equip, 7=unequip,
    // 9=cast force, 10=item-cast, 11=use feat.
    uint8_t action_type = 0xff;
    if (action) {
        __try {
            action_type = *(reinterpret_cast<uint8_t*>(action) + 0x10);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    acclog::Write("Combat.Diag",
        "ADD   [%s] round=%p action=%p type=%u param2=%d",
        acc::combat_diag::RoleTag(this_combatRound),
        this_combatRound, action,
        static_cast<unsigned>(action_type), param2);

    // Authoritative "X, Platz N" / "Warteschlange voll" announce. This hook
    // fires once per genuine add at function entry, so it never under-counts
    // on key auto-repeat or races the queue drain the way the old rising-edge
    // poll did. OnEngineActionAdded self-filters to the controlled creature's
    // round.
    acc::combat::queue::OnEngineActionAdded(this_combatRound, action);
}

extern "C" void __cdecl OnCombatRoundRemoveAllActions(void* this_combatRound) {
    acclog::Write("Combat.Diag",
        "CLEAR [%s] round=%p",
        acc::combat_diag::RoleTag(this_combatRound),
        this_combatRound);
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
