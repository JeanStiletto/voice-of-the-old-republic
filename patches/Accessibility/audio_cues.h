// Navigation-cue vocabulary — resref mapping for the audio slots.
//
// Data-only header. Consumers map enum → resref via GetNavCueResref then
// call audio::PlayCue / PlayCue3D.
//
// Swapping a resref: change the string literal on the case below. CResRef
// has a 16-char hard limit (silent truncation past that → resolution miss
// → silent cue). Resolution is case-insensitive through
// Override → streamwaves → streamsounds → streammusic → BIF/RIM, so an
// Override/ WAV with the same resref transparently shadows the stock asset.
//
// Verify a resref resolves before using it: Play3DOneShotSound silently
// no-ops on a miss. build/sounds-extracted-full/ is the truth table.

#pragma once

namespace acc::audio {

enum class NavCue {
    // Per-kind cues — six map to Pillar 4 categories. Doors split by
    // open_state + material at fire time (the engine has dr_*_lock samples
    // for closed material but no analogous open-state material variants).
    DoorOpen,
    DoorClosedMetal,
    DoorClosedWood,
    DoorClosedStone,
    NpcCreature,
    ContainerPlaceable,
    Item,
    Landmark,
    TransitionExit,
    Wall,
    HazardLedge,
    // Guidance + view-mode signals.
    Collision,
    BeaconActive,
    BeaconWaypointReached,
    BeaconDestinationReached,
};

constexpr const char* GetNavCueResref(NavCue cue) {
    switch (cue) {
        case NavCue::DoorOpen:                 return "gui_close";
        case NavCue::DoorClosedMetal:          return "dr_metal_lock";
        case NavCue::DoorClosedWood:           return "dr_wood_lock";
        case NavCue::DoorClosedStone:          return "dr_stone_lock";
        case NavCue::NpcCreature:              return "fs_metal_droid2";
        case NavCue::ContainerPlaceable:       return "gui_invadd";
        case NavCue::Item:                     return "gui_invselect";
        // Silent — landmarks announce via TTS only. Empty resref
        // short-circuits PlayCue3D without an engine call.
        case NavCue::Landmark:                 return "";
        case NavCue::TransitionExit:           return "gui_quest";
        case NavCue::Wall:                     return "as_nt_wtrdrip_09";
        case NavCue::HazardLedge:              return "cb_sw_bldlrg1";
        case NavCue::Collision:                return "gui_invdrop";
        case NavCue::BeaconActive:             return "gui_check";
        case NavCue::BeaconWaypointReached:    return "gui_prompt";
        case NavCue::BeaconDestinationReached: return "gui_complete";
    }
    return "";
}

}  // namespace acc::audio
