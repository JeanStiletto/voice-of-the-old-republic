# guidance_beacon.cpp (203 lines)

Implementation of the audio beacon. Fires 3D heartbeat cues at the next waypoint, 2D arrival cues on reach, advances the cursor, re-announces next-segment distance+direction after each reach, and auto-disarms on final arrival or player un-load.

## Declarations (in source order)

- L16 — `namespace acc::guidance::beacon`
- L18 — `namespace { // anonymous`
- L20 — `struct BeaconState`
- L26 — `BeaconState g_state;`
- L28 — `float DistXY(const Vector& a, const Vector& b)`
- L40 — `constexpr float kBeaconHeartbeatGain = 8.0f;`
  note: 2x kAccCueGain (8.0x linear = ~+18 dB) so the cue carries at 15–25m distances against falloff
- L45 — `void EmitHeartbeat(const Vector& worldPos, const Vector& listenerPos)`
  note: 3D-positional so the user can localise the next waypoint by pan + falloff
- L61 — `void EmitArrivalCue(acc::audio::NavCue cue, const char* tag)`
  note: 2D-centred (not positional) — user IS at the waypoint, so direction is moot and 2D always audible
- L79 — `void SpeakNextSegment(const Vector& playerPos, const Vector& nextWp)`
  note: called after each waypoint reach; interrupt=false to queue behind the just-emitted arrival cue
- L100 — `} // namespace (anonymous)`
- L102 — `void StartBeacon(const std::vector<Vector>& waypoints)`
- L117 — `void CancelBeacon()`
- L126 — `bool IsActive()`
- L130 — `bool GetCurrentTarget(Vector& out)`
- L137 — `void Tick()`
