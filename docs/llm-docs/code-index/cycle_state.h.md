# cycle_state.h (82 lines)

Pillar 4 cycle state header. Single source of truth for current category and focused-object tracking. Whole-area listings sorted by distance; .lyt-room narrowing not used.

## Declarations (in source order)

- L15 — `namespace acc::cycle`
- L24 — `struct CategoryListing`
  note: per-category snapshot of up to 64 objects sorted by distance; isPin[] discriminates CSWSObject* vs CSWCMapPin* entries
- L38 — `struct CycleState`
  note: LOCAL cursor state for `,`/`.` stepping — global activation target lives in acc::narrated_target
- L44 — `CycleState& GetState(acc::filter::CycleContext ctx = acc::filter::CycleContext::World);`
- L53 — `bool BuildCategoryListing(acc::filter::CycleCategory category, CategoryListing& out, acc::filter::CycleContext ctx = acc::filter::CycleContext::World);`
  note: ctx=Map fog-gates via IsWorldPointExplored and respects IsMapNoteEnabled; ctx=World has no extra filtering
- L59 — `void* CycleNextItem(const CategoryListing& listing, acc::filter::CycleContext ctx = acc::filter::CycleContext::World);`
  note: clamps at boundary, no wrap; returns nullptr on empty listing
- L63 — `void* CyclePrevItem(const CategoryListing& listing, acc::filter::CycleContext ctx = acc::filter::CycleContext::World);`
- L69 — `bool CycleNextCategory(CategoryListing& outListing, acc::filter::CycleContext ctx = acc::filter::CycleContext::World);`
  note: silently skips empty categories; focusedIndex=0 on success; both focusedIndex and outListing.count=0 on all-empty
- L72 — `bool CyclePrevCategory(CategoryListing& outListing, acc::filter::CycleContext ctx = acc::filter::CycleContext::World);`
- L78 — `bool RefreshCurrentListing(CategoryListing& outListing, acc::filter::CycleContext ctx = acc::filter::CycleContext::World);`
  note: tries to preserve previously-focused object after a rebuild; resets to closest when the old object is gone
