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

#include "engine_game.h"  // IsKotor2 — GetNavCueResrefForGame

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
    // Swoop race minigame — playback paths in swoop_race.cpp and
    // swoop_spatial_audio.cpp use GetNavCueResref for these so the
    // glossary preview and the live race fire the same sample.
    SwoopAccelpadBoost,     // acc_boost (panned racing-line steering guide)
    SwoopObstacleWarn,      // cb_gr_boncehard2 (short metallic grenade-bounce;
                            //   looped = constant un-ignorable clicking. Replaced
                            //   mgs_hover_07l, a quiet sustained hover drone whose
                            //   faint attack got lost in the ~0.7s in-range window)
    SwoopWallImpact,        // mgs_sith_hit1 (side-wall thump one-shot)
    // Co-pilot discrete-command steering (experimental, 2026-06-21). A
    // repeating directional tick (panned AND pitched to the side to steer) +
    // a single rising "on the line, release" tone — replaces the continuous
    // panned guide. Custom synthesised WAVs (Override\acc_steer_*.wav): loud,
    // short tones, since the stock UI clicks first tried were far too quiet
    // (gui_invselect RMS 3.3% vs these ~44%). Low pitch = left, high = right,
    // so direction is carried by pitch as well as pan (pan alone localises
    // poorly in the race — the weakness that sank the old guide).
    SwoopSteerLeft,         // acc_steer_l  (low square tick — steer LEFT)
    SwoopSteerRight,        // acc_steer_r  (high square tick — steer RIGHT)
    SwoopSteerAligned,      // acc_steer_ok (rising tone — on the line / release)
    // "You can shift up a gear now" cue (swoop_race.cpp TickShiftReady). Was a
    // bare literal there; centralised here so the glossary can audition it.
    SwoopShiftReady,        // acc_shift (the 0.2 s astromech chirp c_drdastro_atk1,
                            //   shipped under our own resref — see the resolver note)
    // KOTOR 2 only in practice (K1's bike cannot jump): a low bar that spans the
    // bike's lane is close enough that a jump pressed now clears it
    // (minigame_swoop_audio.cpp, the hazard sweep). One-shot per bar.
    SwoopJumpNow,           // mgs_warnbust (the minigames' own warning beep;
                            //   present in both games' Sounds.bif)
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
        case NavCue::SwoopAccelpadBoost:       return "acc_boost";
        case NavCue::SwoopObstacleWarn:        return "cb_gr_boncehard2";
        case NavCue::SwoopWallImpact:          return "mgs_sith_hit1";
        case NavCue::SwoopSteerLeft:           return "acc_steer_l";
        case NavCue::SwoopSteerRight:          return "acc_steer_r";
        case NavCue::SwoopSteerAligned:        return "acc_steer_ok";
        case NavCue::SwoopShiftReady:          return "acc_shift";
        case NavCue::SwoopJumpNow:             return "mgs_warnbust";
    }
    return "";
}

// Per-game resolution. Kept as the single seam for a cue whose sample differs
// per game; currently none does. History: the shift-ready cue was the vanilla
// c_drdastro_atk1, which KOTOR 2 ships only in streamsounds\ (not key-indexed,
// so the engine's resref lookup fails — silent for the whole first K2 swoop
// round, patch-20260914-163810). The K2 fallback c_drdasasin_atk1 is 0.87 s
// against the chirp's 0.20 s — "too long" at the gear change, and it masked
// the guide tone at the mine after the gear-5 shift (rounds 14/15). The chirp
// now ships as the Override asset acc_shift.wav (byte-identical in both
// games' streamsounds), so both games hear the same short beep.
inline const char* GetNavCueResrefForGame(NavCue cue) {
    return GetNavCueResref(cue);
}

}  // namespace acc::audio
