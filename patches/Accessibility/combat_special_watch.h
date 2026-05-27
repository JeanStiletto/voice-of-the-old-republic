// Specials-queue heartbeat — "you can act now" cue.
//
// Walks every party member's combat_round.actions each tick, counts
// party-wide "specials" (anything not a routine auto-attack against a
// hostile creature: action_type != 1, or action_type == 1 with
// attack_feat != 0, or non-Creature target). Filters out the engine's
// 0xFF placeholder head.
//
// Edge trigger on ≥1 → 0 while in combat fires gui_actqueue immediately.
// 6s heartbeat repeats while specials stay at 0 in continued combat.
// First-round gate keeps the first ~6s silent so "Kampf beginnt" has
// clean air. Auto-resets across combat enter/exit.

#pragma once

namespace acc::combat::special_watch {

// Cheap out of combat (one chain walk). Call after combat::TickCombatMode.
void Tick();

}  // namespace acc::combat::special_watch
