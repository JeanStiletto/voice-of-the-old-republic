# Equipment screen — engine RE notes

Investigation pass to back out of guess-and-test. Based on reading
`docs/llm-docs/re/k1_win_gog_swkotor.exe.xml` (symbols), the SARIF
analysis (signatures), and Ghidra-headless `DumpBytes.java` runs
against the GoG `.gzf` (raw prologue bytes; verified to match Steam
runtime per `project_ghidra_gog_steam_bytes_match`).

## Why the previous attempts failed

Two distinct dead-ends:

1. **`FireActivate(BTN_EQUIP)`** silently no-ops because BTN_EQUIP
   renders with `is_active=0` until a complete mouse-driven
   sequence (slot click → item click) raises it. Same gate that
   bites Options tab buttons.
2. **Click-sim at BTN_EQUIP's reported center (320, 424)** lands on
   `LBL_TOHIT` instead. Cause TBD — chain step works for slot
   buttons in the same panel, so coords are fine for *some* control
   classes but not others. (Hypothesis: the engine reflows certain
   labels/buttons at runtime so the static `.gui` extents don't
   reflect actual on-screen position. Not investigated further.)
3. **Click-sim at listbox row coords** lands on dead space. Row
   `extent` is listbox-local, not screen-absolute. Would need
   parent-chain offset accumulation we don't currently do.

## CSWGuiInGameEquip — engine handlers

All `__thiscall`. `this` = the equip panel pointer (`CSWGuiInGameEquip*`).

| Address      | Name                  | Signature                                                                         |
|--------------|-----------------------|-----------------------------------------------------------------------------------|
| `0x006b8eb0` | `OnSelectSlot`        | `void(this, CSWGuiControl* slot_btn)`                                             |
| `0x006b9470` | `OnEnterSlot`         | `void(this, CSWGuiControl* slot_btn)`                                             |
| `0x006b7920` | `OnItemSelected`      | `void(this, CSWGuiInGameItemEntry* item_entry)`                                   |
| `0x006b9160` | `OnOKPressed`         | `void(this, void* param_1)` (called as button-onClick — param_1 is the OK btn ptr)|
| `0x006b5760` | `EquipItem` (by id)   | `int(this, ulong slot_id, int p2, int p3)`                                        |
| `0x006b5890` | `EquipItem` (by ptr)  | `int(this, CSWSItem* item, int p2, int p3)`                                       |
| `0x006b5910` | `UnequipItem`         | `void(this, ulong item_id, int p2)`                                               |
| `0x006b59f0` | `ShowCantEquipMessage`| `void(this, int reason, ulong item_id)` — engine surfaces failure reason here     |

### `OnOKPressed` decoded (`0x006b9160`, 94 bytes)

```
8b 44 24 04   mov eax, [esp+4]            ; param_1 (loaded ONCE)
56 57         push esi/edi
8b f1         mov esi, ecx                ; this
8b 48 4c      mov ecx, [eax+0x4c]         ; ecx = param_1->is_active (or +0x4c equivalent)
33 ff         xor edi, edi
3b cf         cmp ecx, edi
74 48         je +0x48                    ; *** if param_1->[+0x4c] == 0, skip equip ***
f6 86 70 42 00 00 01   test [esi+0x4270], 1   ; check this->field33_0x4270 bit 0
74 3f         je +0x3f                    ; *** also gates on internal flag ***
... releases this->previously_equipped_item (+0x42b0) ...
... releases this->field51_0x42b8 ...
... call this->commit_method ...
ret 4
```

So `OnOKPressed` is gated by **two** conditions:

1. `param_1->[+0x4c] != 0` — the source-control's `is_active` bit
2. `this->[+0x4270] & 1` — an internal "ready to equip" flag set by an
   earlier path

