# menus_chargen_attr.cpp (424 lines)

Implements the chargen Attributes-panel fixes: mirrors each ability button's
matching label into the cycle-category cache ("Stärke, 8" instead of bare
"8"), syncs chain focus into the engine's `selected_ability` so Left/Right
targets the focused row (the engine only writes that field on a real mouse
click), and speaks a computed modifier/cost suffix + the engine's own
description text on each chain step. The modifier/cost math is hand-computed
(D&D 3.5 floor rule + the engine's point-cost accessor) rather than read from
the engine's labels because those refresh asynchronously and render a
modifier of 0 as a bare "-" that sounds broken.

## Declarations (in source order)

- L22 — `namespace acc::menus::chargen_attr`
- L24 — `bool IsChargenAttributesPanel(void* panel)`
- L29 — `int AbilityIndexFromButton(void* panel, void* control)`
- L37 — `void SyncSelectedAbilityFromChainFocus()`
- L68 — `int RowPitchForCursorWarp(void* panel, void* control)`
- L74 — `void CaptureLabelsIfApplicable(void* panel)`
- L118 — `int ParseAbilityValueText(const char* text)` (anonymous ns)
  note: tolerant of transient dash/empty renders; range-checked 1..30
- L138 — `int ComputeAbilityModifier(int value)` (anonymous ns)
  note: D&D 3.5 floor((value-10)/2), adjusted for true floor on negative deltas
- L153 — `typedef PFN_GetAbilityPointCost` / `int ReadEngineAbilityCost(void* panel, int currentValue)` (anonymous ns)
  note: param is the ability's CURRENT VALUE, not its index — verified after index-based calls always returned cost=1
- L175 — `void FormatModifier(int mod, char* outBuf, size_t bufSize)` (anonymous ns)
  note: renders 0 as bare "0" (engine's own "-" render sounds broken)
- L198 — `bool AnnounceChainStepDescription(void* panel, void* control)`
  note: calls the engine's OnEnterPointsButton directly keyed by the FOCUSED button — bypasses the cursor-warp hover hit-test, which resolves to the row above
- L269 — `bool IsChargenAttributesDescriptionListbox(void* listBox)`
  note: lets menus.cpp's listbox hook silence the engine's own hover-driven echo on this listbox
- L278 — `void AnnounceChainStepSuffix(void* panel, void* control)`
- L319-334 — `struct ChangeTracker` / `ChangeTracker s_tracker` (anonymous ns)
  note: per-ability last-mod/last-cost; sentinel INT_MIN = "not yet tracked" (first observation seeds silently)
- L337 — `bool AnnounceValueChange(void* panel, void* control)`
  note: only announces modifier/cost when a +/- press tipped it across a breakpoint
