# map_user_markers.cpp (200 lines)

Implements the Shift+N "drop a saved marker at the map cursor" feature: places a
CSWCMapPin at the map-cursor world position, auto-names it from a nearby
landmark/room + a per-area sequence number, and tracks the pins it created so
other modules can recognise them (fog-of-war-exempt) without relying on a
reference-number heuristic. Talks to `map_ui_cursor` (cursor position),
`engine_area`/`engine_panels` (area + map-panel state), `transitions`
(landmark lookup), and `prism` for speech. Registry resets in lockstep with
area transitions since the engine frees all map pins on area change.

## Declarations (in source order)

- L15 — `namespace acc::map_user_markers`
- L22 — `void* g_lastArea` / `uint32_t g_seqInArea`
  note: per-area sequence, reset on area-pointer change
- L30 — `constexpr int kMaxUserMarkers = 64` / `void* g_userMarkers[64]` / `int g_userMarkerCount`
  note: fixed-size registry of pins created this mod; no-op past capacity (degrades gracefully)
- L36 — `void MaybeResetForArea(void* currentArea)`
  note: clears sequence + registry when area pointer changes
- L48 — `void RegisterUserMarker(void* pin)`
- L64 — `void BuildAutoName(void* area, const Vector& pos, int seq, char* outBuf, size_t bufSize)`
  note: tier 1 = landmark within 15m (matches cursor/walking-adapter window); tier 2 = engine room name (skips resref-style noise); tier 3 = bare "Marker N"
- L105 — `void OnDrop()`
  note: silent no-op if map panel isn't foreground or cursor inactive; speaks localized failure/success; rolls back sequence on CreateMapPin failure
- L174 — `bool IsUserMarkerPin(void* pin)`
  note: self-syncs to current area before compare — authoritative discriminator vs. reference-number range test
- L188 — `void PollWin32()`
  note: polls SaveMarkerAtCursor hotkey; gated on a loaded player position
