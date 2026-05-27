// Engine input-code translation. Pure read-side; no engine state access.

#pragma once

namespace acc::engine {

// Human-readable name. "?" for unknown, "INPUTDEVICE_NONE" for -1.
const char* InputIndexName(int code);

// Translates the four KOTOR logical-action codes
// CSWGuiManager::HandleInputEvent rewrites before per-panel dispatch.
// Unknown codes pass through.
int ManagerTranslateCode(int code);

}  // namespace acc::engine

// Pre-translation codes received by CSWGuiManager::HandleInputEvent.
// File-scope for callsite brevity in the menu hooks.
constexpr int kInputNavUp    = 0xb6;
constexpr int kInputNavDown  = 0xb7;
constexpr int kInputNavLeft  = 0xb8;
constexpr int kInputNavRight = 0xb9;
constexpr int kInputEnter1   = 0xb5;
constexpr int kInputEnter2   = 0xbb;
constexpr int kInputEsc1     = 0xb4;
constexpr int kInputEsc2     = 0xdf;
constexpr int kInputActivate = 0x27;   // KEYBOARD_F1, engine's activate code

// Raw InputIndices — engine has no logical-action translation for these.
// Stock kotor.ini has no [Keymapping] for scancodes 32/33, so they arrive
// bare at the manager hook.
constexpr int kInputHome     = 32;
constexpr int kInputEnd      = 33;

// Raw InputIndices for unmapped Pillar 4 cycle keys.
// kInputKbAnnounce = physical key right of `.` (KEYBOARD_SLASH = 105) —
// labelled `-` on QWERTZ, `/` on QWERTY, same physical position. Keeps
// `,` `.` `-` contiguous on QWERTZ (`,` `.` `/` on QWERTY).
constexpr int kInputKbLeftShift  = 24;
constexpr int kInputKbRightShift = 25;
constexpr int kInputKbComma      = 103;
constexpr int kInputKbPeriod     = 104;
constexpr int kInputKbAnnounce   = 105;
