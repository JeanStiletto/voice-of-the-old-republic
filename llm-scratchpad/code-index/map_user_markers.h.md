# map_user_markers.h (25 lines)

Saved user markers on the area map. Shift+N drops a CSWCMapPin at the map cursor
position. Auto-named from cursor room/landmark + per-area sequence number. Persists
for the current area-load only. Reference numbers start at kUserMarkerReferenceBase
to avoid colliding with engine's monotonic counter.

## Declarations (in source order)

- L19 — `namespace acc::map_user_markers`
- L21 — `constexpr uint32_t kUserMarkerReferenceBase`
  note: 0x80000000u — high half stays disjoint from engine counter which resets per area load
- L23 — `void PollWin32()`
