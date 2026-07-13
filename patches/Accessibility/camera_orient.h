// Camera-orient hotkey (N).
//
//   - Beacon armed   → rotate to face the beacon's current heartbeat target.
//   - Otherwise       → advance one cardinal CW (N → E → S → W → N).
//
// Drive mechanism: synthesised keypresses of the player's *bound* turn key via
// SendInput with KEYEVENTF_SCANCODE, so the engine's own UpdateCamera runs its
// full poll-axis → turn → apply pass exactly as if the key were held. The
// scancode comes from engine_keymap::TurnScancode() (read from swkotor.ini
// [Keymapping]), so a turn rebind is honoured — the only change from the old
// hardcoded A/D. (Calling the engine's AcclTurnCamera directly from our
// out-of-band tick was tried and does not reliably move the chase-cam.) A
// closed-loop arrival check via the engine's own yaw read releases the key.
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
