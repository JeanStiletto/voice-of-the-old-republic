// In-game / title-screen "Tastaturbelegung" screen (CSWGuiInGameOptKeyMappings,
// optkeymapping.gui).
//
// Shaped like the Fähigkeiten screen (menus_abilities) — a two-level submenu so
// the engine's mouse-only rebind path never sees the keys as raw chain nav:
//
//   * Tab level (where you land): Up/Down move across a single flat list of
//     [Bewegung, Spiel, Minigames, OK, Abbrechen, Standard], clamped. Enter on
//     a category drills into that category's binding listbox; Enter on
//     OK / Abbrechen / Standard activates the engine button (commit / discard /
//     reset). Esc falls through to the engine's close.
//   * List level (after Enter on a category): Up/Down/Home/End browse the
//     bindings, each announced as "{action}: {key}". Enter on a changeable row
//     arms a key capture (the engine grabs the next keypress and stages it;
//     OK persists). Esc returns to the tab level.
//
// The engine model is decompiled in build/re/optkeymappings-keymapbutton: each
// row is a CSWGuiKeyMapButton whose embedded mapped_key_button holds the bound
// key; SetCaptureEvent arms capture; the per-frame Update applies the next key;
// OnAcceptClick commits to live input + swkotor.ini. We never touch persistence.

#pragma once

namespace acc::menus::keymap {

// True iff `panel` is the keyboard-mapping screen.
bool IsKeyMapPanel(void* panel);

// Dedicated two-level input handler. Returns true and sets outRv when it
// consumed the event; mirrors the TryHandleInput contract used across the menu
// dispatch (rv: 0 = pass to engine, 1 = consumed).
bool HandleInput(void* activePanel, int param_1, int param_2, int& outRv);

// Per-tick poll: detects when an armed key capture completes and re-announces
// the focused row's new binding. No-op off the keyboard screen.
void Tick();

}  // namespace acc::menus::keymap
