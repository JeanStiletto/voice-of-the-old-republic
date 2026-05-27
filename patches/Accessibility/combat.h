// Combat event narration. Poll-based — no engine re-entry beyond
// documented accessors.
//
// Channels:
//   - Combat mode entry/exit — debounced via the turn_announce stability
//     pattern; speaks "Kampf beginnt" / "Kampf beendet".
//   - Combat log — polls CSWGuiInGameMessages.messages_listbox for new
//     rows. Resolves via CGuiInGame.in_game_messages so it fires during
//     live combat, not just on the review screen.
//   - Attack resolution — diffs combat_round.attacks_list[7] for the
//     player creature; announces on attack_result transition.
//   - Saving throws — skeleton; needs a hook on SavingThrowRoll for
//     proper DC + roll. Currently a coarse field-diff heuristic.
//
// Each Tick is cheap and idle when nothing is happening.

#pragma once

namespace acc::combat {

// Cross-feature gating read — false on chain fault (treat as not-in-combat).
bool IsCombatActive();

void TickCombatMode();
void TickCombatLog();

// Skeleton scope: player creature only. Watching opponents would iterate
// AreaObjectIterator + per-creature snapshots.
void TickAttackResolutions();

// Skeleton — coarse stats-diff heuristic. Real DC + roll await a hook
// (SavingThrowRoll @0x5b92b0 or BroadcastSavingThrowData @0x4ec760).
void TickSavingThrows();

}  // namespace acc::combat
