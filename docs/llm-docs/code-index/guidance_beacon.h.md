# guidance_beacon.h (48 lines)

Header for the audio-beacon nav guide (Pillar 3 Mode B). Documents the three
cues (BeaconActive heartbeat, BeaconWaypointReached, BeaconDestinationReached)
and the single-area-only scope (a transition destination goes stale on
module swap; user re-arms in the new area). Singleton — Ctrl+- toggles.

## Declarations (in source order)

- L23 — `namespace acc::guidance::beacon`
- L27 — `constexpr float kReachToleranceMeters = 3.0f`
- L30 — `constexpr unsigned int kHeartbeatMs = 800`
- L34 — `void StartBeacon(const std::vector<Vector>& waypoints)` — empty vector cancels
- L36 — `void CancelBeacon()`
- L38 — `bool IsActive()`
- L42 — `bool GetCurrentTarget(Vector& out)`
- L46 — `void Tick()`
