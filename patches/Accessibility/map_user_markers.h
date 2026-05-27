// Saved user markers on the area map.
//
// Shift+N drops a CSWCMapPin at the map cursor position. Auto-named from
// the cursor's room/landmark + per-area sequence number. Folds into the
// MapPin cycle category immediately.
//
// Persists for the current area-load only. Engine-side persistence uses
// per-player CSWSScriptVarTable variables; replicating that is follow-up
// work. Marker vanishes on area transition or save+reload.
//
// Reference-number range starts at kUserMarkerReferenceBase so we never
// collide with the engine's monotonic counter (counter resets per area
// load; our high-half range stays disjoint).

#pragma once

#include <cstdint>

namespace acc::map_user_markers {

constexpr uint32_t kUserMarkerReferenceBase = 0x80000000u;

void PollWin32();

}  // namespace acc::map_user_markers
