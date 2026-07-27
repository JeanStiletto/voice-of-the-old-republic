# menus_chargen_skills.h (106 lines)

Public surface mirroring menus_chargen_attr.h for CSWGuiSkillsCharGen (8
skills instead of 6 attributes, straight top-to-bottom struct order, flat
class/cross-class cost instead of a D&D modifier).

## Declarations (in source order)

- L36 — `namespace acc::menus::chargen_skills`
- L39 — `bool IsChargenSkillsPanel(void* panel)`
- L45 — `int SkillIndexFromButton(void* panel, void* control)`
  note: struct order Computer, Demolitions, Stealth, Awareness, Persuade, Repair, Security, Treat Injury
- L54 — `void SyncSelectedSkillFromChainFocus()`
- L58 — `void CaptureLabelsIfApplicable(void* panel)`
- L64 — `int RowPitchForCursorWarp(void* panel, void* control)`
- L72 — `void AnnounceChainStepSuffix(void* panel, void* control)`
- L86 — `bool AnnounceChainStepDescription(void* panel, void* control)`
  note: reads the description listbox directly rather than skill_descriptions[i] array (per the .cpp implementation)
- L92 — `bool IsChargenSkillsDescriptionListbox(void* listBox)`
- L103 — `bool AnnounceValueChange(void* panel, void* control)`
