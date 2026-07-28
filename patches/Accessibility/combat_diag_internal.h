// internal seam between combat_diag.cpp and combat_queue_hooks.cpp.
//
// This is NOT public API. The Phase-1 structure pass (refactoring
// candidate 14) moved OnCombatRoundAddAction out of combat_diag.cpp
// because it is not a diagnostic: it drives the shipped combat-queue
// announce ("X, Platz N" / "Warteschlange voll") by forwarding to
// acc::combat::queue::OnEngineActionAdded. The three genuinely
// diagnostic CSWSCombatRound hooks stayed behind.
//
// Both files tag their log lines with the same PLAYER/other role, so that
// one helper is published here.

#pragma once

namespace acc::combat_diag {

// "PLAYER" when `combatRound` belongs to the user's creature, "other" for
// companion / enemy rounds sharing the same engine surfaces. Defined in
// combat_diag.cpp.
const char* RoleTag(void* combatRound);

}  // namespace acc::combat_diag
