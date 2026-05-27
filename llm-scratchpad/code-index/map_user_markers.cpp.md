# map_user_markers.cpp (159 lines)

Implementation of the Shift+N map-pin drop. Composes auto-name from
landmark/room tier lookup and calls CreateMapPin on the client area.

## Declarations (in source order)

- L15 — `namespace acc::map_user_markers`
- L17 — `namespace` (anonymous)
- L23 — `void* g_lastArea`
- L24 — `uint32_t g_seqInArea`
- L27 — `void MaybeResetForArea(void* currentArea)`
  note: resets per-area sequence counter when the area pointer changes
- L43 — `void BuildAutoName(void* area, const Vector& pos, int seq, char* outBuf, size_t bufSize)`
  note: tier 1 = proximity landmark (15m), tier 2 = authored room name (skips resref-style); falls back to bare "Marker N"
- L84 — `void OnDrop()`
  note: silent no-op when map is not foreground; rolls back sequence number on CreateMapPin failure
- L148 — `void PollWin32()`
