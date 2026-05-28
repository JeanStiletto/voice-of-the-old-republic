# menus_chargen_attr.h (111 lines)

Public surface for the chargen Attributes panel accessibility module. Declares `namespace acc::menus::chargen_attr`.

## Declarations (in source order)

- L1 — `namespace acc::menus::chargen_attr`
- L10 — `bool IsChargenAttributesPanel(void* panel)`
- L14 — `int AbilityIndexFromButton(void* panel, void* control)` — maps a +/- button to 0-based ability index (Stärke=0 … Geschick=5)
- L18 — `int RowPitchForCursorWarp(void* panel, void* control)` — returns the y-pixel pitch to compensate for the Options-style hit-test shift on this panel
- L22 — `void SyncSelectedAbilityFromChainFocus()` — re-stamps the engine's selected_ability_index field from g_chainIndex each tick; guards against engine cursor-warp overwrite race
- L26 — `void CaptureLabelsIfApplicable(void* panel)` — snapshots ability label texts into the per-panel cache on first sight
- L30 — `void AnnounceChainStepSuffix(void* panel, void* control)` — speaks the current value, point cost, and modifier after the ability name on chain step
- L34 — `bool AnnounceValueChange(void* panel, void* control)` — speaks the updated value/modifier/cost when the + or - button is pressed; returns true when announced
