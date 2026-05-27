#include "audio_cue_player.h"

#include <cmath>

#include "audio_bus.h"
#include "audio_cues.h"
#include "core_settings.h"
#include "log.h"
#include "menus_modsettings.h"

namespace acc::audio {

namespace {

// Maps a NavCue to its short log label. Hardcoded English per the project
// rule that logs stay English (only user-facing speech routes through
// strings.h).
const char* CueLabel(NavCue cue) {
    switch (cue) {
        case NavCue::Door:                     return "Door";
        case NavCue::NpcCreature:              return "Npc";
        case NavCue::ContainerPlaceable:       return "Container";
        case NavCue::Item:                     return "Item";
        case NavCue::Landmark:                 return "Landmark";
        case NavCue::TransitionExit:           return "Transition";
        case NavCue::Wall:                     return "Wall";
        case NavCue::HazardLedge:              return "Hazard";
        case NavCue::Collision:                return "Collision";
        case NavCue::BeaconActive:             return "BeaconActive";
        case NavCue::BeaconWaypointReached:    return "BeaconWp";
        case NavCue::BeaconDestinationReached: return "BeaconDest";
    }
    return "?";
}

// Returns true if `cue` is enabled in the current settings. Pillar 1
// vocabulary cues are toggled per-kind; cues outside that vocabulary
// (guidance / view-mode signals) always pass.
bool IsCueEnabled(NavCue cue) {
    const auto& p1 = acc::core::Get().pillar1;
    switch (cue) {
        case NavCue::Wall:
            // AND of plan-locked Pillar 1 toggle and the user-facing
            // Mod Settings → Wall sounds switch. Either OFF silences
            // every wall fire site (T1 sector beats + T2 foremost
            // cone), since spatial_change_detector funnels them all
            // through PlayCueAtPosition.
            return p1.cueWall &&
                   acc::menus::modsettings::GetToggle(
                       acc::menus::modsettings::Option::WallSounds);
        case NavCue::HazardLedge:        return p1.cueHazard;
        case NavCue::Door:               return p1.cueDoor;
        case NavCue::NpcCreature:        return p1.cueNpc;
        case NavCue::ContainerPlaceable: return p1.cuePlaceable;
        case NavCue::Item:               return p1.cueItem;
        // Landmark cue is permanently disabled — landmarks announce via
        // TTS only (user-feedback 2026-05-27). Returning false here drops
        // the spatial-detector fire cleanly with reason=disabled instead
        // of letting it fall through to PlayCue3D with an empty resref
        // (which would log drop-engine-fail). cueLandmark in
        // Pillar1Settings stays around as inert state for now.
        case NavCue::Landmark:           return false;
        case NavCue::TransitionExit:     return p1.cueTransition;
        // Non-Pillar-1 cues — guidance / view-mode signals. Always pass;
        // their owning subsystems (Pillar 3 beacon, Pillar 2 view mode)
        // own their own enable-toggles upstream.
        case NavCue::Collision:
        case NavCue::BeaconActive:
        case NavCue::BeaconWaypointReached:
        case NavCue::BeaconDestinationReached:
            return true;
    }
    return false;
}

// 3D Euclidean distance squared. Pillar 1 distance gate uses 3D because the
// engine's audio pan/falloff already operate on 3D listener pose — gating
// on 2D would let above/below cues leak through inconsistently.
float DistanceSquared(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

}  // namespace

bool PlayCueAtPosition(NavCue cue,
                       const Vector& worldPos,
                       const Vector& listenerPos,
                       float rangeMax) {
    // Gate 1 — per-kind toggle.
    if (!IsCueEnabled(cue)) {
        acclog::Write("CuePlayer", "drop cue=%s reason=disabled",
                      CueLabel(cue));
        return false;
    }

    // Gate 2 — awareness range. Compare squared distance against squared
    // range to skip the sqrt on the cold path.
    float distSq  = DistanceSquared(worldPos, listenerPos);
    float rangeSq = rangeMax * rangeMax;
    if (distSq > rangeSq) {
        acclog::Write("CuePlayer", "drop cue=%s reason=out-of-range "
            "distSq=%.2f rangeSq=%.2f",
            CueLabel(cue), distSq, rangeSq);
        return false;
    }

    // All gates passed — hand off to audio_bus. Volume gain is centralised
    // in kAccCueGain (audio_bus.h) so all accessibility cue paths match.
    bool ok = PlayCue3D(GetNavCueResref(cue), worldPos);

    // sqrt only on the log path.
    float dist = (distSq > 0.0f) ? std::sqrt(distSq) : 0.0f;
    acclog::Write("CuePlayer", "%s cue=%s pos=(%.2f,%.2f,%.2f) dist=%.2f",
        ok ? "play" : "drop-engine-fail", CueLabel(cue),
        worldPos.x, worldPos.y, worldPos.z, dist);
    return ok;
}

}  // namespace acc::audio
