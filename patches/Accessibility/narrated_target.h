// Unified "last narrated target" slot.
//
// Whichever narration channel spoke a bare-target-name most recently owns
// "what the user is thinking about". Activation keys (Enter, Shift+-,
// Ctrl+-, Alt+-, `-`) read this single slot instead of each resolving
// their own focus independently.
//
// Stamp sites — passive_narrate (engine ShowObject), cycle_input
// (`,`/`.`/`-`), view_mode hover-pause. Non-name narration (pre-rolls,
// empty-state phrases, heading/turn/beacon waypoints) does NOT stamp.
//
// Two slot shapes: game-object (uses server handle for validation) and
// map-pin (uses pointer membership in the client area's map_pins[]).
// Map pins have no CSWSObject so the activation grammar is reduced.
//
// TryGet re-validates on every read and clears stale slots.

#pragma once

#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::narrated_target {

struct Slot {
    void*        obj        = nullptr;  // CSWSObject* OR CSWCMapPin*
    uint32_t     handle     = 0;        // server-side handle (0 for map pin)
    Vector       pos        = {0, 0, 0};// stamp-time pos (frozen for map pins)
    unsigned int tickStamp  = 0;
    bool         isMapPin   = false;
};

// Both args must be server-side. Use engine::GetObjectHandle(obj) if you
// only have a client-side handle.
void Stamp(void* obj, uint32_t serverHandle);

// Map-pin stamp. pos is the pin's frozen world position.
void StampMapPin(void* pin, const Vector& pos);

void Clear();

// Validated read. Returns false (and zeroes out) on stale slots —
// destroyed object, area-switch without an intervening Clear, pin
// removed from the area's map_pins[].
bool TryGet(Slot& out);

}  // namespace acc::narrated_target
