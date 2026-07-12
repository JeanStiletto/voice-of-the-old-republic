// Door-open facing readout.
//
// KOTOR autoturns the player to face whatever they interact with, so after
// opening a door the player has often been rotated (and walked) away from
// where they thought they were pointing. This announces the player's current
// camera facing the moment a door BEGINS to open, so the user re-establishes
// their bearing (and, implicitly, where they came from).
//
// Signal: the CSWSDoor::OpenDoor detour fires at the start of the open (before
// SetOpenState kicks off the animation), handing us the opener's server object
// id. We only react to doors the player-controlled leader opens — a follower /
// enemy / script opening a door nearby must not fire a readout.
//
// De-noising lives in camera_announce::AnnounceCurrentFacing: the same sector
// spoken by the per-tick direction-change announce within the last second is
// suppressed, so a close door the player already got a direction cue for stays
// quiet, while a door reached after walking/turning speaks.

#pragma once

#include <cstdint>

namespace acc::door_announce {

// Called from the OpenDoor detour (main thread) with the opener's SERVER
// object id. Records it + a timestamp for the next Tick; no engine reads or
// speech here.
void NoteDoorOpened(uint32_t openerServerId);

// Per-tick consumer. Must run AFTER camera_announce::Tick so the dedup sees
// this frame's last-spoken sector. Cheap when idle (one flag check).
void Tick();

}  // namespace acc::door_announce
