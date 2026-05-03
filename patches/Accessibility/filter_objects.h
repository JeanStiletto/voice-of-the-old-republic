// Pillar 4 object filter — six categories locked in
// docs/navsystem-longterm-plan.md §"Categories (locked 2026-05-03 — six)".
//
// Layer: filter/ (pure predicate over engine_area's GameObjectKind; no engine
// re-entry of its own). Sits between engine_area (data) and cycle_state
// (focus tracking).
//
// Phase 2 lay-off 4: kind + sub-state filtering. Container requires
// usable=true OR has_inventory=true (CSWSPlaceable +0x328 / +0x334);
// Landmark requires has_map_note=true (CSWSWaypoint +0x228); Transition
// requires transition_destination set (CSWSTrigger +0x30c). Sub-state
// readers live in engine_area.{h,cpp} alongside the kind-aware name
// resolver. Per-tick monitor logs from lay-off 3 will show what gets
// included; if false-negatives surface (e.g. a usable scenery placeable
// that should cycle in), revisit the predicate.

#pragma once

#include "engine_area.h"  // GameObjectKind

namespace acc::filter {

// Six locked Pillar 4 categories in the cycle order returned by
// NextCategory / PrevCategory. Order matches the plan's listing.
enum class CycleCategory : int {
    Door         = 0,
    Npc          = 1,
    Container    = 2,
    Item         = 3,
    Landmark     = 4,
    Transition   = 5,
    Count_       = 6,
};

// Human-readable category name for logs / future TTS prefixing.
const char* CategoryName(CycleCategory c);

// Returns true if the game object belongs in this category. SEH-guarded
// internally via engine_area::GetObjectKind. Sub-state filters TODO
// (lay-off 4) — see header comment.
bool ObjectMatches(void* gameObject, CycleCategory category);

// Cycle helpers — pure stateless rotation over the six categories.
CycleCategory NextCategory(CycleCategory c);
CycleCategory PrevCategory(CycleCategory c);

}  // namespace acc::filter
