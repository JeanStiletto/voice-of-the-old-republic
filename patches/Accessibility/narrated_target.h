// Unified "last narrated target" slot.
//
// Design principle: whichever announcement site spoke a bare-target-name
// most recently owns "what the user is thinking about". Activation keys
// (Enter, Shift+-, Ctrl+-, Alt+-, `-`) all read from this single slot
// instead of resolving their own focus from cycle_state / engine LastTarget
// / view-mode hover independently. Replaces the cycle-vs-engine recency
// tie-break that lived in interact_hotkey::ResolveInteractTarget.
//
// Stamp sites (the three narration channels that announce a target by name):
//   - passive_narrate::OnEngineShowObject  (engine ShowObject hook →
//                                           name speech on real focus
//                                           change; replaces the older
//                                           LastTarget poll)
//   - cycle_input::AnnounceCurrent (`,`/`.`/`Shift+,`/`Shift+.`/`-`)
//   - view_mode hover-pause speech
//
// Non-stamp sites (announce but aren't a focus claim):
//   - Interact pre-roll ("Sicherheit, Tür") — fired AFTER the target is
//     chosen; stamping would feedback-loop on a re-press
//   - Empty-state phrases ("no doors in range")
//   - heading / turn / party-swap / route description / beacon waypoints
//
// Read sites:
//   - interact_hotkey::ResolveInteractTarget  (Enter / Shift+Enter)
//   - cycle_input::OnPathfindFocus            (Shift+-)
//   - cycle_input::OnBeaconFocus              (Ctrl+-)
//   - cycle_input::OnPathfindFocusForce       (Alt+-)
//   - cycle_input::OnAnnounceFocus            (`-` repeat)
//
// Lifecycle:
//   - Cleared on area transition (transitions.cpp's area-change branch).
//   - TryGet re-validates by resolving the stored server handle back to
//     a CSWSObject* and confirming it matches the stamped pointer. Stale
//     entries (object destroyed, area switched without our Clear) return
//     false; caller falls back to its per-key fallback (Enter falls back
//     to engine LastTarget; the cycle-family keys speak "no focus").

#pragma once

#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::narrated_target {

// Two slot shapes — game-object and map-pin. Activation handlers read
// `isMapPin` and branch: map pins go to Ctrl+- beacon (no UseObject
// path exists since the pin isn't a CSWSObject), game objects keep
// the full Enter / Shift+- / Ctrl+- / Alt+- vocabulary.
struct Slot {
    void*        obj        = nullptr;  // CSWSObject* OR CSWCMapPin*
    uint32_t     handle     = 0;        // server-side handle (0 for map pin)
    Vector       pos        = {0, 0, 0};// stamp-time position (cached for
                                        // map pin where no server handle
                                        // resolves to a position later)
    unsigned int tickStamp  = 0;        // GetTickCount() at stamp time
    bool         isMapPin   = false;    // discriminator
};

// Stamp a game-object slot. Both args must be the server-side pair
// (use `acc::engine::GetObjectHandle(obj)` if you only have a client-
// side handle). No-op when either is zero; call sites that hit empty-
// state fallbacks shouldn't stamp.
void Stamp(void* obj, uint32_t serverHandle);

// Stamp a map-pin slot. `pin` is a CSWCMapPin* (out-of-band — not
// resolvable through ResolveServerObjectHandle); `pos` is the pin's
// world position at the moment of stamping (frozen — pins don't move
// once placed). Used by the map cycle's MapPin announcement so
// activation keys can route Ctrl+- to the pin's coordinates and
// reject Shift+-/Enter with a localized hint.
void StampMapPin(void* pin, const Vector& pos);

// Clear the slot. Called on area transition; also safe to call defensively.
void Clear();

// Read the current slot with validation. Returns false when:
//   - Slot has never been stamped (or was cleared)
//   - Game-object slot: stored handle no longer resolves to the
//     stored object (destroyed, area changed without our Clear
//     catching it, etc.)
//   - Map-pin slot: pin pointer no longer appears in the current
//     client area's map_pins[] (pin removed, area changed).
// Output is zero'd on false return so callers don't need to check the
// fields after a false.
bool TryGet(Slot& out);

}  // namespace acc::narrated_target
