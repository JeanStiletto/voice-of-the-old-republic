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
// Cycle scope: whole-area listings, sorted by distance. The earlier
// "current .lyt-room + LOS" narrowing plan was abandoned when .lyt-
// rooms turned out to over-segment K1 into spatially-meaningless
// slivers (see project memory on .wok rooms — mesh-identity OK, NOT
// perceptual). Whole-area listings are the right default: short
// enough to feel instant on Pillar 4's per-key cycle, and the cluster
// labels in announce already give the user a regional cue.

#pragma once

#include "engine_offsets.h"  // Vector
#include "filter_objects.h"  // CycleCategory

namespace acc::cycle {

// Sorted listing for one category. Fixed capacity matches the worst-case
// crowded-area scan (Manaan docks NPC count is ~30, Taris cantina has ~20
// placeables); 64 leaves headroom. Listings exceeding the cap are
// truncated and a one-time warning is logged.
//
// `isPin[i]` discriminates entry shape: false → CSWSObject* (waypoint /
// other game object), true → CSWCMapPin* (user-placed map pin folded into
// the Map hint cycle alongside waypoints). Consumers branch on this when
// resolving name (GetWaypointMapNote vs GetMapPinNoteText) and stamping
// the narrated_target slot (Stamp vs StampMapPin).
struct CategoryListing {
    static constexpr int kMaxObjects = 64;

    void*  objs[kMaxObjects];        // CSWSObject* OR CSWCMapPin*
    Vector positions[kMaxObjects];   // world position at scan time
    float  distances[kMaxObjects];   // distance from player at scan time
    bool   isPin[kMaxObjects];       // true → CSWCMapPin*, false → game object

    int count = 0;
};

// Mutable per-context state. World and Map keep independent focus so
// flipping the area map open + cycling doesn't pollute the in-world
// cycle's last focus and vice versa.
//
// Cycle's focusedObj/focusedIndex are LOCAL CURSOR state — they exist so
// `,`/`.` know where to step next within the current sorted listing.
// They are NOT the global activation target (that lives in
// `acc::narrated_target`). The cycle-input announce path stamps the
// narrated-target slot every time it speaks an item, which is what
// Enter / Shift+- / Ctrl+- / `-` read from.
struct CycleState {
    acc::filter::CycleCategory category   = acc::filter::CycleCategory::Door;
    void*                      focusedObj = nullptr;  // CSWSObject* (may be stale across rebuilds)
    int                        focusedIndex = -1;     // -1 = nothing focused
};

// Returns the singleton state for the given context (defaults to World
// for backwards-compat with call sites that pre-date Phase 6's map
// cycle). World and Map slots are independent — separate categories,
// separate focus indices.
CycleState& GetState(acc::filter::CycleContext ctx =
                         acc::filter::CycleContext::World);

// Build a sorted-by-distance-ascending listing for `category` from the
// current player area. Returns false if no area / no player position; in
// that case `out.count` is set to 0 and the caller should treat the listing
// as empty.
//
// `ctx` selects what filtering applies on top of `ObjectMatches`:
//   - World: no extra filtering.
//   - Map:   restricts to map-cycleable categories (returns empty
//            immediately for Npc/Container/Item) and fog-of-war-gates
//            survivors via IsWorldPointExplored against the per-area
//            CSWSAreaMap. Spoiler-correct by construction — the player
//            never hears the name of a landmark they haven't revealed.
//
// Side effects: none. Re-entrant. Safe to call from per-tick monitors.
bool BuildCategoryListing(acc::filter::CycleCategory category,
                          CategoryListing& out,
                          acc::filter::CycleContext ctx =
                              acc::filter::CycleContext::World);

// Cycle the focused item within the current category. Mutates the
// matching context's state's focusedIndex + focusedObj based on the
// supplied (already-built) listing. Returns the new focusedObj
// (nullptr if the listing is empty).
//
// Behaviour at boundaries: clamps. `,` at index 0 stays at index 0 (no
// wraparound); `.` at last index stays at last index. Per the plan §"Open
// design questions": wraparound vs. clamp is not yet locked — chose clamp
// for first impl because it's the more conservative default (doesn't
// surprise the user with a "back to start" jump).
void* CycleNextItem(const CategoryListing& listing,
                    acc::filter::CycleContext ctx =
                        acc::filter::CycleContext::World);
void* CyclePrevItem(const CategoryListing& listing,
                    acc::filter::CycleContext ctx =
                        acc::filter::CycleContext::World);

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
bool CycleNextCategory(CategoryListing& outListing,
                       acc::filter::CycleContext ctx =
                           acc::filter::CycleContext::World);
bool CyclePrevCategory(CategoryListing& outListing,
                       acc::filter::CycleContext ctx =
                           acc::filter::CycleContext::World);

// Refresh the listing for the current category (e.g. between cycle keys to
// pick up object movement / additions). Re-finds the previously-focused
// object by pointer in the new sorted order; if it's no longer present,
// resets focus to closest (index 0).
//
// Returns false if no area / no player position.
bool RefreshCurrentListing(CategoryListing& outListing,
                           acc::filter::CycleContext ctx =
                               acc::filter::CycleContext::World);

}  // namespace acc::cycle
