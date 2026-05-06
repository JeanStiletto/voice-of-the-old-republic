// View mode — "stop and look around without budging the character".
//
// Layer: feature/ (composes engine_options + future engine_camera helpers).
// Phase 4 lay-off 3 = skeleton + camera-behavior probe; lay-off 4+ adds
// keyboard-driven camera input.
//
// User model: pressing B freezes the character and lets the player
// inspect the room from where they're standing — no walking, no
// triggering combat / pressure plates / dialog triggers, just a
// stationary look-around. The mode mirrors the sighted-player
// experience of pausing, looking around, deciding what's interesting
// before committing to a move.
//
// Hotkey choice (B):
// - V is "Solo Mode" in stock kotor.ini — taken.
// - B is unbound by KOTOR's stock keymap (see docs/controls-and-input.md
//   §"Default keyboard controls"; B does not appear in any of the
//   manual's bound-key tables).
// - Adjacent to V on the keyboard and visually mnemonic for "Behold /
//   look around".
//
// Implementation primitive (post-2026-05-06 in-game test):
// `SetPlayerInputEnabled(false)` — gates the per-tick W/S movement
// clobber. KOTOR's stock A/D already rotates the camera *only* (not
// the character) — verified blind via AltGr heading announce; the
// character only "snaps" to camera direction when W/S commits forward
// motion. So freezing W/S is sufficient: A/D continues to pan camera
// freely, character stays put, view mode is achieved.
//
// What the lay-off 2 + lay-off 3 probes ruled out:
// - Mouse Look forcing — not needed; A/D drives camera natively without it.
// - Cursor recentring — not needed; we don't synthesise mouse deltas.
// - Caps Lock "Free Look" toggle — verified to have no CClientOptions
//   bit-flip and no audible effect on A/D camera-pan behaviour. Either
//   cut from K1 or visual-only (a blind user can't distinguish).
//   `CSWCameraFreeLook` exists in K1's binary (struct + ctor at
//   0x0063a5d0, Control at 0x00639d00) but appears unreachable via
//   Caps Lock in the runtime path.
//
// Lay-off 3 ships:
// - Hotkey: B (V is "Solo Mode" in stock kotor.ini — taken; B is
//   unbound per `docs/controls-and-input.md`).
// - On enter: SetPlayerInputEnabled(false), speak "View mode on".
// - On exit:  SetPlayerInputEnabled(true),  speak "View mode off".
// - Diagnostic Shift+B probe: snapshots CClientOptions bitfield + four
//   neighbouring slots (kept from the earlier design as a cheap
//   reusable observer; not load-bearing for view mode itself).

#pragma once

namespace acc::view_mode {

// True when the view-mode toggle is engaged (between B presses). Read by
// lay-off 4+ input handlers to decide whether to redirect movement keys
// to camera-pan and freeze the character. Lay-off 3 itself doesn't
// consume this value beyond the lifecycle sanity checks below.
bool IsActive();

// OnUpdate per-tick poll. Reads VK_B + VK_SHIFT via GetAsyncKeyState,
// edge-detects rising edges, and dispatches:
//   plain B  → ToggleViewMode (skeleton enter / exit)
//   Shift+B  → camera-behavior probe (snapshot to log)
// Self-gates on foreground window + GetPlayerPosition (no fires in
// menus / chargen / pre-spawn). Same shape as cycle_input::PollWin32
// and announce_degrees::PollWin32.
void PollWin32();

}  // namespace acc::view_mode
