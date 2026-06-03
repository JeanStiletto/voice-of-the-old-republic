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
#include "prism.h"

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

// ----- Heartbeat distance soft-knee (engine audibility) ---------------
//
// The engine's 3D attenuation curve is tuned for the ~10-20 m band that
// Pillar 1 nav cues + footsteps live in; a heartbeat fired at a waypoint
// 30-180 m out falls below the audible threshold (the same limit the
// swoop spatial cues hit — see swoop_spatial_audio.cpp). We pull the
// SOURCE in along the listener->waypoint ray so it lands inside the
// audible band. Radial scaling preserves the azimuth/elevation exactly,
// so the pan still points at the true waypoint — only the distance is
// bent, never the bearing.
//
// Unlike the swoop path's flat 1:9 compression (tuned for a fast bike
// over 180 m), the beacon uses a SOFT KNEE: within the audible band the
// source plays at its TRUE distance (no compression at all), so the final
// approach keeps a full, honest "getting warmer" loudness ramp right down
// to the reach event. Only the portion beyond the knee is compressed —
// asymptotically — into the narrow band between the knee and the audible
// ceiling. Far waypoints land "just audible" (direction + a faint "still
// a way to go"); the exact remaining metres are already spoken via
// SpeakNextSegment, so the shallow far-field gradient costs no info.
//
//   true <= 18 m  -> apparent = true        (untouched, full resolution)
//   true   50 m   -> apparent ~ 19.1 m
//   true  180 m   -> apparent ~ 20.0 m
//
// Listener is the player position: PlayCue3D applies a character-relative
// listener bias (shifts the source by camera-minus-character), so the
// engine's effective listener-to-source distance equals our
// player-to-source distance. Compressing toward the player therefore
// lands the cue in the audible band as actually heard.
constexpr float kHeartbeatKneeMeters     = 18.0f;  // audible-band knee
constexpr float kHeartbeatCeilingMeters  = 20.0f;  // engine audible edge
constexpr float kHeartbeatFarScaleMeters = 40.0f;  // asymptote rate past knee

Vector CompressHeartbeatPosition(const Vector& target, const Vector& listener) {
    float dx = target.x - listener.x;
    float dy = target.y - listener.y;
    float dz = target.z - listener.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Within the audible band (or degenerate distance) — play as-is so
    // the near field stays geometrically honest.
    if (dist <= kHeartbeatKneeMeters || dist <= 0.0f) {
        return target;
    }

    // Beyond the knee: compress only the excess into [knee, ceiling],
    // asymptotically approaching the ceiling so even very distant
    // waypoints stay just inside the audible edge without ever exceeding
    // it (and without a hard clamp flattening a whole far-field range).
    float excess   = dist - kHeartbeatKneeMeters;
    float band     = kHeartbeatCeilingMeters - kHeartbeatKneeMeters;
    float apparent = kHeartbeatKneeMeters +
                     band * (1.0f - std::exp(-excess / kHeartbeatFarScaleMeters));

    // Scale the source in along the listener->target ray. k = apparent/dist
    // is a pure radial scale, so direction (pan) is unchanged.
    float k = apparent / dist;
    Vector out;
    out.x = listener.x + dx * k;
    out.y = listener.y + dy * k;
    out.z = listener.z + dz * k;
    return out;
}

// Fire the directional heartbeat. 3D-positional so the user can localise
// the next waypoint by pan + falloff. Plays at full base volume (scaled
// by the global cue slider in audio_bus). Bypasses audio_cue_player
// because its 80m range gate would otherwise apply; the beacon wants to
// carry at any distance. The source is soft-knee compressed onto the
// engine's audible band (see CompressHeartbeatPosition) so it stays
// hearable past ~20 m without losing bearing or near-field resolution.
void EmitHeartbeat(const Vector& worldPos, const Vector& listenerPos) {
    Vector cuePos = CompressHeartbeatPosition(worldPos, listenerPos);
    bool ok = acc::audio::PlayCue3D(
        acc::audio::GetNavCueResref(acc::audio::NavCue::BeaconActive),
        cuePos);
    acclog::Write("Beacon", "heartbeat target=(%.2f,%.2f,%.2f) "
                  "cue=(%.2f,%.2f,%.2f) listener=(%.2f,%.2f,%.2f) ok=%d",
                  worldPos.x, worldPos.y, worldPos.z,
                  cuePos.x, cuePos.y, cuePos.z,
                  listenerPos.x, listenerPos.y, listenerPos.z,
                  ok ? 1 : 0);
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
    prism::Speak(msg, /*interrupt=*/false);
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
