# guidance_beacon.cpp (266 lines)

Drives Pillar 1 3D audio cues along a pathfinder waypoint sequence — Pillar 3
Mode B's "audio guide" half. Fires a 3D-positional heartbeat cue toward the
next waypoint every kHeartbeatMs, soft-knee compressing distant waypoints
(beyond 18m) asymptotically toward the engine's ~20m audible ceiling along
the listener-to-target ray so bearing stays exact while apparent distance
stays hearable. On reaching a waypoint (3m tolerance) it fires a 2D-centred
arrival cue (unaffected by camera pan/voice-budget), speaks the next segment's
distance+compass, and advances; the final waypoint disarms with a distinct
destination-reached cue. Talks to audio_bus (PlayCue 2D), audio_cue_player
(PlayCue3D), engine_compass, and engine_player.

## Declarations (in source order)

- L16 — `namespace acc::guidance::beacon`
- L20 — `struct BeaconState` — path/nextIdx/lastHeartbeat/active
- L26 — `BeaconState g_state`
- L28 — `float DistXY(const Vector&, const Vector&)`
- L64-66 — `kHeartbeatKneeMeters=18`, `kHeartbeatCeilingMeters=20`, `kHeartbeatFarScaleMeters=40`
- L68 — `Vector CompressHeartbeatPosition(const Vector& target, const Vector& listener)`
  note: pure radial scale — direction/pan unchanged, only apparent distance bent.
- L106 — `void EmitHeartbeat(const Vector& worldPos, const Vector& listenerPos)`
  note: bypasses audio_cue_player's 80m range gate — the beacon must carry at any distance.
- L124 — `void EmitArrivalCue(acc::audio::NavCue cue, const char* tag)` — 2D, not 3D
- L142 — `void SpeakNextSegment(const Vector& playerPos, const Vector& nextWp)`
- L165 — `void StartBeacon(const std::vector<Vector>& waypoints)` — public
- L180 — `void CancelBeacon()` — public, idempotent
- L189 — `bool IsActive()` — public
- L193 — `bool GetCurrentTarget(Vector& out)` — public; used by camera_orient
- L200 — `void Tick()` — public; reach-check, heartbeat cadence, auto-disarm on unload
