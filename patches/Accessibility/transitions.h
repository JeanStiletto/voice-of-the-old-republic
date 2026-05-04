// Pillar 2 — area + room transition announcements.
//
// Layer: narration/ (consumes engine_player + engine_area + strings + tolk;
// no engine re-entry beyond engine_area's wrapped CSWSArea::GetRoom call).
// Phase 2 lay-off 7 (the user-facing slice of `announce/transitions` from
// docs/navsystem-longterm-plan.md §"Pillar 2 — Medium-scale navigation").
//
// Detection model — per-tick polling, no engine hooks:
//
//   prev_area_ptr = nullptr
//   prev_room_index = -1
//   each Tick:
//     pos  = GetPlayerPosition()              # gates: must have a player
//     area = GetCurrentArea()                 # gates: must have an area
//     if area != prev_area_ptr:               # area change (incl. first observe)
//       SpeakAreaName(area)
//       prev_area_ptr = area
//       prev_room_index = -1                  # force re-announce on new area
//     room_index = GetRoomAtIndexed(area, pos)
//     if room_index >= 0 && room_index != prev_room_index:
//       SpeakRoomName(area, room_index)
//       prev_room_index = room_index
//
// First-observation behavior is "speak" — when the player loads into a
// save (or the DLL is injected mid-game), the user hears their starting
// area + room name immediately. This is the same UX choice Pillar 4's
// auto-announce-on-cycle made: silence on initial state is unhelpful;
// always confirm where the player is.
//
// We deliberately do NOT hook CClientExoApp::AddMoveToModuleMovie
// @0x5edb50 in this lay-off (the longterm plan called it out for a
// "Loading: ..." pre-roll). The function entry is only 8 bytes which is
// tight for a clean 5-byte detour cut, and the post-load area-name
// announce already covers the core "you arrived in {area}" UX. Hook is
// parked as a follow-up; if user testing shows the player wants
// pre-load context (e.g. to understand why the screen just went black
// for 10s during a long load), revisit with a mid-function hook on the
// secondary range at 0x6027a0.
//
// Speech uses interrupt=false — transitions shouldn't talk over an
// in-flight cycle / interact / passive_narrate announcement. Tolk
// queues by default.
//
// Self-gating mirrors turn_announce / camera_announce: silent in menus,
// chargen, pre-spawn, between-area loads. The polling resets module
// state on player loss so a re-load picks up cleanly.

#pragma once

namespace acc::transitions {

// Per-tick poll. Self-gates on GetPlayerPosition / GetCurrentArea
// succeeding. Cheap when idle (one pointer compare + GetRoom call per
// tick).
void Tick();

}  // namespace acc::transitions
