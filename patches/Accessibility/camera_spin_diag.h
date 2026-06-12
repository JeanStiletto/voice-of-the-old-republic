// Camera-spin guard + diagnostic. Per-tick observer of the "character spins
// endlessly with no input" bug, plus the cursor-edge guard that fixes it.
//
// Driver (confirmed in-game 2026-06-11, decompiled UpdateCamera @0x5f5e10):
// when the cursor sits in the left/right screen edge band
// (width * screenFramePercentage, default 0.001 → ~1px) the engine calls
// CSWCModule::AcclTurnCamera @0x640090 every frame → continuous
// accumulate-turn with no input. (The keyboard turn axis 0x11c — what our
// synthesised A/D feeds — is the same accumulator via a different source.)
//
// Guard (the fix): on an actual edge-turn spin — in-world, foreground,
// cursor in band, camera rotating — nudge the physical cursor a small inset
// inward (SetCursorPos), clearing the band. Gating on live rotation keeps it
// from disturbing a paused menu.
//
// Diagnostic (tag CameraSpinDiag), episode-based to avoid per-frame spam:
//   - "edge-guard START" once when an edge-guard episode begins.
//   - "edge-guard END" summary (corrections, edge frames, duration, peak
//     rate, net yaw) once the cursor has stayed clear of the band for ~0.5s.
//   - "READ ANOMALY" (rate-limited) if quaternion- and position-derived yaw
//     diverge — a tripwire for a GetCameraYawRadians regression.
// Kept in to confirm the guard fires and to track camera side-effects.

#pragma once

namespace acc::camera_spin_diag {

void Tick();

}  // namespace acc::camera_spin_diag
