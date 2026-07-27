# menus_chargen_skills.cpp (277 lines)

Mirror of menus_chargen_attr.cpp for the chargen Skills panel
(CSWGuiSkillsCharGen): mirrors skill_button → skill_label text into the
cycle-category cache, syncs chain focus into `selected_skill_index`, and
speaks a per-row cost suffix + description on chain step. Cost is computed
via the engine's `IsClassSkill` predicate (1 = class skill, 2 = cross-class)
rather than read from a label, matching the same refresh-timing avoidance
used in the Attributes panel. No D&D modifier concept here (skills only have
rank + flat cost).

## Declarations (in source order)

- L20 — `namespace acc::menus::chargen_skills`
- L22 — `bool IsChargenSkillsPanel(void* panel)`
- L27 — `int SkillIndexFromButton(void* panel, void* control)`
- L35 — `void SyncSelectedSkillFromChainFocus()`
- L66 — `void CaptureLabelsIfApplicable(void* panel)`
- L104 — `int RowPitchForCursorWarp(void* panel, void* control)`
- L117 — `typedef PFN_IsClassSkill` / `int ReadEngineSkillCost(void* panel, int skillIdx)` (anonymous ns)
- L135 — `void AnnounceChainStepSuffix(void* panel, void* control)`
- L160 — `bool AnnounceChainStepDescription(void* panel, void* control)`
  note: calls the engine's OnEnterPointsButton with the FOCUSED button — bypasses the hover hit-test, which is reliably off-by-one on this panel due to label overlap
- L239 — `bool IsChargenSkillsDescriptionListbox(void* listBox)`
- L248 — `bool AnnounceValueChange(void* panel, void* control)`
  note: omits both label and cost from the re-announce — cost is constant per skill, already heard in the chain-step suffix
