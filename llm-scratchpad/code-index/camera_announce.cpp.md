# camera_announce.cpp (183 lines)

Implementation of the per-tick camera sector announcer with hysteresis, quiet
debounce, and held-key interval throttle.

## Declarations (in source order)

- L17 — `namespace acc::camera_announce`
- L19 — `namespace` (anonymous)
- L22 — `constexpr float kSectorSize`
- L23 — `constexpr float kHalfSector`
- L24 — `constexpr float kHysteresis`
- L27 — `constexpr DWORD kQuietMs`
  note: minimum stable-sector duration before speaking; collapses fast-rotation transients
- L33 — `constexpr DWORD kMinIntervalHeldMs`
  note: per-sector announce rate cap while A/D is held
- L37 — `constexpr float kMinXYDistance`
- L39 — `float AngularDelta(float a, float b)`
- L44 — `int s_lastSpokenSector`
- L45 — `int s_pendingSector`
- L46 — `DWORD s_lastChangeAt`
- L47 — `DWORD s_lastSpokenAt`
- L48 — `float s_lastCamCompass`
  note: cached for TryGetCameraEngineYawDegrees; -1 sentinel = not anchored
- L49 — `bool s_prevRelevantHeld`
  note: tracks prior-tick A/D XOR state for release-edge detection
- L51 — `bool ReadCameraCompass(float& outCompass)`
- L71 — `void Tick()`
- L174 — `bool TryGetCameraEngineYawDegrees(float& out)`
  note: inverts compass back to engine yaw using the same involution formula as EngineYawToCompass