Mouse-driven play sets both flags via the prior steps:
- Click on slot → engine processes via `OnSelectSlot`, sets `selected_slot`
- Click on item row → engine processes via `OnItemSelected`, sets
  `this->[+0x42b0] = staged_item`, `this->[+0x4270] |= 1`, raises
  `BTN_EQUIP.is_active = 1`

Without those two prior steps actually firing, `OnOKPressed` no-ops.

### `OnItemSelected` decoded (`0x006b7920`, ~128 bytes)

```
... standard SEH prologue ...
8b f1                  mov esi, ecx                ; this
8b 4d 08               mov ecx, [ebp+8]            ; param_1 = item_entry
8b 41 4c               mov eax, [ecx+0x4c]
85 c0                  test eax, eax
0f 84 bb 04 00 00      je +0x4bb                   ; if entry->[+0x4c] == 0, skip
f6 86 fc 33 00 00 02   test [esi+0x33fc], 2
75 1b                  jnz +0x1b                   ; another internal-state gate
... main path continues ...
```

Same shape — gates on the source `item_entry->[+0x4c]` plus an
internal `this->[+0x33fc] & 2` bit. Probably also requires `selected_slot`
to be set.

## Conclusion — three plausible paths forward

### Path A — replicate the call sequence

In our picker handler, on user Enter:

1. Already done: drive `LB_ITEMS.selection_index` to the user's row.
2. Find that row's `CSWGuiInGameItemEntry*` and call
   `OnItemSelected(this, entry)`.
3. Find `BTN_EQUIP` and call `OnOKPressed(this, btn_equip)`.

Risks:
- Item-entry's `+0x4c` field may not be raised by us setting
  `selection_index`. The mouse-driven path probably sets it via
  `HandleLMouseDown` on the row. So `OnItemSelected` may still no-op.
- Multiple internal flags are involved (`this->[+0x33fc]`,
  `this->[+0x4270]`); we don't fully understand them.

### Path B — direct `EquipItem` call

`CSWGuiInGameEquip::EquipItem(this, slot_id, p2, p3)` is presumably
what `OnOKPressed` eventually calls. If we call it directly with the
right `slot_id` (engine `EquipmentSlots` enum: Head=1, MainHand=16,
OffHand=32, …) and the user's selected item ID, it may equip
without going through the gated UI path.

- `p2`/`p3` semantics unknown. Could be the previously-equipped item
  for swap, plus a flag.
- Risks: bypasses gating but also any validation the gates were
  protecting (slot fit, ability prerequisites). Engine may corrupt
  state or crash if we pass an item that doesn't fit. Need to call
  `CSWSCreature::CanEquipItem` first as a safety check.

### Path C — bypass the equip-screen GUI entirely

`CSWCCreature::EquipToInventorySlot(this, …)` at `0x00613ba0` is the
client-side raw "put this item in this slot" call. The equip screen
ultimately lives on top of this.

- Cleanest in principle: keep the picker UI for slot/item selection,
  but on Enter call the creature's own equip path directly.
- Need to figure out parameter list.
- Need to call `ShowCantEquipMessage` ourselves on rejection so the
  user gets feedback.

## What `ShowCantEquipMessage` does for us

Hooking it (entry-point detour) gives us the engine's failure
reason on every rejected equip — including the cases the user
asked about: "wrong slot", "missing prerequisites", and any others.
The function takes `(this, int reason, ulong item_id)`; we'd log
the reason and use it to localize a spoken message.

Caller xrefs (ANALYSIS xref count would tell us how many call sites)
likely include `EquipItem` itself — so a hook here captures BOTH
GUI-driven and direct-API failures.

## Resolution — direct handler calls (RESOLVED 2026-05-04)

