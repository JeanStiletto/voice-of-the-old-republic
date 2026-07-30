# menus_listbox_picker.cpp (305 lines)

Armed-picker state and per-tick monitors for the two "select a row, then commit it into a slot" panels: the equipment picker (equip.gui LB_ITEMS) and the workbench upgrade picker (upgrade.gui LB_ITEMS). Split out of `menus_listbox.cpp` 2026-07-29 (Phase-2 candidate 23, narrowed form). Eleven of the thirteen listbox specs are stateless; these two carry a *mode* — they ARM when the user activates a slot button and while armed steal arrow keys from the panel's own button chain. That armed flag, its bound panel pointer, and the one-shot cursor-park latch are the only mutable state the whole listbox subsystem owns, so they live here with the monitors that watch them. The SPECS themselves stayed in `menus_listbox.cpp` with the other eleven — moving them would have forced the private `ListBoxPanelSpec` struct into a shared header. Spec callbacks reach this state through the `menus_listbox.h` accessors, which is what `menus.cpp` already did from outside.

Cursor-park mechanism: both pickers set `useEngineSelect`, so arrows drive the engine's own `CSWGuiListBox::SetSelectedControl` (real highlight + native multipage scroll) rather than a raw `selection_index` write. The engine re-selects whatever row sits under the mouse every frame (`HandleMouseMove` → `SetSelectedControl`), silently reverting those writes; parking the OS cursor on the panel's BTN_BACK makes the hover-select inert. The park is deferred to the monitor (Update tick) rather than done at arm time because `MoveMouseToPosition` recurses back through the hover pipeline and must stay off the input-dispatch stack.

## Declarations (in source order)

- Picker state (anonymous ns): `s_equipPickerActive/Panel/ParkPending`, `s_workbenchUpgradePickerActive/Panel/ParkPending`
- Accessors (public, declared in `menus_listbox.h`): `IsEquipPickerArmed`, `EquipPickerPanel`, `ArmEquipPicker`, `DisarmEquipPicker`, `IsWorkbenchUpgradePickerArmed`, `WorkbenchUpgradePickerPanel`, `ArmWorkbenchUpgradePicker`, `DisarmWorkbenchUpgradePicker`
- `struct EquipSelState { listBox, lastSelection }` — the equip monitor's row-change tracker
- `bool ParkPickerCursorOffList(panel, backBtnId, tag)` — warps the OS cursor to BTN_BACK; returns true once issued so the caller clears its park-pending latch
- `void MonitorEquipPickerSelection()` — disarm on panel-gone, one-shot cursor park, per-tick row-change announce. Announce lives here rather than in the spec's `announce` callback because the engine also moves the selection on its own (scroll, hover, the commit itself); watching the index per tick catches every move.
  note: row 0 is the protoitem template, hidden from nav (`minSel=1`), so spoken position is `selIdx` as-is over `rowCount-1`
- `void MonitorWorkbenchUpgradePicker()` — disarm on panel-gone, one-shot cursor park, plus a per-frame selection trace kept from the lightsabercrystalcrash investigation (`acclog::Trace` folds steady values to one line, so it costs no spam)
- `void acc::menus::listbox::TickPickerMonitors()` — fanned out from `TickListboxMonitors` so `menus.cpp` keeps one listbox-side tick entry point

## Talks to

`engine_panels` (`FindPanelByKind`, `IdentifyPanel`), `engine_manager` (`MoveMouseToPosition`), `menus_internal` (`FindControlById`, `GetControlCenter`, the equip + workbench .gui IDs), `menus_extract` (`FromControl` row text), `strings`, `prism`.
