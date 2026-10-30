// Camera-orient hotkey (N).
//
//   - Beacon armed   → rotate to face the beacon's current heartbeat target.
//   - Otherwise       → face an exit of the player's region (room_topology's
//                       exit points, real bearing from the player): the one
//                       nearest the camera heading, then clockwise on each
//                       further press while the player stays in the region
//                       and still faces the exit N last turned to.
//   - No region/exits → advance one cardinal CW (N → E → S → W → N).
//   A turn that stays inside one compass sector is silent on purpose:
//   nothing heard means the heading was already right (user decision
//   2026-10-30).
//
// Drive mechanism, first choice: the snap — the free-roam camera's one-shot
// desired-orientation request (a yaw the camera takes on its next update and
// then clears; nothing in the engine writes it). Turns in a frame or two.
// Fallback when the active camera never takes it (combat camera) or the build
// has no address for it: the key-hold turn below.
//
// Key-hold turn: synthesised keypresses of the player's *bound* turn key via
// SendInput with KEYEVENTF_SCANCODE, so the engine's own UpdateCamera runs its
// full poll-axis → turn → apply pass exactly as if the key were held. The
// scancode comes from engine_keymap::TurnScancode() (read from swkotor.ini
// [Keymapping]), so a turn rebind is honoured — the only change from the old
// hardcoded A/D. (Calling the engine's AcclTurnCamera directly from our
// out-of-band tick was tried and does not reliably move the chase-cam.) A
// closed-loop arrival check via the engine's own yaw read releases the key.
//
// camera_announce stays muted while IsActive() is true and announces the
// final direction once the rotation lands, if the sector changed.

#pragma once

namespace acc::camera_orient {

// Per-tick poll + state-machine driver. Cheap when idle.
void Tick();

// True while an auto-rotation is in flight. Consumers use this to
// distinguish "user holding A/D" (announce per-sector) from "we're
// driving" (silent until landed).
bool IsActive();

}  // namespace acc::camera_orient
