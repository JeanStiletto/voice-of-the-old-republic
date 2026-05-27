// Audio beacon — drives Pillar 1 3D cues along a pathfinder waypoint
// sequence. Pillar 3 Mode B's "audio guide" half.
//
// Cues:
//   BeaconActive             — heartbeat between waypoints, 3D-positioned
//                              at the NEXT waypoint so pan/attenuation
//                              point the user that way.
//   BeaconWaypointReached    — on reaching next waypoint; advances cursor.
//   BeaconDestinationReached — after the final waypoint; disarms.
//
// First cut is single-area only. If the destination is a transition,
// the engine swaps modules when the player walks through it; beacon
// state goes stale and the user re-arms in the new area.
//
// Singleton — StartBeacon supersedes any prior. Ctrl+- toggles.

#pragma once

#include <vector>

#include "engine_offsets.h"

namespace acc::guidance::beacon {

// 3.0m: tighter values leave the beacon stuck as the walkmesh routes
// the player past corners without ever crossing <1.5m of the node.
constexpr float kReachToleranceMeters = 3.0f;

// 800ms ≈ one cue per 1.6m at walk speed — steady "follow me" pulse.
constexpr unsigned int kHeartbeatMs = 800;

// Empty `waypoints` cancels. First cue fires on next Tick so the user
// hears the arm without waiting for cadence.
void StartBeacon(const std::vector<Vector>& waypoints);

void CancelBeacon();  // idempotent

bool IsActive();

// World position of the waypoint currently being steered toward (false
// when no beacon armed / path drained). Used by camera_orient.
bool GetCurrentTarget(Vector& out);

// Per-tick driver. Cheap idle (one bool check). Self-gates on player
// position resolvable; un-load mid-flight silently disarms.
void Tick();

}  // namespace acc::guidance::beacon
