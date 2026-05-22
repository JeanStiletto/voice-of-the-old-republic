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

#include "engine_offsets.h"  // Vector

namespace acc::transitions {

// Per-tick poll. Self-gates on GetPlayerPosition / GetCurrentArea
// succeeding. Cheap when idle (one pointer compare + GetRoom call per
// tick).
void Tick();

// Pre-load destination announce — called from the
// `OnSetMoveToModuleString` detour (CServerExoApp::SetMoveToModuleString
// @ 0x004aecd0) at the start of the area-transition pipeline. The
// engine has just been told *which module the player is moving to*,
// before the loading-screen movie begins. We read the module resref
// out of the supplied CExoString and queue a "Lade: …" / "Loading: …"
// announce via Tolk so the user knows what's coming.
//
// `exoStringPtr` is the raw `CExoString*` arg the engine was called
// with — i.e. the destination module's resref (e.g. `"endar_spire"`,
// `"tar_m02ac"`). Layout: `{ char* c_string; uint32 length }`. SEH-
// guarded internally; null / empty / faulting reads silently no-op.
//
// Suppresses re-announce within a small dedup window — the engine
// occasionally re-fires SetMoveToModuleString inside a single
// transition (e.g. when the destination is normalized). Without
// dedup the user would hear "Lade: endar_spire" twice.
void AnnouncePreLoadDestination(void* exoStringPtr);

// Returns the cached Bioware-authored landmark label (e.g. "Bridge",
// "Crew Quarters", localized to the active language) covering the given
// layout-room, or nullptr if no landmark waypoint is registered for
// that room. The cache is populated on each area-change by scanning the
// area's CSWSWaypoint objects with `has_map_note != 0` AND
// `map_note_enabled != 0` (engine fog-of-war filter), resolving each to
// a layout-room via GetRoomAtIndexed.
//
// First-come wins on collision (multiple landmarks per room rare).
// Returned pointer is stable until the next area-change rebuild.
// Out-of-range or unmapped room indices return nullptr.
//
// Public so the map-cursor (and any future Pillar 2/3 consumer) can
// reuse the same cache the in-world room-transition path builds.
const char* GetLandmarkForRoom(int roomIdx);

// Look up the world-space position of the landmark waypoint that
// supplied `roomIdx`'s label. Returns true and writes `outPos` when a
// position is recorded for the room (cache stores it alongside the
// label since the 2026-05-13 proximity gate). Returns false for rooms
// without a landmark or when the position read faulted at cache build.
// Walking and view-mode consumers use this to suppress the landmark
// tier when the player / cursor is far from the actual waypoint —
// .lyt-room partitions can stretch 50m across thin transition strips,
// so "same room" alone over-fires the landmark.
bool GetLandmarkPositionForRoom(int roomIdx, Vector& outPos);

// Walk the landmark cache. Pass `cursor` = 0 on the first call; the
// callee advances it past the next populated slot and writes that
// slot's name + position + room index. Returns false when no more
// landmarks remain. Used by wall_topology::BuildForArea to match each
// landmark against the door snapshot and embed the landmark name in
// cluster labels.
bool IterateLandmarks(int& cursor,
                      char* nameOut, size_t nameBufSize,
                      Vector& posOut, int& outRoomIdx);

// Flag the landmark in room `roomIdx` as "claimed" — wall_topology has
// just embedded its name into a cluster label, so the per-tick
// proximity-fire path in TickProximityLandmarks must NOT also announce
// the bare landmark name (would duplicate the same word twice within
// a second of each other). Idempotent. Cleared on each area-change
// rebuild.
void MarkLandmarkClaimedByDoor(int roomIdx);

// Heuristic: vanilla KOTOR content stores `CSWSArea.room_names[]` as
// the .lyt-room identifier (`m01aa_10`, `stunt_03_main`, `unk_m13ab`)
// — pronounceable but meaningless. Returns true for those resref-style
// tokens (starts with `m\d` / `Stunt`, OR contains an underscore, OR
// is empty/null). Returns false for human-readable strings that mods
// occasionally supply for their custom rooms ("Bridge", "MainHangar").
//
// Public so the map-cursor's ambient announce can reuse the same
// filter the room-transition path uses — both want to surface mod-
// supplied friendly names while hiding vanilla layout-id noise.
bool IsResrefStyleRoomName(const char* name);

// True iff in-world ambient announcements (room transitions, view-mode
// region cursor) should stay silent this tick. Currently gates on:
//   - `acc::combat::IsCombatActive()` (no distractions mid-fight)
//   - `acc::engine::IsForegroundUiBlocking()` (panels / modals / dialog)
// Area-name transitions intentionally bypass this gate — the player just
// changed module, the orientation cue is always worth speaking.
//
// Shared by the walking-adapter (transitions::Tick) and the view-mode
// region-announce so both surfaces have identical "is the player ready
// to listen?" semantics — only one predicate to maintain.
bool IsWorldSpeechGated();

}  // namespace acc::transitions
