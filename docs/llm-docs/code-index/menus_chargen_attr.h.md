# menus_chargen_attr.h (126 lines)

Public surface for the chargen Attributes-panel (CSWGuiAbilitiesCharGen)
fixes. Both problems stem from the same engine quirk: the panel's six +/-
buttons all dispatch through `OnPlusButton`/`OnMinusButton`, which read
`selected_ability` rather than inspecting the fired button's identity — so
the chain-focus sync and the label-mirroring cache are both needed to make
keyboard nav behave like a real per-row control.

## Declarations (in source order)

- L29 — `namespace acc::menus::chargen_attr`
- L32 — `bool IsChargenAttributesPanel(void* panel)`
- L37 — `int AbilityIndexFromButton(void* panel, void* control)`
  note: struct order STR, DEX, CON, WIS, INT, CHA
- L48 — `int RowPitchForCursorWarp(void* panel, void* control)`
  note: compensates the engine's hit-test resolving one row ABOVE the cursor
- L69 — `void SyncSelectedAbilityFromChainFocus()`
  note: called from chain rebind/step AND per-tick — the engine's own OnEnterPointsButton silently overwrites selected_ability between sync and the queued FireActivate
- L74 — `void CaptureLabelsIfApplicable(void* panel)`
- L96 — `void AnnounceChainStepSuffix(void* panel, void* control)`
  note: modifier/cost computed locally rather than read from engine labels — those refresh late and render 0 as a bare "-"
- L105 — `bool AnnounceChainStepDescription(void* panel, void* control)`
- L110 — `bool IsChargenAttributesDescriptionListbox(void* listBox)`
- L123 — `bool AnnounceValueChange(void* panel, void* control)`
  note: label intentionally omitted — user just acted on this row, identity is fresh
