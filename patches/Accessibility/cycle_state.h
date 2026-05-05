// Pillar 4 cycle state — current category + focused-object tracking.
//
// Layer: filter/ (no engine re-entry; consumes engine_area + engine_player +
// filter_objects, exposes a small mutable singleton). The state singleton
// is the single source of truth for "what is currently focused"; consumers
// (input wiring in lay-off 3, announce in lay-off 4, guidance in lay-off 6)
// all read and mutate via this surface.
//
// Phase 2 lay-off 2: data layer only — no input dispatch, no speech. Built
// to be exercised from the per-tick monitor (or a one-shot probe) so the
// listing format and sort behaviour can be eyeballed in logs before lay-off
// 3 wires keys.
//
// Cycle scope (per docs/navsystem-longterm-plan.md §"Cycle scope"): the
// plan locks "current room + line-of-sight extension" for normal gameplay.
// This lay-off uses the simpler **whole-area** scope as a placeholder; the
// room+LOS narrowing lands when engine_area gets the room-cluster slice
// (currently scheduled for the same lay-off as Pillar 2 transitions, since
// both consume GetRoomAt deltas). Acceptable interim — over-includes
// objects from adjacent rooms but produces non-empty listings everywhere
// for dev-loop testing.

#pragma once

#include "engine_offsets.h"  // Vector
#include "filter_objects.h"  // CycleCategory

namespace acc::cycle {

// Sorted listing for one category. Fixed capacity matches the worst-case
// crowded-area scan (Manaan docks NPC count is ~30, Taris cantina has ~20
// placeables); 64 leaves headroom. Listings exceeding the cap are
// truncated and a one-time warning is logged.
struct CategoryListing {
    static constexpr int kMaxObjects = 64;

    void*  objs[kMaxObjects];        // CSWSObject*
    Vector positions[kMaxObjects];   // world position at scan time
    float  distances[kMaxObjects];   // distance from player at scan time

    int count = 0;
};

// Mutable singleton — one shared cycle state across the patch. Default
// category is Door (matches the order used by NextCategory/PrevCategory).
struct CycleState {
    acc::filter::CycleCategory category   = acc::filter::CycleCategory::Door;
    void*                      focusedObj = nullptr;  // CSWSObject* (may be stale across rebuilds)
    int                        focusedIndex = -1;     // -1 = nothing focused

    // GetTickCount() snapshot of the last user-driven `,`/`.`/Shift+`,`/`.`
    // mutation. Compared against passive_narrate::LastTargetChangeTick() in
    // interact_hotkey to resolve cycle-vs-engine focus conflicts (e.g. user
    // cycled to a Tür with `,`, then Q/E moved engine LastTarget to a Feldkiste
    // — Enter should target the Feldkiste). Bumped only by user-action mutation
    // paths, NOT by RefreshCurrentListing (refreshes are housekeeping, not
    // intent). 0 = never mutated; comparisons must use signed-diff to survive
    // GetTickCount wrap.
    unsigned int               mutationTick = 0;
};

// Returns the singleton state. Mutations via the cycle helpers below.
CycleState& GetState();

// Build a sorted-by-distance-ascending listing for `category` from the
// current player area. Returns false if no area / no player position; in
// that case `out.count` is set to 0 and the caller should treat the listing
// as empty.
//
// Side effects: none. Re-entrant. Safe to call from per-tick monitors.
bool BuildCategoryListing(acc::filter::CycleCategory category,
                          CategoryListing& out);

// Cycle the focused item within the current category. Mutates state's
// focusedIndex + focusedObj based on the supplied (already-built) listing.
// Returns the new focusedObj (nullptr if the listing is empty).
//
// Behaviour at boundaries: clamps. `,` at index 0 stays at index 0 (no
// wraparound); `.` at last index stays at last index. Per the plan §"Open
// design questions": wraparound vs. clamp is not yet locked — chose clamp
// for first impl because it's the more conservative default (doesn't
// surprise the user with a "back to start" jump).
void* CycleNextItem(const CategoryListing& listing);
void* CyclePrevItem(const CategoryListing& listing);

// Cycle the category. Empty-category silent skip per the plan §"Locked
// defaults — Pillar 4": Shift+,/. skips empty categories silently. Builds
// candidate listings inline until a non-empty one is found (max 6 attempts,
// then gives up and reports empty). On success, focusedIndex resets to 0
// and focusedObj is the closest item in the new category. On failure (no
// non-empty category in the area), focusedObj is null and focusedIndex
// is -1. Writes the resulting listing to `outListing` for the caller's
// announcement / log.
//
// Returns true if a non-empty category was found.
bool CycleNextCategory(CategoryListing& outListing);
bool CyclePrevCategory(CategoryListing& outListing);

// Refresh the listing for the current category (e.g. between cycle keys to
// pick up object movement / additions). Re-finds the previously-focused
// object by pointer in the new sorted order; if it's no longer present,
// resets focus to closest (index 0).
//
// Returns false if no area / no player position.
bool RefreshCurrentListing(CategoryListing& outListing);

}  // namespace acc::cycle
