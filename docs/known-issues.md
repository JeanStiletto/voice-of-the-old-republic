# Known Issues

Status tracker for accessibility-mod work, in three buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Planned** — future feature work that's not currently in flight.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

- **Tab content echoing.** When the tab content list is too long, the announcement starts echoing — the same line is spoken multiple times instead of just once.

## Planned

### In-game architecture refit (live, partially validated)

**Verified working** (in `patch-20260502-184232.log`):
- Panel identity registry — every observed panel resolves: `MessageBox`, `TutorialBox`, `MainInterface`, `Fade`, `DialogCinematicCopy`, `DialogLetterbox{1,2,3}`, `StatusSummary`, `InGameMenu`, `InGameOptions`, `InGameEquip`, `InGameAbilities`, `InGameCharacter`, `InGamePause`.
- Listbox content extraction — modal messages and dialog reply lists both extract correctly (`src=listbox text="..."`).
- Content-change monitor — caught every dialogue line transition for the Trask conversation; tutorial popup now reads when its label late-binds.
- Esc-handler stale-pointer fix — single-fire close instead of 14× spam.
- Empty-chain walk — surfaced the dialog auxiliary-panel structure (single message-label child, no listbox) and the routing pattern.

**Open from session 2** (`patch-20260502-184232.log`):
- Vtables `0x0073E8E8` and `0x0073E658` identified as CSWGuiLabelHilight and image-only CSWGuiButton subclasses respectively (function `0x00641DB0` is the identity downcast). Their visible text routes through `CSWGuiText.text_params.text_object` (offset 0x50 within text_params; absolute 0x138 from CSWGuiLabel base, 0x1BC from CSWGuiButton base) rather than the inline CExoString. **Iteration 3 lands** the `text_object` indirection in `ExtractTextOrStrRefIndirect`, plus generalized `FindSiblingLabel` (now searches above AND below) and a sibling-label fallback in `ExtractAnnounceableText` for chain-navigable buttons with empty text. Plus a dialog-reply selection monitor in `OnUpdate` that polls `listbox.selection_index` and announces the current row when it changes (engine doesn't fire SetActiveControl on per-row arrow nav).

### In-game architecture refit (in flight, untested)

Six new layers landed in the patch DLL but have not yet been validated against a live in-game session. Iterate on the next gameplay run:

- **Layer 1 — Panel identity registry.** `IdentifyPanel(void*) → PanelKind` resolves CGuiInGame via `*(0x7A39FC) → CAppManager.client(+0x4) → CClientExoApp.internal(+0x4) → CClientExoAppInternal.gui_in_game(+0x40)` and matches the panel pointer against named slots (`tutorial_box`, `main_interface`, `dialog_cinematic`, `bark_bubble`, `in_game_pause`, `message_box`, etc.). Every first-sight `(panel, kind)` pair is logged as `PanelKind: panel=X identified as Y`. Verify against gameplay log: `12B04010` should resolve as `TutorialBox`, `12CD23D8` as `MainInterface`, `07434E40` as `MessageBox`, etc. If `Unknown` everywhere, the indirection chain is wrong.
- **Layer 2 — Listbox content extraction in `ExtractAnnounceableText`.** When a control with `vtable=0x0073E840` is encountered (most commonly as a panel child), walks `controls[0..7]` and concatenates row texts. Resolves the recurring `src=none vtable=0073E840` lines for modals like the recurring `07434E40`. Verify: those lines should now show `src=listbox text="..."`.
- **Layer 3 — Per-panel content monitor.** `MonitorPanelContents` runs every `OnUpdate`, walks `panels[]`, and for kinds in `IsContentMonitored` (TutorialBox, MessageBox, BarkBubble, DialogCinematic/Computer/ComputerCamera/CinematicCopy, AreaTransition) computes a fingerprint of label+listbox content, announces diffs. Designed to catch the late-bound tutorial label (panel constructed with `text=" "`, hint string written seconds later). Logs every fingerprint change as `ContentChange: panel=X kind=Y` with prev/curr.
- **Layer 4 — `g_currentPanel` staleness fix.** Esc handler now uses `activePanel` (resolved from manager's modal_stack each call) instead of `g_currentPanel` (set on focus, never cleared). Should stop the 14×-Esc-spam pattern observed after a modal closes.
- **Layer 5 — Empty-chain logging.** When `RebindChain` lands on a panel with zero navigable controls, log `Chain empty: panel=X kind=Y has no navigable controls`. No fallback wired yet — first see in the log which kinds tend to be empty (suspect: routing-only overlays like `074FE618`) before deciding policy.
- **Per-kind handlers (Layer 6).** Not yet implemented as kind-specific extractors. The content monitor (Layer 3) is the generic substrate; kind-specific extraction (e.g. read `CSWGuiBarkBubble.barktext_label` directly at known offset, walk `CSWGuiDialog.replies_listbox` as a chain target) is the natural next step once the generic layer surfaces what's missing.

Known unknowns to watch for in the next log:
- Whether Layer 1's indirection chain resolves `CGuiInGame` early enough — title-screen sessions never enter gameplay, so identification will return `Unknown` for everything before the first module load. Expected, but worth confirming.
- Whether Layer 3 spam-announces persistent panels whose content varies harmlessly (e.g. a clock label, an FPS counter) — would manifest as repeated `ContentChange` lines for the same panel. If so, narrow the kind whitelist.
- Whether the listbox extraction (Layer 2) returns the expected text or something garbled (string-encoding mismatch on the row label). The `prev`/`curr` lines in `ContentChange` make this directly visible.
- Whether `IdentifyPanel`'s indirect reads occasionally fault (CGuiInGame transient null during loads). The function null-checks at every step but reading `*0x7A39FC` itself would page-fault if the address ever became invalid; not expected on Steam 1.0.3 but a debug log would catch it.

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
