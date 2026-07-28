# core_settings.h (80 lines)

Nav-system settings stub — the locked-design defaults for all four accessibility pillars plus cross-pillar toggles, grouped into per-pillar structs so a future user-options UI can swap in mutable state behind the same `Get()`. Consumer code holds the actual behaviour (bearing frame, cycle sort order); this file only holds tunable numbers/flags (awareness ranges, thresholds, voice budgets, distance milestones).

## Declarations (in source order)

- L7 — `namespace acc::core`
- L10 — `struct Pillar1Settings` — small-scale change-driven cues (wall/hazard/door/npc/placeable/item/landmark/transition toggles, awarenessRangeMeters=5.0f, distanceDeltaThresholdMeters=1.5f, voiceBudgetMax=3, trigger1MaxWallCuesPerTick=3)
- L31 — `struct Pillar2Settings` — room/area announcements + view mode (octagonalSectorHysteresisDegrees=5.0f, viewModeTtsHoverPauseMs=300)
- L41 — `struct Pillar3Settings` — guidance/map cursor/named markers (reachedToleranceMeters=1.0f, distanceMilestonesMeters={200,100,50,20,5})
- L54 — `struct Pillar4Settings` — discrete object cycle category toggles + emptyCategorySilentSkip + spoilerGating
- L65 — `struct CrossPillarSettings` — combatVerbosityReduction, cutsceneNavCuesMostlyOff
- L70 — `struct NavSettings` — aggregates all five above
- L78 — `const NavSettings& Get()`
