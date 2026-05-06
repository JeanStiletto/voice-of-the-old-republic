// Phase 4 lay-off 2 — view-mode "Mouse Look" probe.
//
// Diagnostic-only hotkey: Shift+AltGr toggles
// `CClientOptions.mouse_look` and announces the new state. Intended to
// answer the long-term-plan question "does forcing engine Mouse Look ON
// give us 90% of view mode for free?".
//
// Once the probe has informed the view-mode design, this file goes away
// (or the hotkey rebinds to the actual view-mode toggle) — nothing else
// in the patch consumes it.
//
// Key choice (Shift+AltGr):
// - AltGr (VK_RMENU) alone is `announce_degrees`. To avoid double-fire,
//   `announce_degrees::PollWin32` gates against Shift held.
// - Shift+AltGr is unbound by KOTOR's stock keymap (verified — kotor.ini
//   doesn't bind RMENU at all) and rare in German typing (AltGr+Shift
//   produces no standard glyph).
// - Same Win32-polling rationale as `announce_degrees::PollWin32` and
//   `cycle_input::PollWin32`: AltGr is unbound in stock kotor.ini, so the
//   engine keymap drops the scancode before our manager-side hook sees
//   it. Polling reads OS-level state directly and self-gates on the
//   foreground window + a player-loaded check.

#pragma once

namespace acc::probe_mouselook {

// OnUpdate per-tick poll. Reads VK_SHIFT + VK_RMENU via GetAsyncKeyState,
// fires `acc::engine::ToggleMouseLook` on rising edge, logs both the
// pre- and post-toggle state and speaks an "on"/"off" cue so the user
// knows the keypress landed. When the toggle lands ON it also kicks off
// a synthetic mouse sweep (see TickSweep below) so a non-sighted user
// can hear whether the engine reacts to mouse motion.
void PollWin32();

// OnUpdate per-tick driver for the synthetic mouse-sweep state machine.
// Cheap when idle (one bool check). When a sweep is active (after a
// toggle-to-ON), emits relative-motion mouse events via `SendInput` at
// roughly tick-rate cadence over ~1.5s. The listener is camera-anchored
// (Phase 1 lay-off 4) — if the engine's Mouse Look ON actually drives
// camera rotation from mouse motion, the soundscape pans audibly during
// the sweep. If nothing pans, mouse_event/SendInput either doesn't reach
// the engine (raw-input bypass) or the bit isn't the runtime gate.
void TickSweep();

}  // namespace acc::probe_mouselook
