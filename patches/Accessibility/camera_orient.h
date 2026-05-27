// Camera-orient hotkey (N).
//
//   - Beacon armed   → rotate to face the beacon's current heartbeat target.
//   - Otherwise       → advance one cardinal CW (N → E → S → W → N).
//
// Drive mechanism: synthesised A/D keypresses via SendInput with
// KEYEVENTF_SCANCODE so the input reaches DirectInput (plain-VK SendInput
// is invisible to it). Closed-loop arrival check via the engine's own
// yaw read releases the key.
//
// camera_announce stays muted while IsActive() is true and announces the
// final direction once the rotation releases.

#pragma once

namespace acc::camera_orient {

// Per-tick poll + state-machine driver. Cheap when idle.
void Tick();

// True while an auto-rotation is in flight. Consumers use this to
// distinguish "user holding A/D" (announce per-sector) from "we're
// driving" (silent until landed).
bool IsActive();

}  // namespace acc::camera_orient
