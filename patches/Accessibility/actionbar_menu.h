// Player action bar (Aktionsmenü) submenu — input gate, state, and speech
// wiring.
//
// Layer: input/menu (consumes engine_actionbar primitives + speech).
//
// User contract while active:
//   Up         — cycle to next variant within the focused column. Drives
//                the engine's column up_button widget. Speaks the new
//                variant label.
//   Down       — cycle to previous variant. Drives down_button.
//   Enter      — fire the current variant (drives action_button — same
//                effect as the engine-native 4..7 hotkeys would). Speaks
//                a brief confirmation and disarms.
//   Esc        — disarm without firing. Speaks "abgebrochen".
//   Re-press of the column's Shift+N — re-announce current variant
//                without cycling (handled by the polling caller, not
//                here).
//
// The submenu does NOT shadow engine-native hotkeys 4..7. Pressing 4
// alone still fires the column's currently-selected variant immediately
// (engine path, untouched). Shift+4..Shift+7 is the additive "explore
// before firing" path this submenu provides.
//
// Self-dismiss: the action bar is always alive while the player is
// in-world, so unlike the radial we don't tear down on per-tick state
// drift. We do disarm if the resolve chain breaks (mid-area-load,
// teardown) or the column we're focused on becomes inactive.

#pragma once

namespace acc::actionbar_menu {

// Enter explore mode for column `slot` (0..5). Reads the column's
// current variant text + variant count, speaks the opener pre-roll
// ("Aktionsmenü Spalte 5: Medikit (1 von 2)"), and arms the input gate.
//
// No-op when the column's is_action == 0 (column unpopulated for the
// current actor — e.g. the actor has no medical items): speaks a
// localised "Spalte ist leer" instead and leaves the gate disarmed so
// the user falls back to ordinary input.
//
// Returns true when the gate was armed.
bool Open(int slot);

// True when our gate is armed. Callers (interact_hotkey) check this to
// route Up/Down/Enter/Esc here instead of to the radial / world.
bool IsActive();

// Manager-level input gate. Called from interact_hotkey's Win32 poll
// AFTER the radial gate (radial wins ties because it's modal — the
// radial only opens via Enter on a target, while this submenu opens via
// Shift+N). Press-edge only — release events for consumed keys still
// pass through.
//
// `code` is a pre-translation logical InputIndices value (kInputNavUp,
// kInputEnter1, …). `value` mirrors the engine's val= field — non-zero
// is a press edge, zero is release.
bool HandleInputEvent(int code, int value);

// Forced disarm. Called when the user navigates away from the in-world
// context (panel push, area transition) or when the chain resolution
// fails mid-call. No engine-side cleanup — the engine column state is
// owned by the engine itself; we just drop our gate.
void ForceDisarm(const char* reason);

}  // namespace acc::actionbar_menu
