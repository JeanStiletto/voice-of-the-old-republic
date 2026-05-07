// Navigation-cue vocabulary — resref mapping for the 12 audio slots locked
// in docs/navsystem-longterm-plan.md §"Audio vocabulary inventory".
//
// Layer: audio/ (data-only header, no engine indirection of its own; pairs
// with audio_bus.h which provides the playback primitives). All consumers
// resolve enum → resref via GetNavCueResref, then call acc::audio::PlayCue
// or PlayCue3D from audio_bus.h.
//
// Source of picks: Phase 1 lay-off 5 atmospheric-pass curation
// (docs/navsystem-progress.md, "Lay-off 5"). Each entry was auditioned
// from the 1928-file pool extracted from data/sounds.bif by xoreos-tools
// (docs/tools.md). Some picks are flagged provisional in the curation log
// pending live re-audition under Phase 3 hook-test conditions.
//
// Swap procedure: change the string literal on the relevant case line below.
// The resref is bound by CResRef's 16-char hard limit (audio_bus.cpp's
// FillResRef silently truncates beyond that); current longest pick is
// "as_nt_wtrdrip_09" at exactly 16 chars (any longer = silent truncation
// = resolution miss = silent cue, see project_pitchhook_silent_resref
// memory). The engine's resource-resolution chain is case-insensitive
// and walks Override → streamwaves → streamsounds → streammusic →
// BIF/RIM, so a custom Override/ WAV with the same resref will shadow
// the engine asset transparently.
//
// Verifying a resref before using it: every cue resref MUST resolve to
// a real file in the chitin.key index, otherwise Play3DOneShotSound
// silently no-ops (creates no source, makes no sound). The previous
// Wall pick "gui_select" was missing from chitin.key for the entire
// install — confirmed via the per-fire PitchHook diagnostic on
// 2026-05-07 (958 play cue=Wall logs, 0 source creations). The full
// extraction at build/sounds-extracted-full/ is the source of truth for
// "does this resref resolve?".
//
// Phase 1 lay-off 5 closeout. No runtime consumer in this lay-off — the
// header is included by audio_bus.cpp for compile-verification only. The
// first consumer lands in Phase 2 (Pillar 4 cycle) and Phase 3 (Pillar 1
// change-driven cues).

#pragma once

namespace acc::audio {

// 12-slot navigation-cue vocabulary. Order is plan-stable: per-kind first
// (8), then special-purpose (4). When adding a slot, also extend the
// switch in GetNavCueResref below or the build will warn on missing case.
enum class NavCue {
    // Per-kind cues (8) — one per object/feature class the player can
    // encounter while moving. Six map directly to Pillar 4's locked
    // categories; Wall and HazardLedge are Pillar 1 walkmesh-geometry
    // features without an object-type equivalent.
    Door,
    NpcCreature,
    ContainerPlaceable,
    Item,
    Landmark,
    TransitionExit,
    Wall,
    HazardLedge,
    // Special-purpose cues (4) — guidance + view-mode collision.
    Collision,
    BeaconActive,
    BeaconWaypointReached,
    BeaconDestinationReached,
};

// Map a NavCue to its engine resref. constexpr so the compiler folds the
// switch to a constant table; no runtime branching on hot paths.
constexpr const char* GetNavCueResref(NavCue cue) {
    switch (cue) {
        case NavCue::Door:                     return "gui_close";
        case NavCue::NpcCreature:              return "fs_metal_droid2";
        case NavCue::ContainerPlaceable:       return "gui_invadd";
        case NavCue::Item:                     return "gui_invselect";
        case NavCue::Landmark:                 return "gui_quest";
        case NavCue::TransitionExit:           return "mgs_s1";
        case NavCue::Wall:                     return "as_nt_wtrdrip_09";
        case NavCue::HazardLedge:              return "cb_sw_bldlrg1";
        case NavCue::Collision:                return "gui_invdrop";
        case NavCue::BeaconActive:             return "gui_actscroll";
        case NavCue::BeaconWaypointReached:    return "gui_prompt";
        case NavCue::BeaconDestinationReached: return "gui_complete";
    }
    return "";  // unreachable; satisfies non-void return path
}

}  // namespace acc::audio
