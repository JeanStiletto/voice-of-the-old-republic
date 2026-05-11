// Audio beacon — consume a pathfinder waypoint sequence, drive Pillar 1
// 3D cues as the player walks.
//
// Layer: guidance/ (depends on guidance_pathfind, audio_cue_player,
// engine_player). Phase 5 lay-off 3 — the "audio guide" half of Pillar 3
// Mode B. The user dispatches Ctrl+- to start a beacon to the
// currently-cycled object; per-tick we read player position and emit:
//
//   - BeaconActive             : at every kHeartbeatMs while between
//                                waypoints — the "this way" cue. Emitted at
//                                the *next* waypoint's world position so
//                                3D pan + attenuation point the user
//                                toward the next hop.
//   - BeaconWaypointReached    : on reaching the next waypoint (player is
//                                within kReachToleranceMeters of it).
//                                Advances the cursor to the following waypoint.
//   - BeaconDestinationReached : after the final waypoint. Disarms.
//
// Cross-area: this first cut does NOT handle area transitions. The path
// was computed entirely within the current area; if the destination is a
// transition trigger, the engine will swap modules when the player walks
// through it, the current beacon state goes stale, and the user re-fires
// Ctrl+- in the new area to set a fresh beacon. Multi-area pathfinding
// is future work (docs/navsystem-progress.md Phase 5 lay-off plan).
//
// Singleton state: only one beacon active at a time. StartBeacon
// supersedes any prior; CancelBeacon clears state and silences the
// heartbeat. Ctrl+- toggles via the cycle_input wiring.

#pragma once

#include <vector>

#include "engine_offsets.h"  // Vector

namespace acc::guidance::beacon {

// Reach tolerance — the horizontal distance below which a waypoint is
// considered "reached". 3.0m gives margin for "the walkmesh routes me
// past the waypoint without going through its exact node position" —
// observed live with a corridor-turn waypoint that the player walked
// within ~2m of but never <1.5m (log patch-20260511-163145). Tighter
// values left the beacon stuck on waypoint N forever as the player
// grazed past it.
constexpr float kReachToleranceMeters = 3.0f;

// Heartbeat cadence — how often the BeaconActive cue fires while between
// waypoints. 800ms = roughly one cue per 1.6m at KOTOR walk speed, which
// reads as a steady "follow me" pulse without spam.
constexpr unsigned int kHeartbeatMs = 800;

// Start a beacon with the supplied waypoint sequence (typically the
// output of guidance::ComputePath plus the original goal). Replaces
// any prior beacon. Empty `waypoints` cancels (same as CancelBeacon).
//
// The first heartbeat cue fires on the next Tick() so the user hears
// the beacon armed immediately without waiting for the cadence to
// elapse.
void StartBeacon(const std::vector<Vector>& waypoints);

// Silence + disarm. Idempotent — calling when no beacon is armed is a
// no-op. Caller speaks any localised "Beacon cancelled" phrase
// separately.
void CancelBeacon();

// Reports whether a beacon is currently armed. Used by cycle_input's
// Ctrl+- toggle to choose between StartBeacon (no beacon active) and
// CancelBeacon (already active).
bool IsActive();

// Per-tick driver. Reads player position, fires waypoint-reached /
// destination-reached cues, schedules the next heartbeat. Cheap when
// idle (one bool check). Self-gates on player-position resolvable —
// if the player un-loads mid-flight (area transition, save reload)
// the beacon silently disarms.
//
// Call from acc::tick::Dispatch after acc::guidance::TickProgressWatchdog
// so the beacon's arrival check runs in the same frame the autowalk
// watchdog updates (they don't share state but both read player pos
// and consumers grep these in chronological order).
void Tick();

}  // namespace acc::guidance::beacon
