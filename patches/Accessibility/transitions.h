// Pillar 2 — area + room transition announcements.
//
// Layer: narration/ (consumes engine_player + engine_area + strings + prism;
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
// in-flight cycle / interact / passive_narrate announcement. Prism
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
// announce via Prism so the user knows what's coming.
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

// Find the closest landmark waypoint to `pos` within `rangeM` metres.
// On success returns true and writes the landmark text (null-terminated,
// truncated to `nameBufSize - 1`) plus its world position. The cache is
// populated on each area-change by scanning CSWSWaypoint objects with
// `has_map_note != 0` AND `map_note_enabled != 0` (engine fog-of-war
// filter) and storing them in a flat array — not keyed by .lyt-room, on
// purpose. K1's .lyt-rooms over-segment into sliver shapes (94-98 %
// per-step flip rate in dense Taris areas), so a cursor / player /
// marker position inside one sliver routinely fails to find a landmark
// stored under the adjacent sliver. Proximity scan over the flat list
// avoids that pathology.
//
// Returned pointer-equivalent text is copied into the caller's buffer,
// so callers don't need to hold the cache lifetime.
//
// Public so cursors (map, view-mode, marker auto-name) and the in-world
// AltGr degrees announce reuse the same cache the walking-adapter
// builds.
bool FindLandmarkNear(const Vector& pos, float rangeM,
                      char* nameOut, size_t nameBufSize,
                      Vector& posOut,
                      int* outLandmarkIdx = nullptr);

// Walk the landmark cache. Pass `cursor` = 0 on the first call; the
// callee advances it past the next populated slot and writes that
// slot's name, position, and a landmark-index handle. Returns false
// when no more landmarks remain. The landmark-index handle is the
// opaque key the caller passes back to `MarkLandmarkClaimedByDoor` when
// a door label takes ownership of the landmark's name.
bool IterateLandmarks(int& cursor,
                      char* nameOut, size_t nameBufSize,
                      Vector& posOut, int& outLandmarkIdx);

// Flag landmark `landmarkIdx` as "claimed" — wall_topology has just
// embedded its name into a cluster label, so the per-tick proximity-
// fire path in TickProximityLandmarks must NOT also announce the bare
// landmark name (would duplicate the same word twice within a second
// of each other). Idempotent. Out-of-range indices are silently
// ignored. Cleared on each area-change rebuild.
void MarkLandmarkClaimedByDoor(int landmarkIdx);

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

// True from the moment `CServerExoApp::SetMoveToModuleString` fires
// (engine has been told a module transition is starting) until
// `transitions::Tick` next observes a fresh area pointer (new module
// finished loading and surfacing through `GetCurrentArea`). Per-tick
// probes that call into engine accessors on player / leader / area-
// object state should short-circuit while this is true — the engine is
// tearing down the previous module's `CResRef` arenas while preparing
// the new one, and probing the old state into that handoff can perturb
// the engine's loader and trigger a use-after-free deep inside
// `CLYT::LoadLayout` (newest crash: `swkotor.exe(1).23224.dmp`,
// `_strlen+0x30` ← `CLYT::LoadLayout+0x117` on a decommitted resref
// page during the stunt_03a / stunt_levbridge load). The existing
// `GetPlayerPosition` gate doesn't cover this window — the old module
// is still alive, so the gate keeps returning true straight through
// the transient.
//
// Latches on the entry-hook `OnSetMoveToModuleString` and clears on
// area-pointer change (or on player-loss reset). First-module case
// stays false until SetMoveToModuleString actually fires.
bool IsModuleLoadPending();

}  // namespace acc::transitions
