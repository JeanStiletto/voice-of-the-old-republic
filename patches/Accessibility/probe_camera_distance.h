// Camera-distance probe — hunts for the addressable orbit-radius field and
// tests whether direct writes survive the engine's per-frame update.
//
// Purpose: feasibility check for "Option B" — collapsing camera distance to
// ~0 so the engine's camera-anchored audio listener ends up at the player.
// If that works, the engine's own audio listener naturally becomes
// character-centric without us detouring SetListenerPosition/Orientation.
//
// Engine surfaces (from Ghidra decomp of AcclTurnCamera @0x640090,
// Camera::GetDist @0x0045c1d0, CSWCameraOnAStick::Control_ComputeDesiredPosition
// @0x00637af0):
//
//   Chain to active behavior:
//     *kAddrAppManagerPtr → AppManager
//       +0x04            → CClientExoApp
//         +0x04          → CClientExoAppInternal
//           +0x18        → CSWCModule
//             +0x40      → Camera*
//                          Camera::vtable[0x80](-1) → CAurBehavior* (active)
//
//   For free-roam the active behavior is CSWCameraOnAStick. Its addressable
//   distance is at field46_0x110 — used directly in Control_ComputeDesired
//   Position as Vector(0, distance, 0) rotated by the orientation
//   quaternion at field6_0x28..0x37.
//
//   Cache fields on the behavior:
//     +0x84  field29_0x84   non-zero = "auto-fit framing" on; Control
//                            recomputes field46_0x110 each frame from
//                            field30_0x88 / field31_0x8c. We expect this
//                            to stomp any clamp write we make.
//     +0x110 field46_0x110  TARGET orbit radius (smoothed toward this).
//     +0x120 field50_0x120  z-offset (camera height above target).
//
//   Engine primitives (use these for control rather than raw writes):
//     CSWCModule::ZoomCamera(this, delta) @0x006401d0 — adds delta to the
//       deep-struct distance field (same chain Accl{Turn,Tilt}Camera use).
//     Camera::GetDist(this) @0x0045c1d0 — returns the cached distance via
//       the engine's own accessor (validates our offset).
//
// Hotkeys (Ctrl-modified to avoid colliding with the existing plain-F-key
// probes; the existing probes have their `modsForbidden` widened to
// kModCtrl so Ctrl+F* lands only here):
//
//   Ctrl+F12 — ProbeCameraDistDump. One-shot snapshot. Logs camera chain
//              pointers, all candidate offsets, Camera::GetDist() result,
//              and the auto-fit flag.
//   Ctrl+F11 — ProbeCameraDistClampToggle. Cycles per-tick clamp mode:
//              OFF → 0.0m → 0.5m → 2.0m → OFF. While active, each tick
//              writes the target value into field46_0x110 AND reads back
//              what the engine has now AND logs both. Tracks stomps
//              (write_target ≠ readback) per second.
//
// Diagnostic-only TU; ships only while we're characterising the camera-
// distance surface. Strips out once Option B is either confirmed viable
// (probe goes away as the design locks in via a real per-tick clamp or
// detour) or shown unworkable (we retreat to Option A's listener detours).

#pragma once

namespace acc::probe_camera_distance {

// OnUpdate per-tick poll. Reads the two probe hotkey edges and (when the
// clamp toggle is active) writes + reads back the distance field each
// tick. Self-gates on AppManager chain being non-null.
void Tick();

}  // namespace acc::probe_camera_distance
