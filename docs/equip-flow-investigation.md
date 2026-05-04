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

## Recommended next pass

1. Hook `ShowCantEquipMessage` to log every failure (read-only).
2. Try **Path A** first: call `OnItemSelected` then `OnOKPressed`.
   If the gates open, we're done — engine handles slot-fit,
   prerequisites, error messaging.
3. If Path A's gates don't open, fall back to **Path B** with
   explicit `CanEquipItem` check before `EquipItem`.
4. Don't bother with Path C unless A and B both fail — direct
   creature-level equip skips too many checks.

## Implementation pass 1 (UNVERIFIED — 2026-05-04)

Took Path A in hybrid form: rather than calling `OnItemSelected`
directly (which would no-op on the `+0x4c` gate, same way
`FireActivate(BTN_EQUIP)` cold did), we **synthesise a click on
the listbox row** so the engine's own dispatch raises the gates
as a side effect, then `FireActivate(BTN_EQUIP)` — which now
finds `+0x4c=1` and `+0x4270 bit 0=1` and runs `OnOKPressed`
through the normal commit path.

The key fix vs. the prior failed click-sim attempt: row extents
are **listbox-local**, not screen-absolute (point #3 of "why
previous attempts failed"). New helper
`GetListBoxRowScreenCenter(lb, row, ...)` accumulates the
listbox's own screen-absolute origin onto the row's local
extent. One accumulation step is sufficient because LB_ITEMS is
a panel-direct child whose extent is already screen-absolute.

Sequencing: `g_pendingCursorMove` + `g_pendingClick` +
`g_pendingActivate` are scheduled together. `Update()` processes
them in order on a single tick, so `OnItemSelected` (synchronous
inside HandleLMouseUp) raises the gates before FireActivate
fires.

Risks not yet falsified by play-test:
- MoveMouseToPosition could exhibit the Options-style hit-test
  shift on equip rows too. If it does, click lands one row
  above; will be obvious from selection logs.
- `row.extent` may not reflect post-scroll position if the
  engine doesn't relayout listbox children on
  `top_visible_index` change. Edge case for selections beyond
  the initial visible window.
- `ShowCantEquipMessage` not yet hooked, so engine-side
  rejections are silent. Add the hook if equip seems to no-op
  and we can't tell why from logs.

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
- Bytes verified at `0x006b9160` (OnOKPressed), `0x006b8eb0`
  (OnSelectSlot), `0x006b9470` (OnEnterSlot), `0x006b7920`
  (OnItemSelected), `0x006b59f0` (ShowCantEquipMessage),
  `0x006b5760`/`0x006b5890` (EquipItem variants).
