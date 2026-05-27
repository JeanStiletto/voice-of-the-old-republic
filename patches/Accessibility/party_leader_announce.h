// Tab leader-change announce.
//
// Speaks the new controlled creature's name on Tab rising-edge.
// Repetition is intentional — in solo mode Tab on the same creature
// still speaks, confirming "still solo" rather than swallowing the press.
//
// Fires in panels too — engine strip panels re-bind to the new leader,
// so the announce is wanted in both world and UI contexts.

#pragma once

namespace acc::party_leader_announce {

// Foreground + player-loaded gates inside.
void Tick();

}  // namespace acc::party_leader_announce
