// Pillar 2 — area + room transition announcements.
//
// Poll-based. Each Tick: compare area pointer + room index against last
// observed; speak on delta. First observation after player-load also
// speaks (gives orientation on game-load / DLL-injection).
//
// Pre-load destination announce ("Loading: <module>") goes through the
// OnSetMoveToModuleString detour (engine entry @0x004aecd0).
//
// Speech uses interrupt=false — transitions don't talk over in-flight
// cycle / interact / passive_narrate.

#pragma once

#include "engine_offsets.h"

namespace acc::transitions {

void Tick();

// exoStringPtr = engine's CExoString* (destination resref). SEH-guarded.
// Dedups re-fires within a transition (the engine occasionally normalises
// the destination and re-emits the call).
void AnnouncePreLoadDestination(void* exoStringPtr);

// Closest landmark waypoint to pos within rangeM. Cache rebuilds on each
// area-change by scanning CSWSWaypoint with map_note_enabled. NOT keyed
// by .lyt-room — K1's .lyt-rooms over-segment into slivers (94-98%
// per-step flip rate); proximity scan over the flat list avoids that.
//
// Shared by cursors (map, view-mode, marker auto-name) and AltGr degrees.
bool FindLandmarkNear(const Vector& pos, float rangeM,
                      char* nameOut, size_t nameBufSize,
                      Vector& posOut,
                      int* outLandmarkIdx = nullptr);

// Walk the cache. cursor=0 on first call; advanced past each populated
// slot. landmarkIdx is the opaque key for MarkLandmarkClaimedByDoor.
bool IterateLandmarks(int& cursor,
                      char* nameOut, size_t nameBufSize,
                      Vector& posOut, int& outLandmarkIdx);

// wall_topology embedded this landmark into a cluster label — suppress
// the per-tick proximity announce so the same name doesn't fire twice
// within a second. Cleared per area rebuild.
void MarkLandmarkClaimedByDoor(int landmarkIdx);

// Vanilla CSWSArea.room_names[] entries are .lyt-room ids ("m01aa_10",
// "stunt_03_main"); mods occasionally supply friendly strings. Returns
// true for resref-style noise so callers fall through to other tiers.
bool IsResrefStyleRoomName(const char* name);

// True iff in-world ambient announces (room transitions, region cursor)
// should stay silent this tick. Combat-active or UI-blocking panel.
// Area-name transitions bypass the gate — the player just changed
// module, the cue is always worth it.
bool IsWorldSpeechGated();

// True between SetMoveToModuleString firing and the next fresh area
// pointer surfacing. Probes that call into player/leader/area accessors
// must short-circuit during this window — the engine tears down the old
// module's CResRef arenas while staging the new one, and probing old
// state can trigger use-after-free in CLYT::LoadLayout. The
// GetPlayerPosition gate alone doesn't cover this (old module is still
// alive).
bool IsModuleLoadPending();

}  // namespace acc::transitions
