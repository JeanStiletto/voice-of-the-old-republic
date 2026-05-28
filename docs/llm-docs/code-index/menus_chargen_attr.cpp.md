# menus_chargen_attr.cpp (425 lines)

Chargen Attributes panel accessibility implementation. Handles chain-step suffix (value/cost/modifier) and value-change announcement for the 6 ability scores.

## Declarations (in source order)

- L22 — `bool acc::menus::chargen_attr::IsChargenAttributesPanel(void* panel)`
- L33 — `int acc::menus::chargen_attr::AbilityIndexFromButton(void* panel, void* control)`
- L48 — `void acc::menus::chargen_attr::SyncSelectedAbilityFromChainFocus()`
- L79 — `int acc::menus::chargen_attr::RowPitchForCursorWarp(void* panel, void* control)`
- L105 — `void acc::menus::chargen_attr::CaptureLabelsIfApplicable(void* panel)`
- L149 — `bool ReadLabelTextAt(void* panel, size_t offset, char* outBuf, size_t bufSize)` — SEH-guarded panel-offset label read (anonymous ns)
- L179 — `bool ReadButtonTextDirect(void* button, char* outBuf, size_t bufSize)` — reads gui_string from a button (anonymous ns)
- L211 — `int ParseAbilityValueText(const char* text)` — parses the numeric value from the engine's ability-value label string (anonymous ns)
- L231 — `int ComputeAbilityModifier(int value)` — standard D&D modifier formula: (value-10)/2 (anonymous ns)
- L244 — `typedef int (__thiscall* PFN_GetAbilityPointCost)(void* this_, int param)` (anonymous ns)
- L246 — `int ReadEngineAbilityCost(void* panel, int currentValue)` — calls engine's point-cost thiscall (anonymous ns)
- L268 — `void FormatModifier(int mod, char* outBuf, size_t bufSize)` — formats "+N" / "-N" / "0" (anonymous ns)
- L280 — `void acc::menus::chargen_attr::AnnounceChainStepSuffix(void* panel, void* control)`
- L322 — `struct ChangeTracker` + `ChangeTracker s_tracker` — tracks (panel, abilityIdx, lastValue) to detect changes (anonymous ns)
- L329 — `void ResetTrackerIfPanelChanged(void* panel)` (anonymous ns)
- L339 — `bool acc::menus::chargen_attr::AnnounceValueChange(void* panel, void* control)`
