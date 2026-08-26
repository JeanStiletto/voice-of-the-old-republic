// Camera-spin guard + diagnostic. Per-tick observer of the "character spins
// endlessly with no input" bug, plus the cursor-edge guard that fixes it.
//
// Driver (confirmed in-game 2026-06-11, decompiled UpdateCamera @0x5f5e10):
// when the cursor sits in the left/right screen edge band the engine calls
// CSWCModule::AcclTurnCamera @0x640090 every frame → continuous
// accumulate-turn with no input. (The keyboard turn axis 0x11c — what our
// synthesised A/D feeds — is the same accumulator via a different source.)
//
// Guard (the fix): on an actual edge-turn spin — in-world, foreground,
// cursor in band, camera rotating — write the ENGINE cursor X field
// directly to viewport centre, clearing the band. Note this is NOT
// SetCursorPos: the post-load input pipeline ignores injected motion
// (SetCursorPos and SendInput were both tried and both failed — see the
// note in camera_spin_guard.cpp), which is why a direct field write is
// used. Gating on live rotation keeps it
// from disturbing a paused menu. The detection band is floored at kMinBandPx
// rather than the config arithmetic (which under-reports the engine's real
// band); anything that parks the cursor must stay clear of that floor.
//
// Watch (cause-agnostic): the guard above knows ONE cause. A spin can also
// come from a stuck mouse axis, a latched mouse-look, or something unlisted —
// and the field report that prompted this left no trace at all, because the
// guard logs only when IT fires and it had never fired in any session, ours or
// a tester's. So the watch keys on the SYMPTOM instead: camera rotating while
// the player commands nothing, no cinematic/screen owning the camera, and no
// snap-turn of ours running. Rotation is read from BOTH the orientation
// quaternion and the camera->player bearing, because an edge-turn orbits the
// camera and so sweeps the bearing too — a dead quaternion read cannot hide a
// spin. Log-only: naming an unknown cause comes before correcting it blind.
//
// Diagnostic (tag CameraSpinDiag), episode-based to avoid per-frame spam:
//   - "edge-guard START" once when an edge-guard episode begins.
//   - "edge-guard END" summary (corrections, edge frames, duration, peak
//     rate, net yaw) once the cursor has stayed clear of the band for ~0.5s.
//   - "UNCOMMANDED ROTATION xN" once per full uncommanded turn, carrying the
//     whole suspect state (cursor vs. band, mouse-look, right-click, held
//     movement key, camera distance, both yaws), plus an END summary.
//   - "READ ANOMALY" if quaternion- and position-derived yaw diverge — a
//     tripwire for a GetCameraYawRadians regression. Heavily gated: the two
//     agree only while the camera is in its normal follow pose, so cinematics,
//     blocking screens, snap-turns, player turn input and implausible follow
//     distances are all excluded, and the divergence must persist for seconds.
//     Ungated it produced hundreds of lines per session that looked like a
//     regression and were not.
// Kept in to confirm the guard fires and to track camera side-effects.

#pragma once

namespace acc::camera_spin_guard {

void Tick();

}  // namespace acc::camera_spin_guard
