# menus_chargen_skills.h (105 lines)

Public surface for the chargen Skills panel accessibility module. Declares `namespace acc::menus::chargen_skills`.

## Declarations (in source order)

- L1 — `namespace acc::menus::chargen_skills`
- L10 — `bool IsChargenSkillsPanel(void* panel)`
- L14 — `int SkillIndexFromButton(void* panel, void* control)` — maps a +/- button to 0-based skill index
- L18 — `void SyncSelectedSkillFromChainFocus()` — re-stamps engine's selected_skill_index each tick to guard against cursor-warp overwrite race
- L22 — `void CaptureLabelsIfApplicable(void* panel)` — snapshots skill label texts into the per-panel cache on first sight
- L26 — `int RowPitchForCursorWarp(void* panel, void* control)` — y-pixel pitch for Options-style hit-test shift compensation
- L30 — `void AnnounceChainStepSuffix(void* panel, void* control)` — speaks current rank, point cost, and class-skill marker on chain step
- L34 — `bool AnnounceChainStepDescription(void* panel, void* control)` — speaks the skill description from the description listbox; returns true when announced
- L38 — `bool IsChargenSkillsDescriptionListbox(void* listBox)` — true when the listbox is the skills-description listbox (used to suppress default listbox nav on it)
- L42 — `bool AnnounceValueChange(void* panel, void* control)` — speaks updated rank and cost after + or - press; returns true when announced
