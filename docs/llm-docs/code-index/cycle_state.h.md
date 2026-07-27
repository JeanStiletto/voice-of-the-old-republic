# cycle_state.h (95 lines)

Pillar 4 cycle state — single source of truth for "what's currently in the cycle". Cycle scope is always a whole-area listing sorted by distance (not `.lyt` rooms, which over-segment K1 into spatially-meaningless slivers). World and Map contexts keep independent `CycleState` singletons (independent focus, independent default category — Map defaults to Landmark since Door etc. aren't map-cycleable). `CategoryListing` entries can be a real game object, a user-placed `CSWCMapPin*`, or a shipped curated static hint (`map_shipped_hints::ShippedHint*`) — `isPin`/`isStatic` discriminate, at most one set per entry.

## Declarations (in source order)

- L15 — `namespace acc::cycle`
- L27 — `struct CategoryListing`
  note: kMaxObjects=64; parallel arrays objs/positions/distances/isPin/isStatic
- L42 — `struct CycleState` — category/focusedObj/focusedIndex
  note: LOCAL cursor state only; the global activation target lives in acc::narrated_target
- L48 — `CycleState& GetState(acc::filter::CycleContext ctx = World)`
- L57 — `bool BuildCategoryListing(acc::filter::CycleCategory category, CategoryListing& out, acc::filter::CycleContext ctx = World)`
  note: ctx=Map filters to map-cycleable categories + fog-of-war gate; ctx=World applies discovery-tier filtering instead
- L63 — `void* CycleNextItem(...)`, L66 `CyclePrevItem(...)`
  note: boundary clamp, no wrap
- L72 — `void* CycleFirstItem(...)`, L75 `CycleLastItem(...)`
- L82 — `bool CycleNextCategory(...)`, L85 `CyclePrevCategory(...)`
  note: empty-category silent skip — scans up to 6 sibling categories
- L91 — `bool RefreshCurrentListing(CategoryListing& outListing, acc::filter::CycleContext ctx = World)`
  note: re-finds the previously-focused object in the new sort order; resets to closest if it's gone
