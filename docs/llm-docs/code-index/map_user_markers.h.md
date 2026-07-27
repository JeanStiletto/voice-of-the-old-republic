# map_user_markers.h (38 lines)

Public surface for the saved user-marker feature (Shift+N drops a map pin at
the cursor). Documents that persistence is area-load-only (no save/reload
survival yet) and that `kUserMarkerReferenceBase` is NOT a reliable identity
signal — engine map-note pins share the 0x80000000-high-bit client-id range,
so `IsUserMarkerPin`'s registry lookup is the only trustworthy discriminator.

## Declarations (in source order)

- L22 — `namespace acc::map_user_markers`
- L24 — `constexpr uint32_t kUserMarkerReferenceBase = 0x80000000u`
  note: arbitrary handed-to-engine reference number; not usable for "is ours" identity
- L26 — `void PollWin32()`
- L35 — `bool IsUserMarkerPin(void* pin)`
  note: true iff `pin` was created by this mod's drop feature in the CURRENT area-load; self-syncs to current area on call
