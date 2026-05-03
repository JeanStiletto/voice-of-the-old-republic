// Navigation-system settings — locked defaults per
// docs/navsystem-longterm-plan.md §"Locked defaults".
//
// Layer: core/ (data-only header + tiny .cpp). No engine touch, no I/O,
// no engine-state reads — pure constants returned from a single accessor.
//
// Phase 1 lay-off 7: minimal stub. The values below are the plan-locked
// defaults. Phase 7 (user options UI, deferred) will replace the static
// constants with a config-file-backed mutable struct + load/save plumbing.
// Until then, every consumer in Phase 2-6 reads through `Get()` so the
// Phase 7 swap is a single-file change rather than a code-wide refactor.
//
// Several plan defaults flagged "(starting; tune live)" — those values are
// our best initial guess; expect them to change once Phase 3 hook tests
// produce live readings. Numeric tunables grouped per pillar so the future
// UI can expose them as sliders.
//
// What's NOT in here:
//   - Hardcoded design choices that the plan locked as behaviour rather
//     than user knobs (bearing frame = world-frame, cycle sort = distance
//     ascending, direction frame = clock-position, etc). Those live in
//     the consumer code as constants.
//   - Movement key swap (A/D ↔ Q/E) — ships via KotOR's engine keybind
//     config per plan §Movement model, not as a runtime setting.
//   - Per-save profiles. Plan §"Persistence: global" is explicit.

#pragma once

namespace acc::core {

// Pillar 1 — small-scale change-driven cues.
struct Pillar1Settings {
    // 8 per-kind cue toggles (plan §Locked defaults — Pillar 1).
    bool cueWall              = true;
    bool cueHazard            = true;
    bool cueDoor              = true;
    bool cueNpc               = true;
    bool cuePlaceable         = true;
    bool cueItem              = true;
    bool cueLandmark          = true;
    bool cueTransition        = true;
    // Trigger toggles.
    bool trigger1DistanceDelta = true;  // 360° distance-delta
    bool trigger2FrontCone     = true;  // ±15° foremost-in-front cone
    // Stuck-detection footstep gating (plan §Pillar 1 stuck-detection).
    bool footstepSuppression   = true;
    // Numeric tunables (plan-locked starting values; tune live).
    float awarenessRangeMeters         = 5.0f;
    float distanceDeltaThresholdMeters = 0.5f;
    int   voiceBudgetMax               = 3;
};

// Pillar 2 — medium-scale room/area announcements + view mode.
struct Pillar2Settings {
    bool roomTransitionAnnouncement = true;
    bool areaTransitionAnnouncement = true;   // two-stage (loading + arrived)
    bool octagonalCompassOnTurn     = true;
    bool viewModeFindabilityLoops   = true;   // active only when view mode is on
    float octagonalSectorHysteresisDegrees = 5.0f;
    int   viewModeTtsHoverPauseMs          = 300;
};

// Pillar 3 — large-scale guidance, map cursor, named markers.
struct Pillar3Settings {
    bool audioBeacon              = true;
    bool autowalk                 = true;
    bool mapCursorExploreMode     = true;   // active when map UI is open
    bool playerPositionOnMap      = true;
    bool savedNamedMarkersEnabled = true;
    bool multiAreaRouteChoicePrompt = false; // plan: future feature
    float reachedToleranceMeters  = 1.0f;
    // Distance-to-destination milestones in metres, descending. Beacon /
    // TTS fires once per crossing. Length-prefixed via the array size.
    static constexpr int kDistanceMilestoneCount = 5;
    float distanceMilestonesMeters[kDistanceMilestoneCount] = { 200.0f, 100.0f, 50.0f, 20.0f, 5.0f };
};

// Pillar 4 — discrete object cycle.
struct Pillar4Settings {
    // 6 category toggles (plan §Pillar 4 categories — locked).
    bool categoryDoor             = true;
    bool categoryNpc              = true;
    bool categoryContainer        = true;
    bool categoryItem             = true;
    bool categoryLandmark         = true;
    bool categoryTransition       = true;
    bool emptyCategorySilentSkip  = true;
    bool spoilerGating            = true;   // engine fog-of-war + curation
};

// Cross-pillar.
struct CrossPillarSettings {
    bool combatVerbosityReduction = true;
    bool cutsceneNavCuesMostlyOff = true;   // only Pillar 2 transitions may fire
};

// Top-level settings aggregate. Single instance; consumers hold a const
// reference returned by Get().
struct NavSettings {
    Pillar1Settings     pillar1;
    Pillar2Settings     pillar2;
    Pillar3Settings     pillar3;
    Pillar4Settings     pillar4;
    CrossPillarSettings cross;
};

// Returns the (currently immutable) settings instance. Phase 7's user
// options UI will replace the static-default backing without changing this
// signature.
const NavSettings& Get();

}  // namespace acc::core
