// KOTOR 2 workbench / crafting item-list screens.
//
// KOTOR 2 restructures the workbench: the category-select panel
// (CSWGuiUpgradeSelection, upgradesel_p.gui) carries the upgradeable-item
// list INLINE (LB_UPGRADELIST, five category filter buttons including
// BTN_ALL), and two new K2-only screens hang off it:
//
//   * CSWGuiCreateItem (component_p.gui) — workbench item creation +
//     breakdown, opened by BTN_CREATEITEMS. Store-shaped: LB_SHOPITEMS
//     (craftable templates) and LB_INVITEMS (inventory to break down)
//     share one screen slot; the engine flips the visibility bit
//     (CSWGuiControl.bit_flags & 0x2) on whichever is active, exactly
//     like the store's buy/sell model. BTN_Examine toggles the views.
//   * CSWGuiCreateMedicalItem (chemical_p.gui) — the lab-station twin
//     (chemicals + Treat Injury instead of components + Repair).
//
// Decompile evidence (2026-08-12 session, kotor2 Ghidra project):
//   * BTN_UPGRADEITEMS handler 0x008c7f60 acts on the LIST'S SELECTED ROW
//     (GetSelectedRow when invoked via the button; row's obj id at +0x1d0).
//   * BTN_Accept handler 0x008d2150 (component) dispatches create
//     (0x008d4ac0) or breakdown (0x008d4680) on the selected row, picked
//     by the create-vs-breakdown flag at panel+0x3ee4.
//   * Esc needs nothing from us: the ctors bind BTN_BACK / BTN_Cancel to
//     the engine's native input code 0x62.
//
// What this module adds for screen readers: chain Enter on a list row
// commits it ("select row, then fire the panel's own commit button" as a
// single deferred op — atomic within one drain tick, so the engine's
// per-frame hover-select can't retarget the selection in between), the
// chain skips rows of whichever item list is hidden, mode flips announce,
// list repopulation rebinds the chain, and first-sight titles come from
// the panel's own localized title label.
//
// All entry points self-gate: they no-op on KOTOR 1 (the panel kinds only
// identify on K2 vtables; the WorkbenchSelect path checks IsKotor2).

#pragma once

namespace acc::menus::crafting {

// If `control` is a row of the actionable item list on a K2 workbench
// screen (WorkbenchSelect's LB_UPGRADELIST, or the VISIBLE item list of a
// crafting screen), fill the commit target and return true. The caller
// queues the deferred op with these values.
//   outListBox — the owning listbox
//   outButtonId — .gui id of the commit button to fire after selecting
//                 (BTN_UPGRADEITEMS = 5 on WorkbenchSelect, BTN_Accept = 12
//                 on both crafting screens)
bool ResolveRowCommit(void* panel, void* control,
                      void** outListBox, int* outButtonId);

// Drain-side worker for the CraftRowCommit pending op: re-verify the row
// still sits in the listbox, write the listbox selection to its index,
// then FireActivate (vtable[15], event 0x27) the commit button resolved
// by .gui id. SEH-guarded throughout; logs the outcome.
void DispatchRowCommit(void* panel, void* listBox, void* row, int buttonId);

// True iff `panel` is a K2 crafting screen and `listBox` is its currently
// HIDDEN item list (LB_SHOPITEMS or LB_INVITEMS with the visibility bit
// clear). RebindChain skips such listboxes so chain navigation can't land
// on rows the user can't act on — same contract as
// store::IsHiddenStoreListBox.
bool IsHiddenCraftingListBox(void* panel, void* listBox);

// True iff this module owns the panel's title speech: K2 only, for the
// whole workbench family (Select / Items / Upgrade / CreateItem /
// CreateMedical). AnnouncePanelTitle early-outs on these so the
// construction-time first-sight stays silent — the K2 workbench dialog
// reply pushes Select+Items+Upgrade STACKED in one tick (live log
// 2026-08-12 13:13:19: four titles + a slot-state line spoke in a row),
// and the panels are pre-built and REUSED across re-entries, so
// pointer-keyed first-sight also never fires again on re-entry. Tick's
// foreground-edge announcer replaces both paths on K2; KOTOR 1 keeps
// the normal first-sight titles (returns false there).
bool OwnsPanelTitle(void* panel);

// Per-tick watcher over the foreground K2 workbench screens:
//   * announces the screen's title when a workbench-family panel ARRIVES
//     in the foreground (replaces first-sight for these panels on K2 —
//     see OwnsPanelTitle). The upgrade slot screen speaks
//     "Aufwerten: <item name>" from its LBL_TITLE, retrying while the
//     label still holds the .gui placeholder "Item Name" (the engine
//     writes the real name a moment after the stacked push);
//   * crafting screens: announces create/breakdown view flips
//     (BTN_Examine or the engine's own toggle) and rebinds the chain;
//   * rebinds the chain when the active list's row count changes
//     (category filter switch, item created / broken down).
// Runs from TickMonitors; cheap no-op when the foreground is unrelated.
void Tick();

}  // namespace acc::menus::crafting
