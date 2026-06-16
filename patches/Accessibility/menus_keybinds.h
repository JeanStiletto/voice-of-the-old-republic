// Mod keybind configurator — the "Tastenbelegung" submenu reached from Mod
// settings. Lets the user freely rebind every user-rebindable hotkeys::Action.
//
// Layer: menus/ (UI state + speech). Like the Audio glossary, this is an
// entirely virtual submenu (no engine panel): menus_modsettings owns an
// "open" flag and routes navigation keys here while it is set.
//
// Two-level navigation, mirroring the engine Key Mapping screen we made
// accessible (menus_keymap.cpp):
//
//   * Category level — the five categories plus a "restore defaults" entry.
//     Up/Down move, Enter drills into a category (or runs the reset), Esc
//     closes back to the Mod-settings root.
//   * Action level — each row reads "{name}: {bind}". Enter arms capture; Esc
//     returns to the category level.
//
// Capture is our own (the engine's SetCaptureEvent only serves engine panels):
// while armed, Tick() polls Win32 for the next physical key plus the modifiers
// held and writes the new binding. A clash with another mod binding, or with a
// hardcoded game key, is announced and capture stays armed for another try.
// Esc cancels capture.

#pragma once

namespace acc { namespace hotkeys { enum class Action : int; } }

namespace acc::menus::keybinds {

// Localised display name for a user-rebindable action, or "" if the action is
// not in the configurator catalogue (e.g. a diagnostic probe). Used by the Key
// Mapping screen accessibility layer to name the mod action a freshly-bound
// game key collides with.
const char* DisplayName(acc::hotkeys::Action action);

// True while the configurator owns input (any level, including capture).
bool IsOpen();

// Open at the category level and announce the title + first category. Called
// from menus_modsettings when the user presses Enter on the Tastenbelegung row.
void Open();

// Press-edge input handler. `keyCode` is the engine kInput* value (the same
// codes menus_modsettings::HandleInput receives). Returns true iff consumed.
// When the user Esc's out of the category level, this flips IsOpen() to false;
// the caller then re-announces the Mod-settings root row.
bool HandleInput(int keyCode);

// Per-frame poll. Drives the capture state machine (next-key detection). Cheap
// (a single early-out) when capture is not armed. Safe to call when closed.
void Tick();

// Force-close without speech. Called by menus_modsettings when the whole
// Mod-settings submenu is torn down (Esc at root / engine modal push) so a
// stale capture can't linger.
void Reset();

}  // namespace acc::menus::keybinds
