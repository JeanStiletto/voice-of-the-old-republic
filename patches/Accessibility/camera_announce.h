// Camera-direction announcement for blind navigation.
//
// Pulled forward from Phase 4 alongside turn_announce — closes the
// navigation feedback loop for KOTOR 1's verified default control scheme:
// A / D rotate the camera around the character (NOT character facing,
// NOT strafe), then W moves the character in the camera's forward
// direction. Without this, the user has no idea where the camera is
// pointing until they press W and the character snaps to that direction
// (which turn_announce then catches).
//
// Strategy: dead-reckon the camera yaw from observed A / D held state +
// the engine's `keyboard_camera_dps` setting (200°/s default per
// `swkotor.ini` Keyboard Camera DPS). Sync to the character's compass yaw
// whenever the character's yaw changes — every W press snaps the character
// to face the camera, so character.yaw == camera.yaw at that moment, which
// re-anchors the dead-reckoning estimate and stops drift accumulating.
//
// Why dead-reckon instead of reading the engine camera directly: the
// `Camera` struct in Lane's DB (size 0x234, /KotOR Types/Rendering) is a
// "PlaceHolder Structure" with no labelled rotation/position fields — the
// embedded `Gob` (rendering game object) doesn't surface its transform
// matrix in the symbols. Dead-reckoning is honest about its accuracy
// budget, doesn't require RE work, and the character-yaw resync makes
// drift bounded to a single A/D burst's duration.
//
// Sign convention: A rotates camera CCW (compass yaw decreases), D
// rotates CW (compass yaw increases). To be verified live; if reversed,
// flip kCameraDpsSignA and kCameraDpsSignD.
//
// Same sector + 5° hysteresis logic as turn_announce. interrupt=false on
// speech so camera direction doesn't talk over passive_narrate / cycle.

#pragma once

namespace acc::camera_announce {

// Per-tick poll. Reads A/D held state, integrates DPS into a tracked
// camera-yaw estimate, syncs to character yaw on character-yaw change,
// announces sector crossings. Self-gates on GetPlayerYawDegrees (silent
// in menus / chargen / pre-spawn / degenerate facing).
void Tick();

// Force the next sector-cross hysteresis check to treat `sector` as
// the last-announced one. Used by `camera_orient::ReleaseAndDisarm` in
// beacon mode: camera_orient speaks "Wegpunkt, <dir>" itself, then
// seeds the sector here so the post-release hysteresis sees pending
// == lastSpoken and stays silent. Without this the user hears the
// beacon cue immediately followed by camera_announce announcing the
// same direction word ~kQuietMs later.
//
// No effect in cardinal mode — there we *want* camera_announce's
// sector cross to fire (it IS the cardinal-cycle announcement).
void SeedLastSpokenSector(int sector);

// Read the dead-reckoned camera yaw in engine frame (0° = +X = East,
// CCW positive — same convention as `engine_player::GetPlayerYawDegrees`).
// Returns false until the first usable in-game tick has anchored the
// estimate (out untouched on false). Tick() must run at least once
// per call site that wants a fresh value — the integration is ticked
// in-line.
//
// Phase 4 lay-off 4a consumer: spatial_change_detector's Trigger 2
// (foremost-in-front) reads this via view_mode::GetEffectiveOrientation
// YawDegrees while view mode is active, so panning A/D pans the
// foremost-in-front cone with the camera. Without this, character yaw
// is frozen during view mode (per SetPlayerInputEnabled disable) and
// Trigger 2 wouldn't fire as the user explores.
bool TryGetCameraEngineYawDegrees(float& out);

}  // namespace acc::camera_announce
