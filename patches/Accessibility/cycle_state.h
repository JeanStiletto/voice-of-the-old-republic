// Pillar 4 cycle state — current category + focused-object tracking.
//
// Single source of truth for "what's currently in the cycle". Input,
// announce, and guidance all go through this surface.
//
// Cycle scope: whole-area listings sorted by distance. .lyt-rooms
// over-segment K1 into spatially-meaningless slivers, so they're not used
// for narrowing.

#pragma once

#include "engine_offsets.h"
#include "filter_objects.h"

namespace acc::cycle {

// Per-category listing. 64 cap covers worst-case crowded areas (Manaan
// docks ~30 NPCs, Taris cantina ~20 placeables); overflow truncates +
// warns once.
//
// isPin[i] discriminates entry shape: CSWSObject* (game object) vs
// CSWCMapPin* (user-placed map pin folded into the Map hint cycle).
// Consumers branch on this for name resolution and narrated_target stamping.
struct CategoryListing {
    static constexpr int kMaxObjects = 64;

    void*  objs[kMaxObjects];
    Vector positions[kMaxObjects];
    float  distances[kMaxObjects];
    bool   isPin[kMaxObjects];

    int count = 0;
};

// World and Map keep independent focus. focusedObj/focusedIndex are
// LOCAL cursor state for `,`/`.` stepping — the global activation target
// lives in acc::narrated_target, which the announce path stamps on speech.
struct CycleState {
    acc::filter::CycleCategory category   = acc::filter::CycleCategory::Door;
    void*                      focusedObj = nullptr;
    int                        focusedIndex = -1;
};

CycleState& GetState(acc::filter::CycleContext ctx =
                         acc::filter::CycleContext::World);

// Build sorted-by-distance listing for `category`. False on no area / no
// player position; out.count is 0.
//
// ctx=Map filters to map-cycleable categories and fog-of-war-gates
// survivors via IsWorldPointExplored — spoiler-correct by construction.
// ctx=World: no extra filtering.
bool BuildCategoryListing(acc::filter::CycleCategory category,
                          CategoryListing& out,
                          acc::filter::CycleContext ctx =
                              acc::filter::CycleContext::World);

// Boundaries clamp (no wrap). Returns new focusedObj (nullptr on empty).
void* CycleNextItem(const CategoryListing& listing,
                    acc::filter::CycleContext ctx =
                        acc::filter::CycleContext::World);
void* CyclePrevItem(const CategoryListing& listing,
                    acc::filter::CycleContext ctx =
                        acc::filter::CycleContext::World);

// Jump straight to the first (closest) / last (farthest) item of the
// listing. Returns the new focusedObj (nullptr on empty).
void* CycleFirstItem(const CategoryListing& listing,
                     acc::filter::CycleContext ctx =
                         acc::filter::CycleContext::World);
void* CycleLastItem(const CategoryListing& listing,
                    acc::filter::CycleContext ctx =
                        acc::filter::CycleContext::World);

// Empty-category silent skip: scans up to 6 sibling categories until a
// non-empty one is found. On success focusedIndex=0 and focusedObj is the
// closest item; on failure both reset. outListing receives the result.
bool CycleNextCategory(CategoryListing& outListing,
                       acc::filter::CycleContext ctx =
                           acc::filter::CycleContext::World);
bool CyclePrevCategory(CategoryListing& outListing,
                       acc::filter::CycleContext ctx =
                           acc::filter::CycleContext::World);

// Re-find previously-focused object in the new sorted order; if it's
// gone, reset to closest. False on no area / no player position.
bool RefreshCurrentListing(CategoryListing& outListing,
                           acc::filter::CycleContext ctx =
                               acc::filter::CycleContext::World);

}  // namespace acc::cycle
