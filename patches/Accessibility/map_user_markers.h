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
// kUserMarkerReferenceBase is an arbitrary value handed to the engine as
// the pin's reference-number; it is NOT a reliable "this is ours" signal.
// Engine map-note pins are keyed by the waypoint's client object id, which
// always carries the 0x80000000 high bit, so a reference-number range test
// can't distinguish our markers from the engine's. Identity tracking
// (IsUserMarkerPin) is the authoritative discriminator instead.

#pragma once

#include <cstdint>

namespace acc::map_user_markers {

constexpr uint32_t kUserMarkerReferenceBase = 0x80000000u;

void PollWin32();

// True when `pin` is a CSWCMapPin this mod created via the saved-marker
// drop (Shift+N) in the CURRENT area-load. The map-hint cycle and the map
// cursor use this to identify player-placed markers — which legitimately
// skip the fog-of-war gate — without the brittle reference-number
// heuristic. The registry resets when the area pointer changes (pins are
// freed by the engine on area transition), so callers may invoke this from
// any context; it self-syncs to the current area.
bool IsUserMarkerPin(void* pin);

}  // namespace acc::map_user_markers
