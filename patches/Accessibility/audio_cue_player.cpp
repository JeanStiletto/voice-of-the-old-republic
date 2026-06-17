#include "audio_cue_player.h"

#include <cmath>

#include "audio_bus.h"
#include "audio_cues.h"
#include "core_settings.h"
#include "log.h"
#include "menus_modsettings.h"

namespace acc::audio {

namespace {

// English log labels — logs stay English by project rule.
const char* CueLabel(NavCue cue) {
    switch (cue) {
        case NavCue::DoorOpen:                 return "DoorOpen";
        case NavCue::DoorClosedMetal:          return "DoorClosedMetal";
        case NavCue::DoorClosedWood:           return "DoorClosedWood";
        case NavCue::DoorClosedStone:          return "DoorClosedStone";
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

bool IsCueEnabled(NavCue cue) {
    const auto& p1 = acc::core::Get().pillar1;
    switch (cue) {
        case NavCue::Wall:
            // AND with the user-facing Mod Settings → Wall sounds switch:
            // either OFF silences every wall fire (T1 sectors + T2 cone)
            // since they all funnel through here.
            return p1.cueWall &&
                   acc::menus::modsettings::GetToggle(
                       acc::menus::modsettings::Option::WallSounds);
        case NavCue::HazardLedge:        return p1.cueHazard;
        // All door sub-cues share cueDoor — the user thinks about "doors"
        // as one category; the open/material split happens at fire time.
        case NavCue::DoorOpen:
        case NavCue::DoorClosedMetal:
        case NavCue::DoorClosedWood:
        case NavCue::DoorClosedStone:    return p1.cueDoor;
        case NavCue::NpcCreature:        return p1.cueNpc;
        case NavCue::ContainerPlaceable: return p1.cuePlaceable;
        case NavCue::Item:               return p1.cueItem;
        // Permanently off — landmarks announce via TTS only. Returning
        // false drops cleanly here instead of falling through to PlayCue3D
        // with an empty resref. cueLandmark stays in settings as inert.
        case NavCue::Landmark:           return false;
        case NavCue::TransitionExit:     return p1.cueTransition;
        // Guidance / view-mode signals — owners toggle upstream.
        case NavCue::Collision:
        case NavCue::BeaconActive:
        case NavCue::BeaconWaypointReached:
        case NavCue::BeaconDestinationReached:
            return true;
    }
    return false;
}

// 3D matches the engine's pan/falloff; 2D would leak above/below cues.
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
    if (!IsCueEnabled(cue)) {
        acclog::Write("CuePlayer", "drop cue=%s reason=disabled",
                      CueLabel(cue));
        return false;
    }

    // Compare squared distances to skip sqrt on the cold path.
    float distSq  = DistanceSquared(worldPos, listenerPos);
    float rangeSq = rangeMax * rangeMax;
    if (distSq > rangeSq) {
        acclog::Write("CuePlayer", "drop cue=%s reason=out-of-range "
            "distSq=%.2f rangeSq=%.2f",
            CueLabel(cue), distSq, rangeSq);
        return false;
    }

    // Passive proximity cues ride the near-field "spatial" group (tight 1m/8m
    // falloff) so loudness tracks distance across the awareness range. This is
    // the funnel for the world cues only (wall/door/container/NPC/item/
    // transition/hazard); the on-demand cycling cues call PlayCue3D directly and
    // stay on the flat full group, where a distant target must remain audible.
    bool ok = PlayCue3D(GetNavCueResref(cue), worldPos,
                        GetSpatialCuePriorityGroup());

    float dist = (distSq > 0.0f) ? std::sqrt(distSq) : 0.0f;
    acclog::Write("CuePlayer", "%s cue=%s pos=(%.2f,%.2f,%.2f) dist=%.2f",
        ok ? "play" : "drop-engine-fail", CueLabel(cue),
        worldPos.x, worldPos.y, worldPos.z, dist);
    return ok;
}

}  // namespace acc::audio
