// Pillar 4 object filter — six categories locked in
// docs/navsystem-longterm-plan.md §"Categories (locked 2026-05-03 — six)".
//
// Layer: filter/ (pure predicate over engine_area's GameObjectKind; no engine
// re-entry of its own). Sits between engine_area (data) and cycle_state
// (focus tracking).
//
// Phase 2 lay-off 2: kind-only filtering. Sub-state filters (Container needs
// usable=true OR has_inventory=true; Landmark needs has_map_note=true;
// Transition needs transition_destination set) are documented in the plan
// but not enforced here — the per-type offsets land in lay-off 4 alongside
// per-type name resolution, where runtime verification is available. This
// lay-off's filter therefore over-includes Containers (every Placeable),
// Landmarks (every Waypoint), and Transitions (every Trigger). Acceptable
// as a stepping stone since lay-off 2 + 3 don't speak; the per-tick monitor
// log will show the over-inclusion clearly when the user audits before
// lay-off 4 tightens it.

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
