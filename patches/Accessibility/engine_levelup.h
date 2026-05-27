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

// True iff chain resolved + dispatched without SEH. Engine-side
// can-level-up gates run inside ShowLevelUpGUI itself.
bool TriggerLevelUp();

}  // namespace acc::engine_levelup
