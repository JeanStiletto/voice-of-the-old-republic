#include "guidance_beacon.h"

#include <windows.h>
#include <cmath>

#include "audio_cue_player.h"
#include "audio_cues.h"
#include "engine_player.h"
#include "log.h"

namespace acc::guidance::beacon {

namespace {

struct BeaconState {
    std::vector<Vector> path;
    size_t              nextIdx        = 0;
    DWORD               lastHeartbeat  = 0;
    bool                active         = false;
};
BeaconState g_state;

float DistXY(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Audio range for beacon cues. Beacon* are non-Pillar-1 cues so
// audio_cue_player::IsCueEnabled always allows them; the range gate
// still applies. Use a generous cap — the user *wants* to hear the
// beacon even when the next waypoint is across the room.
constexpr float kBeaconRangeMeters = 80.0f;

void EmitCue(acc::audio::NavCue cue, const Vector& worldPos,
             const Vector& listenerPos, const char* tag) {
    bool ok = acc::audio::PlayCueAtPosition(cue, worldPos, listenerPos,
                                            kBeaconRangeMeters);
    acclog::Write("Beacon", "%s cue=%d at=(%.2f,%.2f,%.2f) ok=%d",
                  tag, static_cast<int>(cue),
                  worldPos.x, worldPos.y, worldPos.z, ok ? 1 : 0);
}

}  // namespace

void StartBeacon(const std::vector<Vector>& waypoints) {
    if (waypoints.empty()) {
        CancelBeacon();
        return;
    }
    g_state.path          = waypoints;
    g_state.nextIdx       = 0;
    g_state.active        = true;
    // Set lastHeartbeat back in time so the first Tick fires the first
    // heartbeat immediately rather than waiting kHeartbeatMs.
    g_state.lastHeartbeat = GetTickCount() - kHeartbeatMs;
    acclog::Write("Beacon", "StartBeacon armed waypoints=%zu",
                  g_state.path.size());
}

void CancelBeacon() {
    if (!g_state.active) return;
    g_state.path.clear();
    g_state.nextIdx       = 0;
    g_state.active        = false;
    g_state.lastHeartbeat = 0;
    acclog::Write("Beacon", "CancelBeacon — disarmed");
}

bool IsActive() {
    return g_state.active;
}

void Tick() {
    if (!g_state.active) return;

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        // Player un-loaded mid-flight — silently disarm. The next
        // StartBeacon (user re-dispatches in the new area) rebuilds
        // state from scratch.
        acclog::Write("Beacon", "Tick: player un-loaded, auto-disarm");
        CancelBeacon();
        return;
    }

    if (g_state.nextIdx >= g_state.path.size()) {
        // Defensive — shouldn't happen (we disarm on final waypoint
        // below). Disarm and move on.
        acclog::Write("Beacon", "Tick: nextIdx=%zu out of range (path.size=%zu) "
                      "— auto-disarm",
                      g_state.nextIdx, g_state.path.size());
        CancelBeacon();
        return;
    }

    const Vector& targetWp = g_state.path[g_state.nextIdx];
    float dist = DistXY(playerPos, targetWp);

    // Reach check — fire the per-waypoint cue and advance.
    if (dist < kReachToleranceMeters) {
        bool isFinal = (g_state.nextIdx + 1 == g_state.path.size());
        if (isFinal) {
            EmitCue(acc::audio::NavCue::BeaconDestinationReached, targetWp,
                    playerPos, "destination-reached");
            acclog::Write("Beacon", "reached destination (idx=%zu of %zu)",
                          g_state.nextIdx, g_state.path.size());
            CancelBeacon();
            return;
        }
        EmitCue(acc::audio::NavCue::BeaconWaypointReached, targetWp,
                playerPos, "waypoint-reached");
        acclog::Write("Beacon", "reached waypoint %zu of %zu, advancing",
                      g_state.nextIdx, g_state.path.size());
        g_state.nextIdx++;
        // Reset heartbeat clock so the next waypoint's heartbeat fires
        // on the very next tick — gives the user immediate spatial
        // re-anchoring after a reach event.
        g_state.lastHeartbeat = GetTickCount() - kHeartbeatMs;
        return;
    }

    // Heartbeat cadence — fire the "follow me" cue at the next waypoint
    // every kHeartbeatMs.
    DWORD now = GetTickCount();
    if (now - g_state.lastHeartbeat >= kHeartbeatMs) {
        EmitCue(acc::audio::NavCue::BeaconActive, targetWp, playerPos,
                "heartbeat");
        g_state.lastHeartbeat = now;
    }
}

}  // namespace acc::guidance::beacon
