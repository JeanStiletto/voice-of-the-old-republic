// KOTOR Accessibility — virtual mod-settings submenu.
//
// Layer: menus/ (UI state + speech). Reachable via a virtual "Mod
// Einstellungen" chain entry that we inject into both the in-game
// Optionen panel (PanelKind::InGameOptions) and the title-screen
// Optionen panel (PanelKind::MainMenuOptions).
//
// Design summary:
//
//   * The chain entry is NOT backed by a real engine control. We use a
//     static sentinel pointer (see GetRootAnchor) so we never hijack
//     someone else's button and never touch engine-allocated controls.
//     RebindChain inserts a virtual ChainEntry whose `virtualKind ==
//     kVirtualMod_SettingsRoot`; menus_extract::FromControl special-
//     cases the sentinel and returns the localised "Mod Einstellungen"
//     phrase; the Enter dispatcher in menus.cpp special-cases the
//     virtual-kind tag and opens the submenu instead of FireActivate.
//
//   * The submenu itself is entirely virtual — there is no second
//     engine panel. While IsOpen() is true, the manager input hook
//     routes navigation keys through HandleInput before the normal
//     chain dispatcher sees them. Up/Down cycles the focused option,
//     Space/Enter toggles, Esc closes and rebinds the parent chain.
//
//   * Three dummy toggle options ship as scaffolding: extended cycling,
//     room shape descriptions, wall sounds. The toggle bits live in
//     this TU (in-memory only — persistence is a follow-up). Real
//     consumers will read GetToggle(option) once those features are
//     wired.

#pragma once

#include <cstddef>

namespace acc::menus::modsettings {

// Per-option identifiers. Add a new option = add an enum row, a label
// strings::Id, and a row in the option table inside the .cpp.
enum class Option {
    ExtendedCycling = 0,
    RoomShapes,
    WallSounds,
    Count
};

// Sentinel pointer used as the `control` field of the virtual chain
// entry. Address of a static char in the .cpp; never dereferenced,
// never compared against any real engine control. Stable for the
// lifetime of the DLL.
void* GetRootAnchor();

// True iff `control` is the mod-settings root sentinel. Used by
// menus_extract::FromControl to override the speech text and by the
// Enter dispatcher to route Enter through OpenSubMenu instead of
// FireActivate.
bool IsRootAnchor(void* control);

// Visit the registered virtual chain anchor for `panel`. Fires exactly
// once on InGameOptions / MainMenuOptions; no-op for every other panel
// kind. Mirrors the credits / stat-row callback shape — sortCy is the
// synthetic y-coordinate the chain should use when sorting (we place
// the entry just above the Schliess. button so it lands as the last
// stop on a top-to-bottom arrow walk).
void ForEachRootAnchor(void* panel,
                       bool (*callback)(void* sentinel, int sortCx, int sortCy,
                                        void* userData),
                       void* userData);

// Format the root entry's speech ("Mod Einstellungen"). Returns true on
// success with `outBuf` filled. False only if the buffer is degenerate;
// the phrase itself is a constant localised string and never empty.
bool ExtractRootLabel(char* outBuf, size_t bufSize);

// Open the submenu. Saves the parent panel pointer (so we can rebind
// the chain on close) and announces the title + first option.
void OpenSubMenu(void* parentPanel);

// True while the submenu is interactive. While IsOpen the input hook
// in menus.cpp routes arrow / Space / Enter / Esc presses through
// HandleInput before the chain dispatcher.
bool IsOpen();

// Close the submenu and rebind the chain on the saved parent panel.
// No-op if not open. Speaks the parent context so the user knows
// they're back.
void Close();

// Input-press handler. Returns true iff the press was consumed by the
// submenu (caller must suppress the engine + chain dispatch for that
// key). Called only for press edges (param_2 != 0); release edges and
// non-matching keys return false. Safe to call when IsOpen() is false
// — returns false immediately.
//
// `keyCode` is the engine's kInput* value (kInputNavUp/Down/Enter/Esc
// /Space). Match list intentionally narrow: keys not in the list fall
// through to the engine so the user can still e.g. interact with the
// world while a settings menu is "logically open" (defensive — the
// submenu also blocks the parent panel from receiving input so this
// case shouldn't fire in practice).
bool HandleInput(int keyCode);

// Read the current state of a toggle option. Stable accessor for
// downstream feature wiring (extended_cycling, etc.) — they call this
// instead of reaching into the module's internals. Out-of-range
// `option` returns false.
bool GetToggle(Option option);

}  // namespace acc::menus::modsettings
