// Per-object disambiguator for same-LocName objects.
//
// Multiple objects in an area often share a localized display name
// (5x "Wandverkleidung" in one corridor; 3x "Sith-Soldat" in a fight).
// Sighted players tell them apart by position + reticle; blind players
// hear the same name in a row with no way to refer back.
//
// Wraps engine::GetObjectName and appends a stable numeric suffix when
// two or more live objects in the current area share the resolved name.
// Suffix is keyed by server handle, assigned on first narration, and
// persists for the rest of the area lifetime — surviving members keep
// their slot even after others die.

#pragma once

#include <cstddef>

#include "filter_objects.h"  // CycleCategory + ObjectMatches + Vector (via engine_area.h)

namespace acc::narration {

// Mirrors engine::GetObjectName's contract: true on non-empty resolved
// name; false leaves outBuf empty. `category` selects the disambiguation
// scheme (see AppendDisambiguator) so Q/E narration and the in-world cycle
// number the same object identically.
bool GetSpokenName(void* gameObject, acc::filter::CycleCategory category,
                   char* outBuf, size_t bufSize);

// Append the disambiguating number to an already-resolved name in outBuf,
// using the unified policy keyed on category:
//   - Npc (mobile "mobs"): handle-keyed encounter serial (AppendSuffix) —
//     creatures move, so a spatial rank would renumber them as they walk.
//   - Everything else (static): global north-to-south position ordinal
//     (AppendAreaPositionOrdinal).
// Single source of truth shared by Q/E (passive_narrate) and the in-world
// cycle so a given object hears the same number from either path.
void AppendDisambiguator(void* gameObject, acc::filter::CycleCategory category,
                         char* outBuf, size_t bufSize);

// Append a handle-keyed encounter serial to an already-resolved name in
// outBuf (combat narration paths use the engine's universal
// GetObjectDisplayNameByHandle). Uses the current outBuf contents as the
// LocName bucket key. Use for mobile creatures; static objects should use
// AppendAreaPositionOrdinal (position-stable) instead.
void AppendSuffix(void* gameObject, char* outBuf, size_t bufSize);

// Append a GLOBAL north-to-south positional ordinal to an already-resolved
// name in outBuf. Ranks gameObject against EVERY same-category object in the
// current area whose spoken name (engine GetObjectName) matches outBuf — not
// just discovered/listed ones — so the number is a pure function of fixed
// world position: stable across discovery progress, cycle direction,
// distance, visit, save, and player. No-op when fewer than two same-named
// peers exist. Use for static objects (doors, placeables, containers,
// transitions, map-note waypoints); creatures move, so they use AppendSuffix.
void AppendAreaPositionOrdinal(void* gameObject,
                               acc::filter::CycleCategory category,
                               char* outBuf, size_t bufSize);

// North-to-south world ordering. KOTOR's +Y axis points north (the compass
// derives heading as 90 - engineYaw, so due-north corresponds to increasing
// Y), making the greatest-Y entry the northmost. Ranking greatest-Y-first
// numbers from north to south; X then Z break exact ties so the order is
// total and reproducible.
bool PositionLess(const Vector& a, const Vector& b);

// Hooked into transitions.cpp's area-reset chain.
void Reset();

}  // namespace acc::narration
