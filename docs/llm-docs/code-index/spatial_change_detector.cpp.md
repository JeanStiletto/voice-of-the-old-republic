# spatial_change_detector.cpp (1231 lines)

Implements Pillar 1 Triggers 1+2. T1 (`trigger1DistanceDelta`) tracks, per
world-frame sector (Front=E/Left=N/Back=W/Right=S — world-frame, immune to the
engine's yaw drift on plain movement), the closest clustered wall surface and
fires when its distance moves past `distanceDeltaThresholdMeters` since the
last fire on the *same* surface identity; entry/exit/identity-swap are silent
retracks. Objects (Pillar-4-filtered, party followers excluded) get the same
per-handle distance-delta treatment. T2 (`trigger2FrontCone`) tracks the single
closest wall-or-object within +-45 degrees of the player's *effective*
orientation (character yaw normally, camera yaw in view mode via
`view_mode::GetEffectiveOrientationYawDegrees`) and announces a foremost-identity
change once it settles (`kT2QuietMs`) or, during a continuous sweep, at the
`kT2HeldIntervalMs` cadence; cone-clear is silent by design ("open space
ahead"). A `kT2SwitchHysteresisMeters` discount makes the incumbent foremost
sticky so near-equidistant challengers don't thrash it. `CalibrateInRange` runs
on area change and on view-mode enter/exit (reference position swaps between
player and virtual cursor) to silently seed all last-distance baselines,
avoiding an area-load "wall of sound." Wall cache + clustering lives in
spatial_wall_surfaces.{h,cpp}; this file only reads through `ws::` accessors.
Talks to audio_cue_player/audio_cues, core_settings, filter_objects, view_mode.

## Declarations (in source order)

- L39 — `DWORD g_surface_last_cued_at[ws::kMaxWallSurfaces]`
  note: T2's same-surface dedup reads this to avoid double-announcing what T1 just cued
- L53 — `constexpr float kFireDedupTolMeters = 0.05f`
- L112 — `constexpr DWORD kSectorCooldownMs = 1800`
- L123 — `constexpr float kAwarenessRangeHysteresisMeters = 0.3f`
  note: 0.3m band stops sector enter/exit flapping at the range boundary
- L125-130 — `kSectorCount=4; g_sector_last_fired_surface/distance/closest_point/cued_at[4]`
- L139 — `struct ObjectState { uint32_t handle; float last_distance; DWORD last_cued_at; }`
- L145 — `constexpr int kMaxTrackedObjects = 256; ObjectState g_object_state[256]`
- L163 — `constexpr DWORD kT2QuietMs = 250; constexpr DWORD kT2HeldIntervalMs = 300`
- L174 — `constexpr float kT2SwitchHysteresisMeters = 0.5f`
- L176 — `enum class FeatureKind { None, Wall, Object }`
- L182 — `struct Foremost { FeatureKind kind; int wall_surface_index; uint32_t object_handle; }`
- L188 — `bool ForemostEqual(const Foremost& a, const Foremost& b)`
- L207-211 — `g_t2_last_fired, g_t2_pending, g_t2_pending_changed_at, g_t2_last_fired_at, g_t2_initialised`
- L217 — `float T2EffectiveDistance(const Foremost& cand, float rawDist)`
  note: discounts the incumbent's distance by kT2SwitchHysteresisMeters for selection only; raw distance still used for cue/log
- L230 — `float ClosestPointDistanceSquared(const Vector& p, const Vector& a, const Vector& b, Vector& outClosest)`
- L255 — `acc::audio::NavCue ClosedDoorCueForMaterial(void* obj)`
  note: mirrors RefineDoorCue in cycle_input.cpp (kept local — that helper is in an anonymous namespace)
- L268 — `acc::audio::NavCue CategoryToNavCue(acc::filter::CycleCategory c, void* obj)`
- L284 — `bool ClassifyObject(void* obj, acc::audio::NavCue& outCue)`
- L297-333 — `FindOrAddObjectState / FindObjectState / RemoveObjectState`
- L341 — `struct SurfaceScratch { bool in_range; float best_distance; Vector best_closest_point; }` + `g_surface_scratch[kMaxWallSurfaces]`
- L353 — `struct SectorCandidate {...}` + `g_sector_candidates[kSectorCount]`
- L402 — `enum class WallSector { Front=0, Left=1, Back=2, Right=3 }`
  note: for T1 these are abstract world-frame bins (E/N/W/S); for T2 the same enum is genuinely player-relative — two callers, one classifier
- L410 — `WallSector ClassifyRelativeBearing(float relBearingDeg)`
- L423 — `const char* SectorTag(WallSector s)`
- L435 — `void* g_prev_area`
- L448 — `bool g_was_using_cursor`
- L450 — `void OnAreaChange(void* area)`
  note: calls ws::RebuildForArea then resets all sector/object/T2 state
- L498 — `void CalibrateInRange(void* area, const Vector& referencePos, const char* reason)`
  note: seeds last_distance for every in-range wall+object without firing; reused on area-change AND view-mode enter/exit
- L620 — `void Tick()` (public)
  note: orchestrates area-change/reference-swap calibration, then T1 wall passes 1-4 (per-edge->per-surface->per-sector->fire-K-closest) + object scan + T2 foremost debounce; emits a summary log line only when something fired
- L1211-1229 — `GetCachedWalls / GetEdgeSurfaceId / SegmentCrossesSurface` (public)
  note: legacy forwards to wall_surfaces::
