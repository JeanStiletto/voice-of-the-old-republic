// Level-up screen trigger.
//
// Layer: engine/ (pure entry-point wrapper; no menu-side state, no engine
// re-entry beyond the wrapped call). Calls
// CGuiInGame::ShowLevelUpGUI @0x0062dc00 — the same engine surface the
// btn_levelup click on the Charakterblatt walks into via
// CSWGuiInGameCharacter::ShowLevelUpGUI @0x006b0bb0. Going through the
// CGuiInGame variant means we don't need the InGameCharacter panel
// constructed (its controls only exist while the panel is open) — the
// engine creates / surfaces the level-up panel itself from the singleton
// CGuiInGame*.
//
// First version: blind hotkey (Shift+L) bound from interact_hotkey.cpp.
// Once the panel is foreground the existing chain-walker enumerates its
// child buttons (5 BTN_STEPNAME slots from leveluppnl.gui) and the user
// can navigate by ear.
//
// param_1 of ShowLevelUpGUI is unknown (Ghidra signature is
// `undefined4 __thiscall ShowLevelUpGUI(int param_1)` — no decompilation
// available because the .text section is packed). We pass 0; if that
// proves wrong we iterate from the in-game test result.

#pragma once

namespace acc::engine_levelup {

// Open the engine's level-up GUI by calling
// CGuiInGame::ShowLevelUpGUI(gui_in_game, 0). Returns true on success
// (chain resolved + call dispatched without faulting), false if any
// pointer in the AppManager → CClientExoApp → Internal → CGuiInGame
// chain is null or the call raises an SEH exception.
//
// Does not pre-validate "can the player level up" — engine-side gates
// like CSWSCreatureStats::CanLevelUp run inside ShowLevelUpGUI itself.
// If there's no XP to spend the engine surface should bail gracefully;
// caller can speak a "no level-up available" cue if our return value
// suggests the panel didn't open (currently we have no read-side signal
// for that — the call returns undefined4 we don't interpret).
bool TriggerLevelUp();

}  // namespace acc::engine_levelup
