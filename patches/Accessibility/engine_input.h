// Engine input-code translation.
//
// Layer: engine/ (pure read-side helpers; no menu-side state, no engine
// re-entry). The InputIndices enum and the manager-side logical-action
// translation are stable engine concepts — they don't belong in any one
// pillar's code.

#pragma once

namespace acc::engine {

// Look up the human-readable name of a CSWGuiManager-level InputIndices
// value (or one of the engine's logical action codes that pass through
// ManagerTranslateCode unchanged). Returns "?" for unknown codes,
// "INPUTDEVICE_NONE" for -1.
const char* InputIndexName(int code);

// Translate the four KOTOR-internal logical-action codes that
// CSWGuiManager::HandleInputEvent rewrites before dispatching to the active
// panel's per-class override. Codes outside the recognised set pass through
// unchanged.
int ManagerTranslateCode(int code);

}  // namespace acc::engine

// Logical input codes received pre-translation by CSWGuiManager::HandleInputEvent.
// Kept at file scope (not namespaced) for callsite brevity in the menu
// hooks where the bulk of comparisons happen. Values are stable engine
// constants from Lane's SARIF database.
//
// Up/Down (0xb6/0xb7), Left/Right (0xb8/0xb9), Enter (0xb5/0xbb), Esc
// (0xb4/0xdf), and the post-translation activate code (0x27 = KEYBOARD_F1)
// are all referenced from the menu input handler. See ManagerTranslateCode
// for what each code maps to post-translation.
constexpr int kInputNavUp    = 0xb6;
constexpr int kInputNavDown  = 0xb7;
constexpr int kInputNavLeft  = 0xb8;
constexpr int kInputNavRight = 0xb9;
constexpr int kInputEnter1   = 0xb5;
constexpr int kInputEnter2   = 0xbb;
constexpr int kInputEsc1     = 0xb4;
constexpr int kInputEsc2     = 0xdf;
constexpr int kInputActivate = 0x27;   // KEYBOARD_F1, the engine's activate code
