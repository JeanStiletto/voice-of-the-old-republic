#include "guidance_beacon.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

#include "audio_bus.h"          // PlayCue (2D) for arrival confirmations
#include "audio_cue_player.h"   // PlayCueAtPosition (3D) for heartbeat
#include "audio_cues.h"
#include "engine_compass.h"
#include "engine_player.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

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

// Heartbeat gain — boosted 2× over the default kAccCueGain so the
// directional cue carries against ambient + Pillar 1 voice-budget
// pressure at 15–25m distances (where the engine's falloff curve
// significantly attenuates). 8.0× scalar = +18 dB vs unity; we don't
// pass through audio_cue_player here because its 80m range gate is
// always satisfied for our use and we want explicit volume control.
constexpr float kBeaconHeartbeatGain = 8.0f;

// Fire the directional heartbeat. 3D-positional so the user can
// localise the next waypoint by pan + falloff. Bypasses
// audio_cue_player to pass explicit volume.
void EmitHeartbeat(const Vector& worldPos, const Vector& listenerPos) {
    bool ok = acc::audio::PlayCue3D(
        acc::audio::GetNavCueResref(acc::audio::NavCue::BeaconActive),
        worldPos, kBeaconHeartbeatGain);
    acclog::Write("Beacon", "heartbeat at=(%.2f,%.2f,%.2f) "
                  "listener=(%.2f,%.2f,%.2f) gain=%.1f ok=%d",
                  worldPos.x, worldPos.y, worldPos.z,
                  listenerPos.x, listenerPos.y, listenerPos.z,
                  kBeaconHeartbeatGain, ok ? 1 : 0);
}

// Fire an arrival confirmation. 2D-centred (not 3D-positional) — the
// user IS at the waypoint, so direction information is moot, and 3D pan
// at the listener position would attenuate against ambient + Pillar 1
// audio under voice-budget pressure. 2D plays at unity, full centred
// stereo, which the user always hears cleanly.
void EmitArrivalCue(acc::audio::NavCue cue, const char* tag) {
    bool ok = acc::audio::PlayCue(acc::audio::GetNavCueResref(cue));
    acclog::Write("Beacon", "%s cue=%d (2D) ok=%d",
                  tag, static_cast<int>(cue), ok ? 1 : 0);
}

constexpr float kRadToDeg = 57.29577951308232f;

// Speak the current segment's distance + compass direction. Called
// after each waypoint reach so the user doesn't have to keep the full
// multi-segment description in working memory: they hear "Weiter 20
// Meter Westen" exactly when they need to act on it. Uses the same
// compass-sector math as guidance_description so on-route announcements
// match the opening overview's vocabulary.
//
// interrupt=false so this queues behind the just-emitted 2D arrival
// cue and any in-flight Pillar 1 / passive narration. The user hears:
//   <arrival cue> -> <Weiter X Meter Y> -> <heartbeat resumes>
void SpeakNextSegment(const Vector& playerPos, const Vector& nextWp) {
    float dx = nextWp.x - playerPos.x;
    float dy = nextWp.y - playerPos.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    int metres = static_cast<int>(distance + 0.5f);
    if (metres < 1) metres = 1;

    float engineYaw = std::atan2(dy, dx) * kRadToDeg;
    float compass   = acc::engine::EngineYawToCompass(engineYaw);
    int   sector    = acc::engine::CompassToSector(compass);
    const char* dir = acc::strings::Get(acc::engine::SectorString(sector));

    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtBeaconNextSegment),
                  metres, dir);
    tolk::Speak(msg, /*interrupt=*/false);
    acclog::Write("Beacon", "next-segment -> [%s] dist=%.2fm sector=%d",
                  msg, distance, sector);
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

bool GetCurrentTarget(Vector& out) {
    if (!g_state.active) return false;
    if (g_state.nextIdx >= g_state.path.size()) return false;
    out = g_state.path[g_state.nextIdx];
    return true;
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

    // Reach check — fire the per-waypoint cue and advance. Arrival cues
    // are 2D-centred so they're always audible regardless of camera pan
    // or Pillar 1 voice-budget pressure.
    if (dist < kReachToleranceMeters) {
        bool isFinal = (g_state.nextIdx + 1 == g_state.path.size());
        if (isFinal) {
            EmitArrivalCue(acc::audio::NavCue::BeaconDestinationReached,
                           "destination-reached");
            acclog::Write("Beacon", "reached destination (idx=%zu of %zu)",
                          g_state.nextIdx, g_state.path.size());
            CancelBeacon();
            return;
        }
        EmitArrivalCue(acc::audio::NavCue::BeaconWaypointReached,
                       "waypoint-reached");
        acclog::Write("Beacon", "reached waypoint %zu of %zu, advancing",
                      g_state.nextIdx, g_state.path.size());
        g_state.nextIdx++;
        // Re-announce the new current segment (distance + direction) so
        // the user has a fresh single-segment direction in mind instead
        // of having to recall it from the opening description.
        SpeakNextSegment(playerPos, g_state.path[g_state.nextIdx]);
        // Reset heartbeat clock so the next waypoint's heartbeat fires
        // on the very next tick — gives the user immediate spatial
        // re-anchoring after a reach event.
        g_state.lastHeartbeat = GetTickCount() - kHeartbeatMs;
        return;
    }

    // Heartbeat cadence — fire the directional "follow me" cue at the
    // next waypoint every kHeartbeatMs. 3D-positional so the user can
    // localise the next hop by pan + falloff against the camera-anchored
    // listener.
    DWORD now = GetTickCount();
    if (now - g_state.lastHeartbeat >= kHeartbeatMs) {
        EmitHeartbeat(targetWp, playerPos);
        g_state.lastHeartbeat = now;
    }
}

}  // namespace acc::guidance::beacon