End-to-end equip works (user-confirmed: Kleidung → Körper, weapons
→ left/right hand, Tarnfeldgen → Belt, all show `(Ausgew.)` in
the inventory afterwards). Took none of the originally proposed
paths verbatim — the true model only became visible after
decompiling all four handlers via Ghidra (`tools/ghidra-scripts/
Decompile.java` against Lane's gzf). The earlier "Conclusion"
section above documented a guess; the decompiles corrected
several wrong assumptions, listed at the bottom of this section.

### Final flow (two deferred handler-call pairs)

**Slot Enter (user navigates to a BTN_INV_X and presses Enter):**

1. Set `slot_btn->is_active = 1`. Mouse-driven play sets this in
   `HandleLMouseDown`; `OnSelectSlot`'s prologue gates on it.
2. Call `OnEnterSlot(panel, slot_btn)` directly @ `0x006b9470`.
   This is the function that **populates `items_listbox`** (walks
   the player's `CItemRepository`, filters by
   `equipable_slots & slot`, class restriction, prerequisites,
   appends matching entries via `AddItemEntryToList` +
   `AddControls`). Sets `panel.selected_slot`.
3. Call `OnSelectSlot(panel, slot_btn)` directly @ `0x006b8eb0`.
   Reads `items_listbox.controls.size`: if > 1 (real items), it
   stages the equip (raises `panel.field33_0x4270 |= 1`,
   pre-selects row 1, calls `ShowDescription(this, 1)`,
   `SetEnabled(items_listbox, 1)`, `ShowTutorialWindow`); if == 1
   (only the protoitem template), pops the engine's "Für diesen
   Slot hast du keine Gegenstände" modal (strref `0xa569`) — the
   correct UI feedback for an empty slot.

**Item-row Enter (user navigates LB_ITEMS and presses Enter):**

1. Set `row->is_active = 1` and `btn_equip->is_active = 1`.
   Mouse-driven play sets these via `HandleLMouseDown` on the row
   and (separately) on BTN_EQUIP; both handlers gate on
   `param_1->is_active != 0`.
2. Call `OnItemSelected(panel, row)` directly @ `0x006b7920`.
   **This is the function that commits the equip** — calls
   `EquipItem(this, item_id, this->selected_slot, 1)` after
   passing its three gates: `row->is_active != 0`,
   `description_listbox.bit_flags & 2 != 0` (set by `ShowDescription`
   above), `items_listbox.bit_flags & 8 != 0` (set by
   `SetEnabled(items_listbox, 1)` above). Also handles
   `ShowCantEquipMessage` on prerequisites failure and stages
   swaps when the slot is occupied.
3. Call `OnOKPressed(panel, btn_equip)` directly @ `0x006b9160`.
   **Cleanup only** — clears `previously_equipped_id`/`_item`,
   `field50_0x42b4`/`field51_0x42b8`, calls `CloseDescription`.
   Does NOT call `EquipItem`. (Earlier sections of this doc
   wrongly attributed the commit to `OnOKPressed`; the decompile
   contradicts that.)

Both pairs are deferred to `OnUpdate` — calling deep engine
functions from inside the input-dispatch hook would recurse
through `HandleMouseMove`/`HandleLMouseDown` paths (same toxicity
class as the listbox-entry hooks from session 4 and the reason
`MoveMouseToPosition` is deferred).

### What earlier sections of this doc got wrong

For the historical record (these are still in the "Conclusion"
section above and weren't deleted on resolution):

- **"`OnSelectSlot` sets `selected_slot`."** Actually `OnEnterSlot`
  sets `selected_slot`. `OnSelectSlot` reads it. The mouse-driven
  populate happens on hover, not click — that's why our cold
  click-sim never populated `LB_ITEMS`.
- **"`OnOKPressed` is what `OnItemSelected` eventually calls."**
  Wrong direction. `OnItemSelected` calls `EquipItem` directly;
  `OnOKPressed` is independent cleanup. Path A as originally
  written (item-select → OK-press) is correct in *order* but
  misattributes which call commits.
- **"`OnItemSelected`'s gate is `+0x4c` only."** It actually has
  three gates: `row->is_active`, `description_listbox.bit_flags
  & 2`, and (inside the commit branch) `items_listbox.bit_flags
  & 8`. The latter two are raised by `OnSelectSlot`'s side
  effects, so the order slot→item is mandatory.

### What `ShowCantEquipMessage` is for

Called by `OnItemSelected` itself with `(this, reason, item_id)`
when the engine rejects the equip (failed prerequisites, wrong
class, etc.). Not yet hooked — engine-side rejections are still
silent for screen-reader purposes. Hook this if/when we want to
speak rejection reasons; the engine's own modal-popup path also
fires for the "no items" case so empty-slot feedback is already
audible without it.

### Aborted intermediate attempts (kept here as cautionary notes)

In rough chronological order, none of these worked end-to-end:

1. `FireActivate(BTN_EQUIP)` cold — vtable[15] silently no-ops
   because BTN_EQUIP renders with `is_active=0` until something
   raises the gate.
2. Click-sim at `BTN_EQUIP`'s reported center `(320, 424)` —
   hit-test resolved to `LBL_TOHIT` instead.
3. Click-sim at `LB_ITEMS` row coords with no offset — row
   extents are listbox-local; cursor landed on dead space.
4. `GetListBoxRowScreenCenter` (listbox.extent + row.extent.local)
   — fixed (3) for the cursor coord, but the click still
   dispatched against the listbox container (`mouseOver=LB_ITEMS`),
   so `OnItemSelected` never fired against the row.
5. `FireActivate(slot_btn)` for slot Enter — routed to
   `OnEnterSlot` (the keyboard-shortcut path, called only from
   the Q/E character-switch case `0xce` in
   `CSWGuiInGameEquip::HandleInputEvent`) without raising the
   intermediate state `OnSelectSlot` needs.
6. Click-sim at `BTN_INV_X` center — hit-test consistently
   resolved to one of the `LBL_INV_*` labels (geometry depends on
   the panel layout; not a clean row-pitch shift). The `+50px`
   row-pitch compensation produced different wrong hits, not
   correct ones.
7. Click-sim at `LBL_INV_X` (id = BTN id + 1) center — hit-test
   resolved to *yet another* `LBL_INV_*` (the labels overlap and
   z-order doesn't behave like a simple flat-grid).

The eventual fix bypasses the click pipeline entirely.

## Container "single-item take" — same shape, less RE done

The Container panel (`CSWGuiContainer`) has `HandleInputEvent`,
`GiveItem`, `SetupGiveMode`, etc. but no symbol-named `TakeItem`
function. The take logic is likely embedded in `HandleInputEvent`
or in the protoitem's onClick handler. Investigation deferred —
fixing equip first is the higher-priority case, and the same
lessons (gate-discovery, paired call sequences) will likely apply.

For now Container's "take all" (FireActivate `BTN_OK` with `sel = -1`)
works; per-item take is the broken path and the user can work
around it with take-all.

## Sources

- `docs/llm-docs/re/k1_win_gog_swkotor.exe.xml` — symbols, function
  metadata, code-block ranges
- `docs/llm-docs/re/k1_win_gog_swkotor.exe.sarif` — Ghidra SARIF
  analysis with full signatures and stack-frame layouts
- `tools/ghidra-scripts/DumpBytes.java` — headless Ghidra runner
  for raw prologue bytes
- `tools/ghidra-scripts/Decompile.java` — headless Ghidra runner
  for full decompiled pseudocode of one or more functions by
  entry-point address. The decompiles of `OnSelectSlot`,
  `OnEnterSlot`, `OnItemSelected`, `OnOKPressed` and the panel's
  `HandleInputEvent` were what unblocked this investigation
  after the click-sim approaches had run out.
- Bytes verified at `0x006b9160` (OnOKPressed), `0x006b8eb0`
  (OnSelectSlot), `0x006b9470` (OnEnterSlot), `0x006b7920`
  (OnItemSelected), `0x006b59f0` (ShowCantEquipMessage),
  `0x006b5760`/`0x006b5890` (EquipItem variants).
