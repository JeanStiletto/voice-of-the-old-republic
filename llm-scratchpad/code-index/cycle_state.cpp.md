# cycle_state.cpp (307 lines)

Implementation of cycle state management. Builds sorted-by-distance area listings, folds user-placed map pins into Map-context Landmark listings, and implements the item/category step primitives.

## Declarations (in source order)

- L10 — `namespace acc::cycle`
- L12 — `namespace { // anonymous`
- L17 — `void SortByDistanceAscending(CategoryListing& l)`
  note: insertion sort; reorders objs/positions/distances/isPin in lockstep; stable; cheap at N≤64
- L42 — `float HorizontalDistance(const Vector& a, const Vector& b)`
  note: 2D XY distance only; Z deliberately ignored for clock-position + metres TTS
- L50 — `} // namespace (anonymous)`
- L52 — `CycleState& GetState(acc::filter::CycleContext ctx)`
  note: returns one of two static singletons (World vs Map); independent focus for each context
- L58 — `bool BuildCategoryListing(acc::filter::CycleCategory category, CategoryListing& out, acc::filter::CycleContext ctx)`
- L211 — `void* CycleNextItem(const CategoryListing& listing, acc::filter::CycleContext ctx)`
- L227 — `void* CyclePrevItem(const CategoryListing& listing, acc::filter::CycleContext ctx)`
- L242 — `namespace { // anonymous`
- L244 — `bool CycleCategoryDirectional(CategoryListing& outListing, bool forward, acc::filter::CycleContext ctx)`
  note: internal helper that does the empty-category skip loop; restores start category and resets focus on total failure
- L267 — `} // namespace (anonymous)`
- L269 — `bool CycleNextCategory(CategoryListing& outListing, acc::filter::CycleContext ctx)`
- L274 — `bool CyclePrevCategory(CategoryListing& outListing, acc::filter::CycleContext ctx)`
- L279 — `bool RefreshCurrentListing(CategoryListing& outListing, acc::filter::CycleContext ctx)`
