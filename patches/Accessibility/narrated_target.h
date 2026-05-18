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
//   - passive_narrate::Tick    (engine LastTarget change → name speech)
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

namespace acc::narrated_target {

struct Slot {
    void*        obj        = nullptr;  // CSWSObject* (server-side)
    uint32_t     handle     = 0;        // server-side handle
    unsigned int tickStamp  = 0;        // GetTickCount() at stamp time
};

// Stamp the slot. Both args must be the server-side pair (use
// `acc::engine::GetObjectHandle(obj)` if you only have a client-side
// handle). No-op when either is zero; call sites that hit empty-state
// fallbacks shouldn't stamp.
void Stamp(void* obj, uint32_t serverHandle);

// Clear the slot. Called on area transition; also safe to call defensively.
void Clear();

// Read the current slot with validation. Returns false when:
//   - Slot has never been stamped (or was cleared)
//   - Stored handle no longer resolves to the stored object (destroyed,
//     area changed without our Clear catching it, etc.)
// Output is zero'd on false return so callers don't need to check the
// fields after a false.
bool TryGet(Slot& out);

}  // namespace acc::narrated_target
