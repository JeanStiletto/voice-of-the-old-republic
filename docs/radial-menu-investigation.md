# Radial menu — TAM populate + navigation

**Status: 2026-05-05 — wrapper-only path landed (UNTESTED in-game).** The crashing inner-PopulateMenus call has been removed in favour of `CSWGuiMainInterface::PopulateMenus(main_interface)` (wrapper, no args). Decomp evidence below; runtime verification pending.

## Premise

Make KOTOR's right-click radial menu (CSWGuiTargetActionMenu) navigable from the keyboard so blind players can pick a non-default action — Security on a locked door, Stealth before opening, Bash on a destructible — instead of only the cursor's default action. The radial is what the engine surfaces for any object that has *more than one* applicable action; the default-action picker (`engine_picker.cpp`) covers the simple "single applicable action" case.

## What's known (after 2026-05-05 decomp)

Decomp run via `tools/ghidra-scripts/Decompile.java` headless against `k1_win_gog_swkotor.exe.gzf`. Source in `docs/llm-docs/re/`. Functions read: `0x00689d80`, `0x00689410`, `0x00620350`, `0x006865b0`, `0x00686680`, `0x00689610`, `0x00685af0`, `0x00684ed0`.

### The two PopulateMenus

There are two functions named `PopulateMenus`. Distinguishing them is the single most important fact in this whole investigation:

- **Wrapper** — `CSWGuiMainInterface::PopulateMenus(this) @0x00689d80`. Public entry point. No arguments. Called by `HandleMouseClickInWorld` on first-click-on-target. Internally:
  1. `player = GetSWParty()->GetPlayerCharacter()`
  2. `game_object = CClientExoApp::GetGameObject(this->field1_0x64)`
  3. `swc_target = game_object->vtable->AsSWCObject(game_object)` — the **vtable downcast**.
  4. Refills 6 personal-action lists on the main interface (`field5_0x74[0..5]`) via `CSWCCreature::GetPersonalActions(player, i, &list)` for `i=0..5`.
  5. Calls inner `CSWGuiTargetActionMenu::PopulateMenus(player, mode, swc_target, &result) @0x00689410`.
  6. Updates main_interface UI rows from the personal-action lists.

- **Inner** — `CSWGuiTargetActionMenu::PopulateMenus(this, player, mode, swc_target, *out) @0x00689410`. `this` = TAM. `swc_target` MUST be a downcast `CSWCObject*` — passing a raw `CGameObject*` corrupts `target_type` (see "What broke" below). Internally:
  1. `target_type = GetTargetInterfaceTargetType(player, swc_target)` (a char).
  2. For row `i = 0..2`: clears + refills `this->action_lists[i]` via `CSWCCreature::GetTargetActions(player, swc_target, i, &list)`.
  3. For each row `r = 0..2`: looks up `this->field1_0x24[target_type * 3 + r]` (the persisted "selected action_id" for this target_type+row), finds the matching action in `action_lists[r]`, paints the row.

### field1_0x24 is the per-(type, row) selection state

`int field1[12]` at TAM +0x24 is **not** the field1[12] our header originally guessed it might be. It's a 4×3 grid: 4 target_types × 3 rows = 12 ints, each holding the *currently-selected action_id* for that combination. `-1` means "no selection, use the first item". The same field is read by `SelectNextAction`, `SelectPrevAction`, and `DoTargetAction` — all key on `field1_0x24[(char)target_type * 3 + row]` to find the active action within `action_lists[row]`. SelectNext/Prev *mutate* this slot to advance/regress the in-row selection.

This means our cycling primitives Just Work after a wrapper call: SelectNext mutates field1, the row's `action_button` text re-derives on next read.

### Action source: per-mode vs per-target

There are two different per-creature action enumerators:

- `CSWCCreature::GetPersonalActions(player, mode, &list)` — feeds the surrounding **MainInterface**'s 6 mode-keyed lists (combat, peaceful, force, items, …). Wrapper-only.
- `CSWCCreature::GetTargetActions(player, target, row, &list)` — feeds the **TAM**'s 3 per-row lists for the specific target (where Security against a locked door appears). Inner-only.

The wrapper calls both. Inner alone gives only target actions. So when we previously tried to call inner-only with bad-typed args, even when the SEH didn't fire and `result=0`, action_lists came back empty because `GetTargetActions(player, garbage_swc, ...)` had a junk vtable at hand.

