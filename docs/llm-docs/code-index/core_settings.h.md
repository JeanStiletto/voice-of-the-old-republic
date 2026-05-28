# core_settings.h (80 lines)

Nav-system settings — minimal stub. All consumers go through `Get()` so a future config-file-backed mutable path is a single-file swap. Defaults encode the locked plan decisions.

## Declarations (in source order)

- L10 — `namespace acc::core`
- L11 — `struct Pillar1Settings`
  note: change-driven cue toggles + awareness/threshold/budget params for wall/hazard/door/npc/etc. proximity cues
- L31 — `struct Pillar2Settings`
  note: room/area announcement + compass + view-mode toggles
- L40 — `struct Pillar3Settings`
  note: guidance/beacon/map-cursor/autowalk toggles + distance-milestone array
- L54 — `struct Pillar4Settings`
  note: per-category cycle toggles + spoiler-gating flag
- L65 — `struct CrossPillarSettings`
- L70 — `struct NavSettings`
  note: aggregates Pillar1..4 + Cross into one settings root
- L78 — `const NavSettings& Get()`
