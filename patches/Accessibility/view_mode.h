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
// Lay-off 3 shipped:
// - Hotkey: B (V is "Solo Mode" in stock kotor.ini — taken; B is
//   unbound per `docs/controls-and-input.md`).
// - On enter: SetPlayerInputEnabled(false), speak "View mode on".
// - On exit:  SetPlayerInputEnabled(true),  speak "View mode off".
// - Diagnostic Shift+B probe: snapshots CClientOptions bitfield + four
//   neighbouring slots (kept from the earlier design as a cheap
//   reusable observer; not load-bearing for view mode itself).
//
// Lay-off 4 (this lay-off) layers the locked virtual-cursor design on
// top:
// - Cursor state: `Vector cursor_pos` + `float cursor_yaw` initialised
//   from player position / camera yaw on enter; W/S translate the
//   cursor along `cursor_yaw`, A/D rotate via stock engine input
//   (camera_announce reads the same engine yaw back for us).
// - Walkmesh-bounded movement via `engine_area::SegmentCrossesWalkmesh`
//   + Pillar 1 wall cache. On collision the cursor stops short of the
//   wall and emits a `NavCue::Wall` cue at the hit point.
// - Listener override every tick: `audio_bus::SetListener(cursor_pos)`
//   so 3D audio (ambient, NPC voice, machinery, our cues) pans /
//   attenuates relative to the cursor — the engine's "soundscape walks
//   forward" without the character moving.
// - Object-nearest-cursor narration: walk `AreaObjectIterator` through
//   `filter::ObjectMatches`, find the closest in-radius object, three-
//   variable hover-pause debounce identical to `turn_announce`'s
//   pattern; speak the per-kind localised name once stable.

#pragma once

namespace acc::view_mode {

// True when the view-mode toggle is engaged (between B presses). Read by
// lay-off 4+ input handlers to decide whether to redirect movement keys
// to camera-pan and freeze the character. Lay-off 3 itself doesn't
// consume this value beyond the lifecycle sanity checks below.
bool IsActive();

// Effective orientation yaw in engine frame (0° = +X = East, CCW positive)
// for cue systems whose semantics should follow the camera in view mode
// and the character otherwise.
//
// - View mode active + camera yaw anchored → camera yaw (per
//   `acc::camera_announce::TryGetCameraEngineYawDegrees`).
// - Otherwise (or camera yaw not yet anchored) → player yaw
//   (per `acc::engine::GetPlayerYawDegrees`).
//
// Returns false only when both readers fail (pre-spawn / area-load) —
// caller treats that as "no cue this tick", same shape as the existing
// `GetPlayerYawDegrees`-fail short-circuits.
//
// Phase 4 lay-off 4a — Trigger 2 (foremost-in-front) consumes this so
// the cone tracks where the user is *looking* during view mode, not
// where the character is statically facing.
bool GetEffectiveOrientationYawDegrees(float& out);

// OnUpdate per-tick poll. Reads VK_B + VK_SHIFT via GetAsyncKeyState,
// edge-detects rising edges, and dispatches:
//   plain B  → ToggleViewMode (skeleton enter / exit)
//   Shift+B  → camera-behavior probe (snapshot to log)
// Self-gates on foreground window + GetPlayerPosition (no fires in
// menus / chargen / pre-spawn). Same shape as cycle_input::PollWin32
// and announce_degrees::PollWin32.
void PollWin32();

// Per-tick driver for the virtual cursor while view mode is active.
// Self-gates on `IsActive()`; idle when view mode is off.
//
// Each call:
//   1. Computes dt from GetTickCount() since the previous call,
//      capped to avoid teleport-on-stall.
//   2. Reads camera yaw via `camera_announce::TryGetCameraEngineYawDegrees`
//      (A/D rotates camera natively in K1; we read the engine value
//      rather than re-deriving the keys ourselves).
//   3. Reads W/S held state via `GetAsyncKeyState`, foreground-gated.
//   4. Steps the cursor along the heading at the configured speed
//      (default 2.0 m/s, KOTOR walk speed); if the step crosses a
//      walkmesh perimeter edge, clamps the cursor short of the hit
//      point and emits `NavCue::Wall` at the hit position.
//   5. Writes the cursor position to the engine's listener via
//      `audio_bus::SetListener` so 3D audio repositions to the cursor.
//   6. Walks `AreaObjectIterator` for the nearest in-radius object
//      passing `filter::ObjectMatches`; if the same object is
//      "hovered" continuously for kHoverPauseMs and differs from the
//      last spoken target, speaks its localised name.
//
// Order of work in `OnUpdate` matters: this must run AFTER
// `camera_announce::Tick()` (so the dead-reckoned camera yaw is
// up-to-date this tick) and AFTER `view_mode::PollWin32()` (so a
// rising-edge B-toggle this tick takes effect immediately, not next
// tick).
void Tick();

}  // namespace acc::view_mode
