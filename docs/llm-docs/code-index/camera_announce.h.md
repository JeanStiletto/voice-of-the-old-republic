# camera_announce.h (38 lines)

Header for the per-tick camera-direction announcer. Documents why camera (not character) direction is announced: A/D rotates the camera only, W snaps the character to face it on commit, so without this the user can't tell where the camera points until moving.

## Declarations (in source order)

- L19 — `void Tick()`
- L23 — `bool TryGetCameraEngineYawDegrees(float& out)` — most recent observed camera yaw, engine frame (0°=+X, CCW+); false until anchored
- L36 — `bool AnnounceCurrentFacing(unsigned int dedupMs)` — one-shot facing readout for event triggers (e.g. door autoturn); dedups against an already-spoken same sector within dedupMs; false during cinematics or unresolved facing
