// Level-up screen trigger.
//
// Calls CGuiInGame::ShowLevelUpGUI @0x0062dc00 — same surface the
// Charakterblatt btn_levelup click reaches via
// CSWGuiInGameCharacter::ShowLevelUpGUI @0x006b0bb0. Going through the
// CGuiInGame variant works without the InGameCharacter panel
// constructed; the engine surfaces the level-up panel from the
// CGuiInGame singleton.
//
// param_1 unknown — Ghidra has no decompile (.text packed). Passing 0
// is the current guess; iterate if wrong.

#pragma once

namespace acc::engine_levelup {

// True iff the active leader has actually earned the next level, per the
// engine's own CSWSCreatureStats::CanLevelUp @0x005a6810 — the same
// predicate that drives the Charakterblatt btn_levelup enabled state
// (checks level cap, accumulated XP vs the required-XP table, and the
// class-side gates). Pure read-only; safe to poll. False when the leader
// can't be resolved (returns the same as "not ready" so callers gate
// conservatively).
bool PlayerCanLevelUp();

// True iff chain resolved + dispatched without SEH. Refuses (returns
// false, leaves level_up_mode untouched) when PlayerCanLevelUp() is
// false — the engine's ShowLevelUpGUI only gates on level_up_mode, which
// we force to 1, so without this guard the wizard opens regardless of XP
// and the player can level up endlessly.
bool TriggerLevelUp();

}  // namespace acc::engine_levelup
