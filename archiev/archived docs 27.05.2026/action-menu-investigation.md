# Action Menu (Aktionsmenü) — Investigation

## Goal

Make the **player's own action bar** (the strip of category icons next to each party-member portrait) navigable from the keyboard and announced for screen readers. This is what the post-first-fight tutorial calls "AKTIONSMENÜ" and is **not** the radial menu — that's `CSWGuiTargetActionMenu`, separate UI, already covered (`docs/radial-menu-investigation.md`).

## Tutorial-text recap (per `patch-20260505-121148.log`, lines 1212–1303)

The sighted-UI flow the game teaches the player after the Endar Spire prologue fight:

1. Click the wounded character's portrait (selects which character "owns" the action bar in focus).
2. Scroll horizontally through the columns of the AKTIONSMENÜ. First two columns are: friendly Force powers, medical items.
3. Each column shows up/down arrows next to its symbol when more than one variant exists (e.g. Medikit vs. verbessertes Medikit).
4. Mouse wheel cycles the variant within the focused column.
5. Left-click executes the current variant on the current character.

## Engine surfaces — known (verified from `swkotor.exe.h` + `k1_win_gog_swkotor.exe.xml`)

### Containers

```
CSWGuiMainInterface (offset 0x771c, label field45)
    CSWGuiMainInterfaceAction[6]      // 6 columns on the player bar
                                       // (manual hotkeys cover only 4: keys 4..7)

CSWGuiMainInterfaceAction (size 0x71c)
    CSWGuiButton action_button         // the icon — click to use current variant
    CSWGuiButton action_label          // text label of current variant
    CSWGuiButton up_button             // cycle to next variant within column
    CSWGuiButton down_button           // cycle to previous variant within column
    int          is_action?            // slot active/visible flag
```

The same `CSWGuiMainInterfaceAction` widget type appears in `CSWGuiTargetActionMenu.target_actions[3]` — the radial reuses the same column primitive, just only 3 of them.

### Functions (from Lane's Ghidra labels)

Action-bar (player-side, "personal"):
- `0x006888e0` — `CSWGuiMainInterface::SelectPrevPersonalAction(this, int)` — cycle column backward
- `0x0068ad60` — `CSWGuiMainInterface::DoPersonalAction(this, int, int)` — execute current variant in column. This is what hotkeys 4–7 ultimately invoke.
- `0x00688690` — `CSWGuiMainInterface::OnCharacterClicked` — handler for portrait click (the "select which character owns the bar" step).

Target-side (the floating 3-column menu over the cycled target — keys 1/2/3):
- `0x00688490` — `OnTargetActionFocus`
- `0x006884b0` — `OnTargetUpArrowPressed`
- `0x00688520` — `OnTargetDownArrowPressed`
- `0x0068ad20` — `DoTargetAction`

Per-column item widgets:
- `0x00684df0` — `CSWGuiMainInterfaceAction::HitCheckMouse` (mouse-region test)
- `0x00685840` — `CSWGuiMainInterfaceAction::GetActive`
- `0x00685960` — `CSWGuiMainInterfaceAction::SetIcon`

Source-of-truth for column contents:
- `0x00619db0` — `CSWCCreature::GetPersonalActions(int slot, CExoArrayList* out)` — populates the variants for one column for a given creature.
- `0x00619c20` — `CSWCCreature::GetTargetActions(CSWCObject* target, int slot, CExoArrayList* out)` — same shape, target-side.

### GUI event codes (the language the engine routes inputs in)

From `GuiEvents` enum in `swkotor.exe.h:16938-16952`:
- `OnEnterKey = 39` (already used by us for activation)
- `OnEscKey = 40`
- `OnUpArrow = 61`, `OnDownArrow = 62`, `OnLeftArrow = 63`, `OnRightArrow = 64`
- `OnTabKey = 206`

## Engine surfaces — suspected

- A `SelectNextPersonalAction` must exist (mouse-wheel-up cycles forward), but Lane's database only labels `SelectPrevPersonalAction`. It is most likely an unlabelled function adjacent to `0x006888e0`, OR `SelectPrevPersonalAction(this, int dir)` actually accepts a direction argument and is misleadingly named. Verify by either disassembling around `0x006888e0` or by hooking `up_button`'s `OnPressed` and observing which function it calls.
- The mouse wheel inside a column is presumably wired to the same handler that `up_button` / `down_button` clicks invoke — i.e. wheel-up == "click up_button".

## Engine surfaces — open

- Which column is "focused" when the user has only pressed a number key 4..7? The hotkey path may bypass focus entirely and act on a hardcoded slot. Need to walk how 4..7 are dispatched — probably via the same `DoPersonalAction(slot, …)` with a fixed slot parameter.
- Whether the action bar respects party-leader vs. selected-character: the tutorial says "click the injured character's portrait first," so the bar is per-character. We need to confirm whether the bar surfaces the *currently-selected* character or always the leader, and how to change that selection from the keyboard. (`OnCharacterClicked` is the entry; need to find its programmatic equivalent.)
- Variant-list length per column: tutorial implies the medical column may hold 0–N variants. Need a way to read `GetPersonalActions` output to announce "Medikit (1 of 2): Medikit" / "Medikit (2 of 2): verbessertes Medikit".

## Mouse-only gap (how this maps to accessibility)

What works without us doing anything:
- Hotkeys 4..7 — `DoPersonalAction` is keyboard-reachable for the *currently-selected* variant per column (4=Force, 5=Medical, 6=Misc, 7=Mine). So a player can already use a medikit blind, *if* the medikit is the currently-selected variant of column 5.

What does not work without us:
- No way to know which variant is current (no announcement).
- No keyboard cycling within a column — the `up_button` / `down_button` widgets exist but are wired only to mouse click + mouse wheel. There is no default key-binding.
- No way to know how many variants the column even has, or what the other columns contain.

## Implementation approach (ranked)

1. **Drive the existing `up_button` / `down_button` widgets via click-sim**, on a key press the user picks (e.g. Up/Down arrow when the action bar is "focused", or a chord like Shift+5 = "open column 5 with arrow nav"). This reuses the click-sim infrastructure we already use for in-game menu icons / equip slots and avoids needing to know the address of `SelectNextPersonalAction`.
2. **Read `action_label` text per column** to announce the current variant after a cycle. Same widget-text path as everything else (`gui_string` indirection per `project_kotor_text_indirection.md`).
3. **Call `DoPersonalAction(slot, …)` directly** to fire the action — same effect as the existing 4..7 hotkeys, but reachable from a chord we control if the user wants Enter-after-cycle behaviour rather than re-pressing the column hotkey.

## Open questions before implementation

- Where does focus live? (Per-column? Per-character? Both?)
- How does the engine know "which character is the action bar showing for"? Likely a field on `CSWGuiMainInterface` set by `OnCharacterClicked`.
- Variant count / current-index — is it on `CSWGuiMainInterfaceAction`, or fetched fresh from `GetPersonalActions` each frame?

These are reachable by attaching diagnostics to `up_button`'s `OnPressed` and watching the resulting state change. That's the next step.
