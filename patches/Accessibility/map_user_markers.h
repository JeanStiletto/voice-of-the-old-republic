// Phase 6 lay-off 3 — saved user markers on the area map.
//
// Layer: input/ + narration/ — Shift+Q while the InGameMap panel is
// foreground drops a CSWCMapPin at the map cursor's current world
// position. The new pin is auto-named with the cursor's resolved
// room/landmark plus a per-area sequence number, copied into the pin's
// inline note_text CExoString via `engine_area::CreateMapPin`.
//
// Once placed, the marker:
//   - Appears in the existing MapPin cycle category (lay-off 1b)
//     immediately — no separate listing path, no new cycle category.
//   - Speaks confirmation via Tolk so the user knows the keypress
//     landed.
//   - Persists for the current area-load only. K1's engine-side
//     persistence uses per-player CSWSScriptVarTable variables
//     (`NW_TOTAL_MAP_PINS` + `NW_MAP_PIN_{NTRY,XPOS,...}_{N}` per pin)
//     and round-trips through the wire; replicating that is a follow-
//     up sub-lay-off. For now the marker vanishes on area transition
//     or save+reload.
//
// Hotkey self-gates on:
//   - `engine::HasActiveMapPanel()` true — map sub-screen foreground.
//   - `map_ui_cursor::TryGetCursorWorldPosition` returns a position
//     (cursor active + seeded).
// Failing either gate stays silent — the chord may be triggered out of
// context (in-world Shift+Q has no meaning today).
//
// The reference-number namespace for our markers starts at
// `kUserMarkerReferenceBase` (0x80000000) so we never collide with the
// engine's own monotonic counter (which starts at 1 and grows
// linearly). Counter resets per area-load; the engine's own
// `GetMapPin(ref, 1)` lookup remains usable since our refs sit in a
// disjoint range.

#pragma once

#include <cstdint>

namespace acc::map_user_markers {

// High-half range so we never collide with engine-placed pins (which
// counter up from 1 via NW_TOTAL_MAP_PINS).
constexpr uint32_t kUserMarkerReferenceBase = 0x80000000u;

// Per-tick poll. Edge-detects Shift+Q via the hotkey registry; on
// rising edge with the gates satisfied, drops a marker and speaks
// confirmation. No-op when idle.
void PollWin32();

}  // namespace acc::map_user_markers
