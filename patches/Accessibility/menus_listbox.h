// KOTOR Accessibility — listbox-driven panel input dispatcher.
//
// Step 4 of the menus.cpp refactor. Three structurally similar panels
// (Container loot, SaveLoad, EquipPicker item-pick) share a "select-then-
// confirm" interaction shape: arrow keys drive a listbox cursor we
// announce inline, Enter dispatches a panel-specific commit, Esc backs
// out. Each had its own ~80-120 line block in OnHandleInputEvent.
//
// This TU collapses those three blocks into a spec-table-driven dispatcher.
// Each panel is described by a `ListBoxPanelSpec` value: how to match the
// panel, when it's armed, where the listbox lives, how to announce a row,
// what Enter and Esc do, whether to fall through if nothing was consumed.
// Adding a 4th similar panel is one new spec entry plus its callbacks
// (~30 lines), not another copy of the handler scaffolding.
//
// Why a spec table (not just a helper that consolidates the duplicated
// listbox-arrow nav): the divergence isn't decorative — Container's Enter
// is QueueButtonByIdActivate(BTN_OK), SaveLoad's is
// QueueButtonByIdActivate(saveload_button), EquipPicker's is a custom row
// commit (QueueEquipCommit + disarm). The spec encodes that variation as
// onEnter / onEsc callbacks, keeping the dispatcher generic.
//
// EquipPicker state (armed flag + bound panel) lives in this TU because
// the picker's input handler is the primary owner. Two outside touch sites
// in menus.cpp (the slot-Enter arming site + MonitorEquipPickerSelection's
// "panel gone, disarm" cleanup) call the accessors below.

#pragma once

namespace acc::menus::listbox {

// Try to handle the input event against one of the listbox-driven panel
// specs. Returns true if a spec matched and decided the caller should
// return immediately — `outRv` carries the value to return (1 = consumed,
// 0 = not consumed but skip generic handlers, e.g. Container's
// always-return-from-this-handler shape).
//
// Returns false if no spec matched or the matched spec wants the caller
// to fall through to subsequent handlers (SaveLoad / EquipPicker not-armed
// or armed-but-not-consumed both fall through to chain nav / slot-zone).
//
// Logging: on every armed spec the dispatcher emits the standard
// "Menus.Input #N this=PTR key=... val=... [CONSUMED]" line, mirroring the
// inline blocks the original code had at the end of each handler. Specs
// that don't claim the event don't log here — the caller's outer
// log-and-return path takes over.
bool TryHandleInput(int n, void* thisPtr, void* activePanel,
                    int param_1, int param_2, int& outRv);

// EquipPicker zone state. The picker arms when chain Enter activates an
// equip slot button (BTN_INV_*); it stays armed until Enter commits a row,
// Esc disarms, or the panel disappears from CSWGuiManager.panels[].
//
// menus.cpp's slot-Enter handler calls Arm; MonitorEquipPickerSelection
// calls IsArmed + Disarm when the equip panel is gone. The spec's
// onEnter/onEsc/resetStale callbacks read+write directly via the file-
// statics in menus_listbox.cpp.
bool  IsEquipPickerArmed();
void* EquipPickerPanel();
void  ArmEquipPicker(void* panel);
void  DisarmEquipPicker();

}  // namespace acc::menus::listbox
