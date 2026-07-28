# cycle_state.cpp (402 lines)

Implements the Pillar 4 listing build + cursor stepping. `BuildCategoryListing` iterates `AreaObjectIterator`, filters via `filter::ObjectMatches`, applies discovery-tier gating (unless the "Extended cycling" mod setting is on) for World context or map-note/fog-of-war gating for Map context, then for Map+Landmark specifically folds in TWO extra sources: the mod's own saved map-pin markers (identified by identity in `map_user_markers`, not a reference-number bit test — the old `flags & 0x80000000` test misclassified every engine note pin) and the shipped curated static hints (`map_shipped_hints`), each still fog-gated through `IsWorldPointExplored` except user markers (the player placed those, so no spoiler). Sorts ascending by 2D horizontal distance (insertion sort, cheap at N≤64). Next/Prev/First/Last clamp at listing boundaries (no wrap, per project convention). `CycleNextCategory`/`CyclePrevCategory` try up to `Count_` sibling categories before giving up.

## Declarations (in source order)

- L14 — `namespace acc::cycle`
- L21 — `void SortByDistanceAscending(CategoryListing& l)`
- L49 — `float HorizontalDistance(const Vector& a, const Vector& b)`
  note: Z deliberately ignored — vertical separation doesn't matter for clock-position + metres TTS
- L59 — `CycleState& GetState(acc::filter::CycleContext ctx)`
  note: sMap defaults to CycleCategory::Landmark (not the struct-default Door) since Door isn't map-cycleable
- L73 — `bool BuildCategoryListing(acc::filter::CycleCategory category, CategoryListing& out, acc::filter::CycleContext ctx)`
  note: Map+Landmark folds in engine waypoint map-notes (via IsMapNoteEnabled + IsWorldPointExplored), then user markers, then shipped static hints, in that order
- L279 — `void* CycleNextItem(const CategoryListing&, acc::filter::CycleContext)`
- L295 — `void* CyclePrevItem(...)`
- L310 — `void* CycleFirstItem(...)`
- L323 — `void* CycleLastItem(...)`
- L339 — `bool CycleCategoryDirectional(CategoryListing& outListing, bool forward, acc::filter::CycleContext ctx)`
- L364 — `bool CycleNextCategory(...)`, L369 `CyclePrevCategory(...)`
- L374 — `bool RefreshCurrentListing(CategoryListing& outListing, acc::filter::CycleContext ctx)`
