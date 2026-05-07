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
// Lay-off 4 layers the locked virtual-cursor design on top:
// - Cursor state: `Vector cursor_pos` + `float cursor_yaw` initialised
//   from player position / camera yaw on enter; W/S translate the
//   cursor along `cursor_yaw`, A/D rotate via stock engine input
//   (camera_announce reads the same engine yaw back for us).
// - Walkmesh-bounded movement via `engine_area::SegmentCrossesWalkmesh`
//   + Pillar 1 wall cache. On collision the cursor stops short of the
//   wall and emits a `NavCue::Wall` cue at the hit point.
// - Listener override via `OnSetListenerPosition` detour at the engine's
//   own per-frame `CExoSound::SetListenerPosition` write site
//   (hooks.toml entry @0x5d5df0). When view mode is active the hook
//   substitutes the cursor position for the engine's camera-derived
//   Vector so 3D audio (ambient, NPC voice, machinery, our cues) pans /
//   attenuates relative to the cursor — the engine's "soundscape walks
//   forward" without the character moving. Engine reclaims the listener
//   on view-mode exit because the substitution stops happening.
// - Object-nearest-cursor narration: walk `AreaObjectIterator` through
//   `filter::ObjectMatches`, find the closest in-radius object, three-
//   variable hover-pause debounce identical to `turn_announce`'s
//   pattern; speak the per-kind localised name once stable.
//
// Lay-off 5 (this lay-off) routes Enter / Shift+Enter while view mode is
// active:
// - Enter on a hover target → exit view mode, then call
//   `acc::interact::DispatchInteract(hover_obj, hover_handle, false)` —
//   identical to outside-view-mode Enter (engine action picker walks +
//   opens / talks / loots / picks up). Hover target comes from the
//   lay-off-4 hover-pause tracker.
// - Shift+Enter on a hover target → same call with forceRadial=true,
//   identical to outside-view-mode Shift+Enter (radial menu opens and
//   the user picks an action). Gives the user agency: Shift+Enter on
//   an NPC walks but doesn't auto-talk.
// - Enter or Shift+Enter on empty cursor (no hover target in the 1.0 m
//   radius) → exit view mode, then `acc::guidance::WalkTo(cursor_pos)` —
//   raw walk to the cursor's world position, no action.
// - In all cases view mode auto-exits BEFORE dispatch (decision (a) of
//   the lay-off plan: clean lifecycle, autowalk runs against an
//   unfrozen character). The exit is silent (no "View mode off"
//   announce) so the dispatch announce isn't preempted.
// - Coordination with `interact_hotkey::PollHotkey`: PollHotkey gates
//   its own Enter branch on `!IsActive()` so the same VK_RETURN rising
//   edge can't double-dispatch via both paths.

#pragma once

#include "engine_offsets.h"  // Vector

namespace acc::view_mode {

// True when the view-mode toggle is engaged (between B presses). Read by
// lay-off 4+ input handlers to decide whether to redirect movement keys
// to camera-pan and freeze the character. Lay-off 3 itself doesn't
// consume this value beyond the lifecycle sanity checks below.
bool IsActive();

// True if `PollEnter` (lay-off 5 Enter / Shift+Enter dispatch) handled
// the current tick's VK_RETURN rising edge. AUTO-CLEARS on read so the
// flag can't outlive its tick.
//
// Why this exists: `view_mode::Tick` runs earlier in OnUpdate than
// `interact_hotkey::PollHotkey`, and `PollEnter` exits view mode before
// dispatching. So by the time PollHotkey checks `IsActive()` the answer
// is already false, and PollHotkey would re-fire the same Enter press
// through `OnInteract` — double-dispatch (verified in
// patch-20260506-142103.log: empty-cursor WalkTo immediately followed by
// OnInteract-on-stale-LastTarget cancelling it). PollHotkey now checks
// `ConsumedEnterThisTick()` in addition to `IsActive()` so the press is
// owned end-to-end by view_mode.
//
// Read-and-clear semantics: PollHotkey is the only consumer; clearing
// on read is fine because no other code path needs to observe the flag
// across ticks.
bool ConsumedEnterThisTick();

// Read the current virtual-cursor world position. Returns false when
// view mode isn't active (no meaningful cursor) — callers should fall
// back to whatever default the engine would have used. The lay-off-4
// listener-override hook is the primary consumer: when view mode is
// active it substitutes this position for the engine's camera-derived
// listener Vector.
bool TryGetCursorPosition(Vector& out);

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

// Per-tick driver for the virtual cursor + Enter dispatch while view
// mode is active.
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
//   5. (Listener override happens in `OnSetListenerPosition`, not here
//      — the engine's own per-frame call site is detoured; while view
//      mode is active the hook substitutes `cursor_pos` for the
//      engine's camera-derived Vector. No per-tick call from this
//      function.)
//   6. Walks `AreaObjectIterator` for the nearest in-radius object
//      passing `filter::ObjectMatches`; if the same object is
//      "hovered" continuously for kHoverPauseMs and differs from the
//      last spoken target, speaks its localised name. Captures both
//      the handle and the CSWSObject* so lay-off-5 Enter dispatch
//      can hand the obj+handle pair to `DispatchInteract` without
//      re-resolving.
//   7. (Lay-off 5) Polls VK_RETURN; on rising edge dispatches Enter /
//      Shift+Enter (see file header). This runs LAST in the tick so
//      the hover state read by the Enter handler reflects the cursor
//      position computed earlier this same tick — no one-tick lag.
//
// Order of work in `OnUpdate` matters: this must run AFTER
// `camera_announce::Tick()` (so the dead-reckoned camera yaw is
// up-to-date this tick) and AFTER `view_mode::PollWin32()` (so a
// rising-edge B-toggle this tick takes effect immediately, not next
// tick). It must also run BEFORE `interact_hotkey::PollHotkey()` so
// our Enter dispatch is observed before PollHotkey's gate-on-IsActive
// short-circuit kicks in for the same press.
void Tick();

}  // namespace acc::view_mode
