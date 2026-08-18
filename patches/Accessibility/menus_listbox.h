// listbox-driven panel input dispatcher.
//
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

// Title-speech override lookup. If `panel` matches a listbox spec that
// supplies its own title text (e.g. SkillInfoBox carries a BioWare dev
// placeholder string baked into skillinfo.gui — see
// `ChargenFeatGrantedTitle`), returns the localised replacement string.
// Returns nullptr if no spec matches or the matched spec has no override.
//
// Called from menus.cpp's AnnouncePanelTitle before the generic
// label-walk so any spec that knows its title is broken-by-default can
// substitute correct speech. Lifting this hook into the spec table
// keeps panel-specific knowledge in one place per panel, instead of
// scattering `if (kind == X) speak("…")` checks into the title path.
const char* GetTitleOverride(void* panel);

// ---------------------------------------------------------------------------
// Armed-picker state. Owned by menus_listbox_picker.cpp — see that file's
// header for why these two panels carry a mode when the other eleven specs
// don't. Everything that touches the state goes through these accessors,
// including the spec callbacks in menus_listbox.cpp.
// ---------------------------------------------------------------------------

// EquipPicker zone state. "Armed" is the ENGINE's picker-open bit
// (kEquipPickerOpenFlagOff), not a flag of ours — the same engine call that
// raises it disables the nine slot buttons underneath, so anything we kept
// beside it could disagree with what the screen actually does. IsArmed is pure
// and safe to call several times a frame.
//
// Arm sets only a one-tick LATCH covering the window between queueing the
// engine's open call and the engine raising its own bit; the picker monitor
// retires that latch. ClearArmLatch means "we are done driving this picker" and
// deliberately leaves the engine's bit alone — the engine clears it when the
// commit or cancel we queued reaches CloseDescription, and until then the slots
// really are still disabled.
// `slotBtn` is the slot button whose Enter queued the open; the monitor
// keeps it for the latch's lifetime so a refused open (engine bit never
// rises — KOTOR 2 pops its "no items for this slot" modal instead) can
// still name the item that stays equipped in that slot.
bool  IsEquipPickerArmed();
void* EquipPickerPanel();
void  ArmEquipPicker(void* panel, void* slotBtn);
void  ClearEquipPickerArmLatch();

// Workbench upgrade picker — arms when the chain Enter handler activates
// a BTN_UPGRADE3X/4X slot button (populating LB_ITEMS with the slot's
// compatible inventory mods). Same engine-owned model as the equip picker
// above, reading kUpgradePickerOpenFlagOff.
//
// WorkbenchUpgradePickerPanel() is the upgrade panel while the picker is up
// (null otherwise); the spec's announce callback needs it to ask
// GetWorkbenchPickerInfo which slot layout is on screen (the row-0 remove entry
// exists on power slots but not on the colour slot).
bool  IsWorkbenchUpgradePickerArmed();
void* WorkbenchUpgradePickerPanel();
void  ArmWorkbenchUpgradePicker(void* panel);
void  ClearWorkbenchUpgradeArmLatch();

// Per-tick fan-out for the listbox-paired monitors that live in this TU:
// MonitorContainerSelection (per-row navigation announces) and
// PollContainerGiveModeKey (Win32 poll for the give-mode toggle key — the
// engine's player-control layer eats Tab before menu dispatch). Also calls
// TickPickerMonitors. Called from menus.cpp's TickMonitors, alongside the
// general-monitor tick.
void TickListboxMonitors();

// The two armed-picker monitors (equip + workbench upgrade), in
// menus_listbox_picker.cpp. Fanned out from TickListboxMonitors so menus.cpp
// keeps a single listbox-side tick entry point.
void TickPickerMonitors();

// Registers the picker-owned message-buffer rules. Called from
// msg_router.cpp's EnsureRulesRegistered, FIRST — the rule it adds claims the
// engine's inventory feedback while the equip picker previews items, and rules
// are first-match-wins, so it has to sit ahead of the combat rules.
void RegisterPickerMsgRules();

}  // namespace acc::menus::listbox
