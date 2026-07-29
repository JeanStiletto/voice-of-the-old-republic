// CSWSCombatRound::AddAction detour - the combat-queue announce trigger.
//
// Split out of combat_diag.cpp by the Phase-1 structure pass (refactoring
// candidate 14). This hook is NOT a diagnostic despite having lived in a
// file named diag: it is the mechanism behind the shipped queue announce
// ("X, Platz N" / "Warteschlange voll"), forwarding every genuine engine
// action-add to acc::combat::queue::OnEngineActionAdded. It sits next to
// combat_queue.cpp so that relationship is visible from the filename.
//
// The three CSWSCombatRound hooks that really are log-only
// (RemoveAllActions / SetCurrentAction / RemoveLastAction) stayed in
// combat_diag.cpp.
//
// Hook wiring (hooks.toml + exports.def) is unchanged - only the
// definition's home file moved.

#include "combat_diag.h"
#include "combat_diag_internal.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "combat_queue.h"    // OnEngineActionAdded - authoritative announce
#include "engine_offsets.h"
#include "engine_player.h"
#include "log.h"
#include "engine_rebase.h"

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
            action_type = *(reinterpret_cast<uint8_t*>(action) + kCombatRoundActionTypeOffset);
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
