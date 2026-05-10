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

// Wrapper around CExoInput::CoolDownEvent @ 0x005df4b0. Suppresses future
// emits of `eventID` from CExoInput::GetEvents for `ms` milliseconds. The
// engine itself uses this for state-changing keys (vanilla quickload sets
// CoolDownEvent(0x107, 1000) so a held key doesn't re-fire) — we use it
// to suppress held-Esc repeats after the upstream's case 0xdf fires.
//
// Resolves the CExoInput* through the global pointer slot at 0x007A39FC;
// returns silently if uninitialised (e.g. very early in DLL load before
// the engine builds its input subsystem). SEH-guarded.
void CoolDownInputEvent(int eventID, int ms);

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
constexpr int kInputTab      = 0xCE;   // LOGICAL_TAB, pre-translation logical code

// Raw InputIndices values for unmapped keys — used by the Pillar 4 cycle
// (Phase 2 lay-off 3). Unmapped keys pass through ManagerTranslateCode
// unchanged, so they arrive at the manager hook as their InputIndices index
// (positions in engine_input.cpp's k_names[] table).
//
// `kInputKbAnnounce` is the physical key right of `.` (KEYBOARD_SLASH = 105).
// On a German QWERTZ keyboard that key is labelled `-`, on US QWERTY it's
// labelled `/` — same physical position either way. DirectInput uses
// position-based scancodes, so KEYBOARD_MINUS(94) would hit the `ß` key
// position on QWERTZ rather than the `-` key the user expects. Pillar 4
// announce intentionally lives at the slash position to keep `,` `.` `-`
// in a contiguous row on QWERTZ (and `,` `.` `/` on QWERTY).
constexpr int kInputKbLeftShift  = 24;
constexpr int kInputKbRightShift = 25;
constexpr int kInputKbComma      = 103;
constexpr int kInputKbPeriod     = 104;
constexpr int kInputKbAnnounce   = 105;  // KEYBOARD_SLASH — see comment above
