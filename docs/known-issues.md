# Known Issues

Status tracker for accessibility-mod work, in three buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Planned** — future feature work that's not currently in flight.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

- **Tab content echoing.** When the tab content list is too long, the announcement starts echoing — the same line is spoken multiple times instead of just once.

## Planned

### Character creation screen

End-to-end navigation through the character creation flow works (foreground-panel routing via `CSWGuiManager.modal_stack` lands chain rebind on the active modal/wizard correctly). Outstanding items:

- **CHARAKTERAUSWAHL class-icon row.** Six class icons (`vtable=0073E658`, image-only buttons with no inline text) sit at a single y row. The selected class name is rendered in a sibling label (`id=2`, e.g. "Gauner") that the engine repaints on focus change. We currently announce nothing on focus into an icon, and Right-arrow on the row triggers the cycle-flanker heuristic (`FindAdjacentArrow`) which `FireActivate`s the next icon — auto-advancing the screen instead of just moving focus. Need a row-vs-cycle distinguisher (more than ~3 same-y same-empty-text peers ⇒ row) plus sibling-label readout on focus.
- **Step-gating: Talente / Name / Spielen silent on Enter.** Custom-character wizard buttons no-op silently when their prerequisite step isn't yet complete. Hypothesis: gated on `CSWGuiControl.is_active` at +0x4c (same flag that blocks `vtable[15]` activation on tabs). Diagnostic logging of `is_active` / `bit_flags` per chain entry is in place. Once confirmed: announce locked state on focus ("…, gesperrt") and announce a hint on Enter instead of letting `FireActivate` silently no-op.
- **Wizard sub-screen announce.** Porträt picker, Attribute spender, Fähigkeiten, Talente, Name input each have their own widget mix. Chain navigation works; per-screen value readouts (current attribute values, remaining points, name editbox content, etc.) need separate investigation — not blocking entry into the game now that the skeleton flow is passable.
- **Unhandled vtable types from chargen panels.**
    - `0073E658` (image-only button override at vtable[22]) — class icons + portrait-picker arrows. Detect-as-button + sibling-label readout.
    - `0073E8E8` (label override at vtable[20]) — wizard step-number decorations paired with "1"/"2"/… labels. Probably skip; non-interactive.
    - `00752E30` — appears 4× in CHARAKTERAUSWAHL as id=0/1/4/7/8/9/10. Looks like layout/group containers. Skip until proven interactive.
    - `0073E5B8` — frame/background widget, appears as id=0 in many panels. Skip.

## Polish

- **Cycle items have English description labels.** Cycle widgets such as Difficulty announce as `"Difficulty, Leicht"` rather than `"Schwierigkeitsgrad, Leicht"` in the German build. The captured category text comes from the `.gui` file's default at panel construction time, which is English. Localized German lives in the parent Options panel's listbox-blob (per-tab) or in TLK str_refs we haven't located yet — fixing this needs either cross-panel blob lookup or a hooked panel-init function that captures the text before the engine's localization step replaces it.
- **Focus mismatch after leaving a modal.** When a modal closes and the chain rebinds onto the underlying foreground panel, we announce the chain-anchored entry but the engine's actual hover/active state isn't yet at that control — the first arrow press realigns to the announced item rather than moving past it, so users press one extra arrow before the chain actually advances. Likely needs a deferred `MoveMouseToPosition` or `panel.SetActiveControl` on rebind to commit the announced focus into the engine's own state.
