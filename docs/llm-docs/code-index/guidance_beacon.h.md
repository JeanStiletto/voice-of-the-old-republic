# guidance_beacon.h (48 lines)

Audio beacon header. Drives Pillar 1 3D cues along a pathfinder waypoint sequence (Pillar 3 Mode B). Singleton; StartBeacon supersedes any prior. Ctrl+- toggles.

## Declarations (in source order)

- L23 — `namespace acc::guidance::beacon`
- L27 — `constexpr float kReachToleranceMeters = 3.0f;`
  note: 3m chosen because tighter values leave the beacon stuck at corridor corners where the walkmesh routes past without crossing <1.5m of the node
- L30 — `constexpr unsigned int kHeartbeatMs = 800;`
- L34 — `void StartBeacon(const std::vector<Vector>& waypoints);`
  note: empty waypoints cancels; first cue fires on next Tick (no cadence delay for the arm)
- L36 — `void CancelBeacon();`
  note: idempotent
- L38 — `bool IsActive();`
- L42 — `bool GetCurrentTarget(Vector& out);`
  note: world position of the next waypoint being steered toward; used by camera_orient
- L46 — `void Tick();`
  note: cheap idle (one bool check); un-load mid-flight silently disarms
