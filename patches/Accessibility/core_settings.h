// Nav-system settings — minimal stub. Consumers go through Get() so the
// future user-options UI is a single-file swap. Behaviour-as-locked-design
// (bearing frame, cycle sort order, etc.) stays in consumer code.

#pragma once

namespace acc::core {

// Pillar 1 — small-scale change-driven cues.
struct Pillar1Settings {
    bool cueWall              = true;
    bool cueHazard            = true;
    bool cueDoor              = true;
    bool cueNpc               = true;
    bool cuePlaceable         = true;
    bool cueItem              = true;
    bool cueLandmark          = true;
    bool cueTransition        = true;
    bool trigger1DistanceDelta = true;       // 360° distance-delta
    bool trigger2FrontCone     = true;       // ±45° foremost-in-front cone
    bool footstepSuppression   = true;
    float awarenessRangeMeters         = 5.0f;
    float distanceDeltaThresholdMeters = 1.5f;
    int   voiceBudgetMax               = 3;
    // K-nearest cap on per-tick wall cues; walls beyond K still update
    // last_cued_distance so they don't pile into next tick's candidates.
    int   trigger1MaxWallCuesPerTick   = 3;
};

// Pillar 2 — room/area announcements + view mode.
struct Pillar2Settings {
    bool roomTransitionAnnouncement = true;
    bool areaTransitionAnnouncement = true;
    bool octagonalCompassOnTurn     = true;
    bool viewModeFindabilityLoops   = true;
    float octagonalSectorHysteresisDegrees = 5.0f;
    int   viewModeTtsHoverPauseMs          = 300;
};

// Pillar 3 — guidance, map cursor, named markers.
struct Pillar3Settings {
    bool audioBeacon              = true;
    bool autowalk                 = true;
    bool mapCursorExploreMode     = true;
    bool playerPositionOnMap      = true;
    bool savedNamedMarkersEnabled = true;
    bool multiAreaRouteChoicePrompt = false;
    float reachedToleranceMeters  = 1.0f;
    static constexpr int kDistanceMilestoneCount = 5;
    float distanceMilestonesMeters[kDistanceMilestoneCount] = { 200.0f, 100.0f, 50.0f, 20.0f, 5.0f };
};

// Pillar 4 — discrete object cycle.
struct Pillar4Settings {
    bool categoryDoor             = true;
    bool categoryNpc              = true;
    bool categoryContainer        = true;
    bool categoryItem             = true;
    bool categoryLandmark         = true;
    bool categoryTransition       = true;
    bool emptyCategorySilentSkip  = true;
    bool spoilerGating            = true;   // engine fog-of-war + curation
};

struct CrossPillarSettings {
    bool combatVerbosityReduction = true;
    bool cutsceneNavCuesMostlyOff = true;
};

struct NavSettings {
    Pillar1Settings     pillar1;
    Pillar2Settings     pillar2;
    Pillar3Settings     pillar3;
    Pillar4Settings     pillar4;
    CrossPillarSettings cross;
};

const NavSettings& Get();

}  // namespace acc::core
