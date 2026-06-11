// Camera-spin diagnostic (temporary). Per-tick observer that captures the
// "character spins endlessly with no input" bug in the logs so we can
// confirm which engine driver is firing.
//
// Two confirmed drivers of CSWCModule::AcclTurnCamera @0x640090 (decompiled
// 2026-06-11, see UpdateCamera @0x5f5e10):
//   1. cursor in the left/right screen edge band (width * screenFramePercentage,
//      default 0.001 → ~1px) → continuous accumulate-turn, no input needed.
//   2. the keyboard turn axis (0x11c) — what our synthesised A/D feeds; a
//      lost key-up on focus theft strands it.
// Both accumulate into the camera yaw cached at CSWCModule+0x98.
//
// This logs, each tick the camera is rotating (or the cursor is in the edge
// band), both yaw sources (quaternion vs position-derived — validating the
// GetCameraYawRadians fix), the cached accumulator, the cursor/edge state,
// and the rightClickHeld / mouse_look gate. Pure observation — no hooks, no
// input emitted. Remove once the fix lands.

#pragma once

namespace acc::camera_spin_diag {

void Tick();

}  // namespace acc::camera_spin_diag
