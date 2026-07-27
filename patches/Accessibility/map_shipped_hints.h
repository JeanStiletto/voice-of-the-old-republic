// Curated map hints shipped with the mod.
//
// A small static table of (module resref, world position, localised
// label) for story spots the game never marks but blind players struggle
// to find: the Sandral estate backdoor, the Promised-Land rebel corpses
// in the Taris sewers, the bantha grazing band by the krayt-dragon cave.
// Positions come straight from the module .git data (verified against the
// K1CP-patched .mods 2026-07-27).
//
// Speech/cycle-only: entries fold into the Map hint cycle listing
// (cycle_state) fog-of-war-gated through the engine's own
// IsWorldPointExplored — they reveal exactly like vanilla map notes — and
// are NEVER materialised as engine CSWCMapPins, so nothing is drawn on
// the visible map and no engine lifetime applies. Listing entries carry a
// pointer to the table row (static storage, stable forever); the
// activation path stamps them with the map-pin slot shape (frozen
// position), which narrated_target validates back through IsShippedHint.

#pragma once

#include <cstddef>

#include "engine_offsets.h"  // Vector
#include "strings.h"

namespace acc::map_shipped_hints {

struct ShippedHint {
    const char*      module;  // module resref, lowercase
    Vector           pos;     // authored world position (from .git data)
    acc::strings::Id label;
};

// Fill `out` with up to `maxOut` pointers to this module's hints (stable
// table rows). Returns the count; 0 when the module has none or no module
// resolves.
int CollectForCurrentModule(const ShippedHint** out, int maxOut);

// True when `p` is a row of the hint table AND its module matches the
// current module — the staleness contract narrated_target needs (a stamp
// from a previous area must invalidate on transition).
bool IsShippedHint(const void* p);

// Localised label for a table row previously handed out by
// CollectForCurrentModule / carried in a listing. False on a foreign
// pointer; outBuf is always NUL-terminated.
bool GetLabel(const void* p, char* outBuf, size_t bufSize);

}  // namespace acc::map_shipped_hints