### HandleMouseClickInWorld, vanilla flow

`@0x00620350` confirms the two-click radial flow:

```
if (last_clicked == 0x7f000000 || last_target != hover || last_clicked != last_target) {
  // First click: open the radial
  if (hover != 0x7f000000) SetLastTarget(hover);
  if (main_interface) CSWGuiMainInterface::PopulateMenus(main_interface);  // wrapper
} else {
  // Second click: dispatch the picked action
  last_clicked = 0x7f000000;
  if (count > 0 && desc != 0) {
    target_obj = GetGameObject(desc->target_id);
    swc = target_obj->vtable->AsSWCObject();   // same downcast pattern
    PlayGuiSound(6);
    (*(desc + 0xc))(*(desc + 8), player);      // call action_function(action_id, player)
  }
}
```

Same `vtable->AsSWCObject` pattern shows up everywhere the engine takes a target across the CGameObject/CSWCObject boundary. Two-click semantics: gate matches → dispatch; gate doesn't match → open radial.

### Engine primitives we're driving

- **`SelectNextAction(tam, row) @0x006865b0`** — finds the action whose `item_id_` matches `field1_0x24[target_type*3 + row]`, advances to the next entry in `action_lists[row]`, writes the new `item_id_` back into `field1_0x24`. PlaysGuiSound(7) when something changed.
- **`SelectPrevAction(tam, row) @0x00686680`** — symmetric; wraps to last.
- **`DoTargetAction(tam, row) @0x00689610`** — finds the matching action via the same field1_0x24 lookup, validates target via `GetGameObject()->AsSWCObject()`, then either:
  - On `field7_0x30 & 1 == 0` (action disabled) → sets `failure_reason_strref` to one of 0x96d5/0x96d6/0x96d7/0xa5b6/0xa602/0xbb40 and PlaysGuiSound(2). Pure UI rejection.
  - On enabled → calls `(*pCVar10->action_function)(pCVar10->item_id_, player)`, plays sound 6, sets visual feedback, calls `ServerCreature::ClearAllActions(0)`. Same dispatch shape as HandleMouseClickInWorld.

So nothing in the navigation primitives requires us to do anything beyond "let the wrapper run, then read+drive".

## What broke (RESOLVED)

The previous implementation called inner `CSWGuiTargetActionMenu::PopulateMenus @0x00689410` directly with `clientCreature` and `clientObject` arguments, where `clientObject` was the raw `CGameObject*` returned by `CClientExoApp::GetGameObject(handle)`. **Inner expects a `CSWCObject*` — the result of `clientObject->vtable->AsSWCObject(clientObject)`**. Without that downcast:

- `target_type = GetTargetInterfaceTargetType(player, garbage_swc)` returns junk.
- `field1_0x24[(char)target_type * 3 + row]` indexes well past field1's 12 ints; reads garbage from the stack-adjacent area; on subsequent SelectNext/Prev/DoTargetAction calls the same out-of-bounds read trips `/GS` (STATUS_STACK_BUFFER_OVERRUN 0xC0000409) on the next NWScript tick. The crash signature observed in `project_inner_populate_menus_crashes.md` is consistent with this: stack canary at script-tick time, not at the dispatch site itself.
- `GetTargetActions(player, garbage_swc, row, &list)` calls vtable methods on garbage → either a clean miss returning empty lists, or another fault.

So the same bug both produced the empty rows AND the delayed crash.

The fix is to drop the direct inner call entirely. The wrapper at 0x00689d80 already does the AsSWCObject downcast and calls inner with correct types, populating both the surrounding UI and the TAM's per-row action_lists.

## What we built (after the fix)

`acc::picker::Drive` empty-descriptor branch (`engine_picker.cpp`):

1. `SetMainInterfaceTarget(targetClient)` — installs hover target into `main_interface->field1_0x64`. (Already done before the descriptor probe.)
2. `GetDefaultActions(internal)` returns count=0 → empty descriptor branch.
3. `CSWGuiMainInterface::PopulateMenus(main_interface)` — wrapper, no args. Wrapper does steps 1–6 from "Wrapper" above.
4. `LogStateWide(tam, "after-wrapper")` — synchronous diagnostic.
5. Returns `radialOpened=true` — picker caller proceeds into `radial_menu::ArmAfterPopulate`.

