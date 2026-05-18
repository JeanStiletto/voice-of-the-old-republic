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
    // MapPin is map-context only — server-side game_objects[] doesn't
    // hold map pins (they live in CSWCArea.map_pins[]), so the iterator
    // never sees them and World-context BuildCategoryListing returns
    // empty for this category. cycle_state special-cases it in Map
    // context to walk the client-area pin array directly.
    MapPin       = 6,
    Count_       = 7,
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

// Which surface the cycle is currently driving. World = the in-world
// cycle that scans CSWSArea object-list; Map = the map-UI cycle that
// projects the same data onto the in-game area map, fog-of-war-gated.
// Carried through cycle_state / cycle_input so a single set of helpers
// handles both surfaces without duplicating the iteration loop.
enum class CycleContext : int {
    World = 0,
    Map   = 1,
};

// Which CycleCategory values render as discrete icons on the in-game
// area map. Sighted players see: door icons, transition arrows, named
// map-note pins. NPCs / items / non-quest containers don't render on
// the K1 map, so the map cycle silently skips them (the cycle-category
// loop already skips empty categories, so this just biases empty hard
// for non-map-cycleable kinds when the context is Map).
//
// Lay-off 1b will extend this with MapPin (quest markers) + Party
// (companion arrows) once the RE pass for CSWCArea.map_pins[] and
// GetPartyMemberMapLocation lands.
bool IsMapCycleable(CycleCategory c);

}  // namespace acc::filter
