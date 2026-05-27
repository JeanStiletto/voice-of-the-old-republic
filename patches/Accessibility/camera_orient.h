// Camera-orient hotkey (N).
//
// Layer: guidance/ — combines `engine_player` (camera + player + chain) with
// `engine_compass` (sector math) and `guidance_beacon` (active waypoint
// target) to point the gameplay camera at a meaningful direction on demand.
//
// Behaviour:
//
//   - If a beacon is armed (`guidance::beacon::IsActive()` true), rotate
//     the camera to look at the beacon's current heartbeat target — the
//     next waypoint the user is being guided to.
//
//   - Otherwise, advance the camera clockwise to the *next* cardinal
//     direction (N → E → S → W → N). "Next" is computed in compass-CW
//     order so repeated presses cycle predictably from any starting yaw.
//
// Neither mode speaks anything itself. camera_announce stays muted while
// `IsActive()` is true (its sector-cross hysteresis would spam the user
// with intermediate direction words as the camera passes through), then
// announces the final direction once the rotation releases. The guidance
// loop (compass / beacon heartbeat) covers the waypoint context — a
// beacon-mode confirmation would just duplicate it.
//
// Drive mechanism — synthesised A/D keypresses via SendInput.
//
// Why not write the engine state directly:
//   - `AcclTurnCamera @0x640090` looks like the primitive but two failures
//     ruled it out: (a) calling it mid-frame with engine-context-unsafe
//     params crashed the engine one frame later (dump 121984.dmp), and
//     (b) for the *orbital* camera in normal play (CSWCameraOnAStick),
//     its vtable[7] slot is `return_zero @0x63e7f0` — so the inner-state
//     yaw write at `iVar3+0x40` is dead code. The only effect is setting
//     two recompute flags on CSWCModule (`+0x100`, `+0x80 bit 0`), which
//     the engine's later UpdateCamera pipeline consumes alongside other
//     UpdateCamera-side state (Tilt, mouse delta, PollInput timing) we
//     don't own.
//   - Writing `Camera::GetYaw`'s field at `behavior+0x3C` directly did
//     stick but didn't rotate — that field is read by the public GetYaw
//     accessor but isn't the source of truth for the rendered camera
//     transform (CSWCameraOnAStick's per-frame Control uses different
//     state).
//
// Why SendInput is the right call:
//   - The engine's UpdateCamera reads the yaw input via
//     `CExoInputInternal::PollInput(0x11c, 0)`. SendInput-driven A/D
//     keypresses reach DirectInput → PollInput, then the engine's whole
//     rotation pipeline runs as if the user were pressing the key — with
//     all the setup (Tilt, dt, mouse delta) intact.
//   - Per `feedback_hook_vs_poll_principle`, this is the engine doing
//     its job in response to input. We just synthesise the input.
//   - Closed-loop arrival check via `Camera::GetYaw @0x45C170` (the
//     engine getter we already use in probe_camera_distance) tells us
//     when to release the key. No timing magic, no overshoot from
//     guessing the engine's effective DPS.

#pragma once

namespace acc::camera_orient {

// Per-tick poll + state-machine driver. On rising-edge of
// `Action::CameraOrient`, arms a rotation and synthesises a held A or D
// keypress; subsequent ticks read Camera::GetYaw and release the key
// when the camera is within tolerance of the target yaw (or after a
// safety timeout). Cheap when idle (one hotkey query). Self-gates on
// player + camera resolved — in menus / chargen / pre-spawn this is a
// silent no-op.
void Tick();

// True while an auto-rotation is in flight (between the hotkey press
// that armed it and the closed-loop release). Consumers use this to
// distinguish "user is holding A/D" (announce per-sector crossing) from
// "we're driving the camera ourselves" (silent until landed). The
// canonical consumer is camera_announce::Tick, which suppresses its
// sector-cross speech while we're rotating so the user only hears the
// final direction once the camera settles.
bool IsActive();

}  // namespace acc::camera_orient
