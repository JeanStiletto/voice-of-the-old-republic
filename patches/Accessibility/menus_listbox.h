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

// Diagnostic one-shot dump of CSWGuiFeatsCharGen layout: the four feat
// lists (existing/granted/available/chosen) and the chart's row × col
// cells (feat ID + status per cell). Dedups per-panel-pointer so a
// re-focus of the same panel doesn't re-dump. Silently no-ops on any
// panel whose vtable isn't CSWGuiFeatsCharGen — caller can pass any
// foreground panel without a kind check.
//
// Called from AnnouncePanelTitle. Used to plan main-panel accessibility
// (chart structure, scroll behaviour, which feats appear vs are
// invisible) without committing to a nav design yet.
void DumpFeatsCharGenStructureIfNeeded(void* panel);

// EquipPicker zone state. The picker arms when chain Enter activates an
// equip slot button (BTN_INV_*); it stays armed until Enter commits a row,
// Esc disarms, or the panel disappears from CSWGuiManager.panels[].
//
// menus.cpp's slot-Enter handler calls Arm; the per-tick equip-picker
// monitor (in this TU as of post-Step-5 cleanup) calls IsArmed + Disarm
// when the equip panel is gone.
bool  IsEquipPickerArmed();
void* EquipPickerPanel();
void  ArmEquipPicker(void* panel);
void  DisarmEquipPicker();

// Per-tick fan-out for the 3 listbox-paired monitors:
// MonitorContainerSelection (per-row navigation announces),
// MonitorEquipPickerSelection (mirror for the equip-picker LB_ITEMS), and
// PollContainerGiveModeKey (Win32 poll for the give-mode toggle key — the
// engine's player-control layer eats Tab before menu dispatch). Called
// from menus.cpp's TickMonitors, alongside the general-monitor tick.
void TickListboxMonitors();

}  // namespace acc::menus::listbox