`acc::radial_menu` navigation (unchanged from previous commit, now actually exercising correctly populated state):

- Up / Down — switch row, skip empty rows. Speak new row + label.
- Left / Right — `SelectNextActionInRow` / `SelectPrevActionInRow`. Speak new label.
- Enter — `DispatchRowAction` (DoTargetAction). Speak label, drop active flag.
- Esc — drop active flag, no engine teardown.

## Open after the fix

These are testable now that the crash is gone:

### O1 — Does the wrapper alone populate `action_lists` for our test target?

The previous test (commit 651adf7) reported empty action_lists *after* both wrapper AND inner. Inner was poisoning state; the wrapper's output may have already been empty before the inner ran, or it may have been populated and then overwritten. We don't know yet.

Investigation: launch with the wrapper-only path, walk to the Endar Spire Security door, press Enter, read `Radial.StateW[after-wrapper]:` lines. Expected: `action_lists[r]` non-empty for at least one row.

### O2 — Are all 4 target_types covered by field1_0x24's 12 ints?

`(char)target_type * 3 + row` indexes field1[12]. `(char)0..3 * 3 + 0..2` = 0..11, fits. `(char)4 * 3 + 2 = 14` overflows. So target_type is enumerated 0..3. Need to confirm what those four are by reading `GetTargetInterfaceTargetType`. Hypothesis: 0 = creature, 1 = door, 2 = placeable, 3 = trigger (matches the `case '\0'` / `case '\x03'` branches in DoTargetAction).

Investigation: decomp `CSWCCreature::GetTargetInterfaceTargetType` and read the switch. Cheap follow-up.

### O3 — Does the wrapper need a re-populate when the player picks a different target?

The wrapper reads `main_interface->field1_0x64` to know *which* target to populate against. Our `SetMainInterfaceTarget(targetClient)` sets that. If the user closes the radial (Esc), walks to a new door, and presses Enter, our picker calls SetMainInterfaceTarget again with the new handle, then GetDefaultActions, then (on empty descriptor) the wrapper. So a fresh PopulateMenus per Enter — should be self-correcting. Verify in test.

### O4 — field1_0x24 stickiness across radial opens

Inner-PopulateMenus does NOT reset field1_0x24. So if you opened the radial on Door A and selected Security (field1_0x24[1*3 + 0] = security_action_id), then close, walk to Door B, open radial — Security stays selected on Door B as long as it's available (target_type matches, action_id is in action_lists[0]). If not available, the search falls through to "default to action_lists[0].data[0]". This is by design — vanilla mouse users see the same.

No work needed; just be aware that the announced "Aktion 1/N: <label>" on first open may not be the row's first action — it'll be whatever was last selected for this target_type+row. If that's confusing, we could explicitly write -1 to field1_0x24[type*3 + row] before announcing, but defer until a user reports it.

## Anti-patterns to avoid

- **Do NOT call inner `CSWGuiTargetActionMenu::PopulateMenus @0x00689410` directly.** The wrapper does the AsSWCObject downcast that inner requires; we don't have a clean way to redo that conversion outside the wrapper without re-walking GetGameObject + vtable[AsSWCObject], at which point we're just reimplementing the wrapper. If a future need arises (e.g. wanting to populate against a target that's not the current hover), do it by writing `main_interface->field1_0x64` directly and calling the wrapper.
- **Do NOT iterate modes 0/1/2 by calling inner with different mode params.** The `mode` parameter is only used for visual styling (greyed-out actions when the creature is dead). It doesn't change which actions are surfaced. The 3 rows on the radial are 3 *categories*, not 3 *modes* — they all come from one inner call's `GetTargetActions(player, target, row=0..2)` loop.

## Carried-forward references

- `docs/engine-action-picker.md` — companion doc for the default-action path. Out-of-scope-mentions the radial; this doc is the radial-side companion.
- Memory `project_inner_populate_menus_crashes.md` — pre-fix observation log. Now resolved by this investigation.
- Memory `project_object_handle_namespaces.md` — server vs client handle distinction. Wrapper resolves through CClientExoApp::GetGameObject so client-form handles are correct.
- Decomp: `tools/ghidra-scripts/Decompile.java`. Re-run with `analyzeHeadless ... -postScript Decompile.java <addr> ...` per `docs/tools.md:86`.
