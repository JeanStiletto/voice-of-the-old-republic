// Player action bar (Aktionsmenü) submenu — Shift+4..Shift+7.
//
// While active:
//   Up/Down  — engine's up_button / down_button cycles the variant.
//   Enter    — fires via action_button (same as bare 4..7).
//   Esc      — disarm without firing.
//
// Bare 4..7 still fire the engine path untouched — this submenu is the
// additive "explore before firing" path.
//
// Self-disarms only on chain break (mid-area-load, teardown) or column
// going inactive. The action bar is always alive in-world.

#pragma once

namespace acc::actionbar_menu {

// slot 0..5. False on empty column (speaks "Spalte ist leer"). True armed.
bool Open(int slot);

bool IsActive();

// Press-edge only. Radial gate runs before this (modal precedence).
bool HandleInputEvent(int code, int value);

void ForceDisarm(const char* reason);

// Used by the bare-key announce path so 4..7 reads the same variant the
// engine fires.
int CurrentSelection(int slot);

}  // namespace acc::actionbar_menu
