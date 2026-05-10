// Combat system event narration — Phases 1A, 1B, 4A/4B.
//
// Layer: poll-based event narration (engine reads only; no engine
// re-entry beyond the documented accessor calls).
//
// Three independent per-tick channels share this TU because they all
// observe combat state and announce edges:
//
//   * Phase 1A — combat-mode entry/exit. Polls `GetCombatMode @0x5ede70`
//     each tick; debounces with the stability pattern from
//     `turn_announce.cpp` so brief on/off cycles collapse to a single
//     edge. Speaks "Kampf beginnt" / "Kampf beendet".
//
//   * Phase 1B — live combat-log narration. Polls
//     `CSWGuiInGameMessages.messages_listbox @+0x64` for new rows; each
//     newly-appended row is pushed to TTS. Honours vanilla feedback
//     verbosity for free (the engine populates the listbox per its own
//     options). One frame later than `BroadcastFloatyData` would be —
//     accepted trade-off documented in `docs/combat-system.md` Phase 1.
//
//   * Phase 4A/4B — structured per-attack and saving-throw callouts.
//     Polls the player creature's `combat_round.attacks_list[7]` per
//     tick; on `attack_result` transition from pending (0) to a resolved
//     value, builds a localised announcement. Saving-throw watch is a
//     separate per-tick loop over the active party + nearest hostiles,
//     reading `creature_stats` save fields with edge-detection.
//
// All three channels are polling-based to keep this initial skeleton
// hookless. The combat-system.md plan calls out hook points
// (`AddMessages @0x626920`, `ResolveAttack @0x5bba80`,
// `BroadcastSavingThrowData @0x4ec760`) as future upgrades — they would
// give us 1-frame-earlier delivery. Polling is a safe default; the hooks
// can be added later without restructuring the consumers.
//
// Each Tick() is cheap (single chain-walk + small fixed read budget) and
// silently no-ops when the player isn't loaded or combat hasn't started.

#pragma once

namespace acc::combat {

// Phase 1A — per-tick combat-mode poll. Reads the engine's combat-mode
// flag; debounces and speaks on stable transitions only. Idle when no
// player is loaded.
void TickCombatMode();

// Phase 1B — per-tick combat-log poll. Walks the manager's panels[] for
// CSWGuiInGameMessages; if its messages_listbox has new rows since last
// tick, speak each appended row. Idle when the panel isn't loaded
// (early init / no game session) and when no rows were added.
//
// One-shot dedup: rebuilds the "last row count" baseline whenever the
// listbox pointer changes (new panel instance), so a swap or close+
// reopen doesn't replay every historic row.
void TickCombatLog();

// Phase 4A — per-tick attack-resolution poll. Snapshots the player
// creature's combat_round.attacks_list[7]; on attack_result transition
// from kAttackResultPending to a resolved value, builds a localised
// callout describing hit/miss/crit/deflected and speaks it. Idle when
// no combat round is active (`combat_round` null) or the snapshot hasn't
// changed.
//
// **Skeleton scope**: only the player creature's own attacks are watched
// here. Watching opposing creatures would require iterating
// `engine_area::AreaObjectIterator` per tick and snapshotting each one's
// attacks_list — viable but defers until 4A is validated against the
// player's own attack stream. Marked **TODO** inline.
void TickAttackResolutions();

// Phase 4B — per-tick saving-throw poll. **Skeleton only**: the engine's
// own save-throw event isn't directly observable without a hook
// (`SavingThrowRoll @0x5b92b0` or `BroadcastSavingThrowData @0x4ec760`).
// We diff the party creature stats' save fields per tick and announce
// when one ticks down by an effect-roll outcome — coarse heuristic that
// gets us "Bastila resisted" / "Bastila succumbed" without the exact
// roll/DC. Real DC + roll values lock in once the hook lands.
void TickSavingThrows();

}  // namespace acc::combat
