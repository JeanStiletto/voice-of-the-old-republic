// deferred menu-side operation queue.
//
// (why ops are deferred, single-slot queue, ClickAt as combined move+click).
//
// Body of `Drain` mirrors the original `TickPendingOps` block in menus.cpp:
// same dispatch order (cursor warp → click → activate → equip-select →
// equip-commit → slider), same engine entry points, same diagnostic log
// lines. The only behavioural change is at the queue level — every Queue*
// uniformly debounces against `IsPending()`, where the old code's per-site
// debounce subsets were inconsistent (e.g. EquipPicker Enter checked
// EquipSelect/Commit, but Slider Left/Right didn't).

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "menus_pending.h"

#include "engine_input.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"   // IdentifyPanel / PanelKind — chargen sub-screen close
#include "log.h"
#include "engine_reads.h"    // LookupTlk for workbench slot-type strref
#include "menus.h"           // ClearPendingAnnounce — partner of InvalidateChain
#include "menus_chain.h"     // ValidateChainPanel — post-Activate stale-pointer guard
#include "menus_chargen_attr.h"     // IsChargenAttributesPanel — chargen sub-screen close
#include "menus_chargen_feats.h"    // IsChargenFeatsPanel — chargen sub-screen close
#include "menus_chargen_skills.h"   // IsChargenSkillsPanel — chargen sub-screen close
#include "menus_galaxymap.h"        // DispatchInput for GalaxyInput
#include "menus_journal.h"   // Sort/Swap post-activate list-rebuild repair
#include "menus_listbox.h"   // DisarmWorkbenchUpgradePicker (post-slot-select cleanup)
#include "menus_powers_levelup.h"   // IsPowersLevelUpPanel — chargen sub-screen close
#include "menus_store.h"     // DispatchTradeAction for StoreItemActivate
#include "prism.h"           // Speak — workbench slot-click outcome announce
#include "strings.h"         // WorkbenchSlotInstalled / Removed / NoMatch

namespace acc::menus::pending {

namespace {

enum class Kind {
    None,
    MoveCursor,        // x, y, a = target (self-dedup)
    ClickAt,           // x, y, a = target (self-dedup)
    Activate,          // a = target
    EquipSelect,       // a = panel, b = slot
    EquipCommit,       // a = panel, b = row, c = btn
    WorkbenchSlotSelect,    // a = panel, b = slot
    WorkbenchUpgradeCommit, // a = panel, b = row, c = btnAssemble
    SliderInput,       // a = target, code = direction (500 inc / 501 dec)
    StoreItemActivate, // a = panel (CSWGuiStore), b = row (StoreItemEntry)
    GalaxyInput,       // a = panel (galaxy map), code = engine event,
                       // x = announce-planet flag (0/1)
};

struct PendingOp {
    Kind  kind = Kind::None;
    int   x = 0;
    int   y = 0;
    void* a = nullptr;
    void* b = nullptr;
    void* c = nullptr;
    int   code = 0;
};

PendingOp g_op;

// Kept local to the drain block (not a public surface). Needed for the
// slider HandleInputEvent dispatch; menus.cpp has its own copy in its
// FireActivate. Typedef + constant are non-ODR-violating across TUs.
constexpr int kVtableHandleInputEvent = 15;
typedef void (__thiscall* PFN_ControlHandleInputEvent)(void* this_, int code, int state);

void Reset() {
    g_op = PendingOp{};
}

}  // namespace

bool QueueMoveCursor(int x, int y, void* target) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::MoveCursor;
    g_op.x = x;
    g_op.y = y;
    g_op.a = target;
    return true;
}

bool QueueClickAt(int x, int y, void* target) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::ClickAt;
    g_op.x = x;
    g_op.y = y;
    g_op.a = target;
    return true;
}

bool QueueActivate(void* target) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::Activate;
    g_op.a = target;
    return true;
}

bool QueueEquipSelect(void* panel, void* slot) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::EquipSelect;
    g_op.a = panel;
    g_op.b = slot;
    return true;
}

bool QueueEquipCommit(void* panel, void* row, void* btn) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::EquipCommit;
    g_op.a = panel;
    g_op.b = row;
    g_op.c = btn;
    return true;
}

bool QueueWorkbenchSlotSelect(void* panel, void* slot) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::WorkbenchSlotSelect;
    g_op.a = panel;
    g_op.b = slot;
    return true;
}

bool QueueWorkbenchUpgradeCommit(void* panel, void* row, void* btnAssemble) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::WorkbenchUpgradeCommit;
    g_op.a = panel;
    g_op.b = row;
    g_op.c = btnAssemble;
    return true;
}

bool QueueSliderInput(void* target, int code) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::SliderInput;
    g_op.a = target;
    g_op.code = code;
    return true;
}

bool QueueStoreItemActivate(void* panel, void* row) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::StoreItemActivate;
    g_op.a = panel;
    g_op.b = row;
    return true;
}

bool QueueGalaxyInput(void* panel, int engineCode, bool announcePlanet) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::GalaxyInput;
    g_op.a = panel;
    g_op.code = engineCode;
    g_op.x = announcePlanet ? 1 : 0;
    return true;
}

bool IsPending() {
    return g_op.kind != Kind::None;
}

void Drain(void* gm) {
    if (g_op.kind == Kind::None) return;

    if (!gm) {
        acclog::Write("Update", "pending op but GuiManager singleton is NULL");
        Reset();
        return;
    }

    // CSWGuiManager mouseOverControl pointer at +0x8 (per the decompilation
    // in docs/menu-nav-design.md). Reading it directly lets us verify what
    // the engine's hit-test resolved the cursor to — the difference between
    // "click landed on tab T" and "click landed on the inline listbox" is
    // invisible from cursor coords alone.
    auto getMouseOver = [&]() -> void* {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(gm) + 0x8);
    };

    // Snapshot then clear-then-dispatch. Clearing first means a re-entrant
    // engine callback (which we deliberately decouple from the input hook,
    // but defence-in-depth) sees an empty queue rather than the half-
    // dispatched op.
    PendingOp op = g_op;
    Reset();

    switch (op.kind) {
    case Kind::None:
        break;

    case Kind::MoveCursor: {
        auto move = reinterpret_cast<PFN_MoveMouseToPosition>(kAddrMoveMouseToPosition);
        // Capture mouseOver BEFORE the move. Disambiguates whether
        // MoveMouseToPosition itself produces the 45-px-shifted hit-test
        // result (before==something-else, after==shifted-button) vs. the
        // engine pre-setting mouseOver during panel init (before==after,
        // never refreshed by our move at all). See chat.
        void* moBefore = getMouseOver();
        move(gm, op.x, op.y);
        acclog::Write("Update", "MoveMouseToPosition(%d, %d) target=%p mouseOver before=%p after=%p",
                      op.x, op.y, op.a,
                      moBefore, getMouseOver());
        break;
    }

    // Combined move + click. Click-sim via manager's HandleLMouseDown/Up.
    // Dispatches against mouseOverControl, which on Options-style tabbed
    // panels resolves to the button one step above the chain target
    // (consistent 45-px hit-test shift — see chat investigation). That
    // activates the wrong tab.
    //
    // Direct vtable[6]/[7] on the chain target was tried as a workaround
    // (commit reverted) — it crashes the game on the second tab+ click.
    // The button's own HandleLMouseDown/Up depend on manager-side state
    // (probably the +0x1c mouse_held bit and/or other setup we'd skip).
    // Need a different approach for the off-by-1; for now keep the original
    // pipeline so behavior is at least stable.
    case Kind::ClickAt: {
        auto move = reinterpret_cast<PFN_MoveMouseToPosition>(kAddrMoveMouseToPosition);
        auto down = reinterpret_cast<PFN_ManagerLMouseDown>(kAddrManagerLMouseDown);
        auto up   = reinterpret_cast<PFN_ManagerLMouseUp>(kAddrManagerLMouseUp);
        void* moBefore = getMouseOver();
        move(gm, op.x, op.y);
        acclog::Write("Update", "MoveMouseToPosition(%d, %d) target=%p mouseOver before=%p after=%p",
                      op.x, op.y, op.a,
                      moBefore, getMouseOver());
        moBefore = getMouseOver();
        int dResult = down(gm, /*press=*/1);
        void* moAfterDown = getMouseOver();
        int uResult = up(gm);
        void* moAfterUp = getMouseOver();
        acclog::Write("Update", "click-sim Down=%d Up=%d at (%d,%d) target=%p "
                      "mouseOver before=%p afterDown=%p afterUp=%p",
                      dResult, uResult, op.x, op.y, op.a,
                      moBefore, moAfterDown, moAfterUp);
        break;
    }

    // Direct activate via vtable[15].HandleInputEvent(0x27, 1). Bypasses
    // hit-test, so a button covered by a listbox extent (e.g. Schliess.
    // in an Options sub-dialog) still fires its onClick when we target it.
    //
    // Raise is_active=1 before the dispatch to mirror what HandleLMouseDown
    // does in the engine's mouse pipeline. Several engine onClick handlers
    // gate on `this->is_active != 0` (memory: oncontrolentered_is_active_gate)
    // and silently divert to a different code path when the flag is zero.
    // For MessageBox OK buttons that divergent path appeared to corrupt
    // engine state during close — three Windows __fastfail dumps today all
    // landed between this FireActivate and the next frame, with the same
    // MessageBox foreground each time (quit-confirm, save-overwrite,
    // tutorial-hint — all the same engine MessageBox singleton). Forcing
    // is_active=1 keeps the dispatch on the same code path the mouse
    // pipeline takes.
    //
    // ONLY raise from 0 → 1: HandleLMouseDown's actual behaviour is a
    // conditional raise, not an unconditional write. Some controls (tab
    // buttons in InGameOptions, equip-slot buttons in InGameEquip) carry
    // non-zero engine-bookkeeping values in this field (selected-tab
    // marker, equip-state flags). Clobbering those with 1 corrupts the
    // engine's view of which tab is selected, which surfaces later as a
    // crash in unrelated subsystems on the next render/physics tick. A
    // session FireActivate audit across 9 events found 5→1 (Feedback tab)
    // was the only non-trivial clobber across all of testing, and the only
    // session it appeared in is the one that crashed in CSWRoomSurfaceMesh
    // AABB traversal moments after a tab-subdialog close.
    //
    // Post-activation re-announce is handled generically by
    // MonitorFocusedControl on the next tick: toggles flip +0x1c8 bit 0
    // synchronously inside FireActivate, cycles rewrite the value-display
    // button's CExoString in place, sliders mutate cur_value at +0x74. All
    // three produce a different ExtractAnnounceableText on next entry, and
    // the monitor speaks the diff.
    case Kind::Activate: {
        if (op.a) {
            // Snapshot the panel kind before dispatch — used post-fn to
            // decide whether op.a may have freed itself. Done up front
            // because the LevelUp Annehmen path is exactly when we
            // *can't* re-identify the panel safely after fn() returns.
            acc::engine::PanelKind panelKindAtDispatch =
                acc::engine::IdentifyPanel(acc::menus::chain::g_chainPanel);

            // Capture chargen sub-screen close intent before the
            // dispatch — the sub-screen panel may be freed inside fn()
            // when Annehmen/Abbrechen close it, so reading its vtable
            // afterwards is unsafe. Sub-screens of InGameLevelUp share
            // a .gui-id convention: BTN_ACCEPT=11 ("OK" / "Annehmen"),
            // BTN_BACK=12 ("Abbrechen"). Closing one of these via id
            // 11/12 is the same shape as the InGameOptions sub-screen
            // Esc close that already InvalidateChain's in menus.cpp —
            // without it, the parent InGameLevelUp panel's updated
            // chain entries ("Attribute, nicht verfügbar" etc.) can
            // stay silent on restore because channel-0 dedup and the
            // monitor's last-text cache still hold pre-close text.
            bool chargenSubClosing = false;
            if (panelKindAtDispatch != acc::engine::PanelKind::InGameLevelUp) {
                void* chainPanel = acc::menus::chain::g_chainPanel;
                bool isChargenSub =
                    acc::menus::chargen_attr::IsChargenAttributesPanel(
                        chainPanel) ||
                    acc::menus::chargen_skills::IsChargenSkillsPanel(
                        chainPanel) ||
                    acc::menus::chargen_feats::IsChargenFeatsPanel(
                        chainPanel) ||
                    acc::menus::powers_levelup::IsPowersLevelUpPanel(
                        chainPanel);
                if (isChargenSub) {
                    int btnId = *reinterpret_cast<int*>(
                        reinterpret_cast<unsigned char*>(op.a) + kControlIdOffset);
                    if (btnId == 11 || btnId == 12) {
                        // Disambiguate Annehmen/Abbrechen from same-id
                        // sibling controls (e.g. chargen-skills row arrows
                        // sometimes get id 11/12 too — Sprengstoff's right
                        // arrow has id 11). Annehmen/Abbrechen are labeled
                        // buttons; arrows have empty rendered text.
                        char text[32];
                        bool hasText = acc::engine::ReadButtonText(
                            op.a, text, sizeof(text)) && text[0] != '\0';
                        if (hasText) {
                            chargenSubClosing = true;
                        }
                    }
                }
            }

            uint32_t* isActive = reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(op.a) + kControlIsActiveOffset);
            uint32_t prevIsActive = *isActive;
            if (prevIsActive == 0) *isActive = 1;
            acclog::Write("Update", "FireActivate target=%p is_active=%u%s",
                          op.a, prevIsActive,
                          prevIsActive == 0 ? "->1" : " (preserved)");
            void** vtable = *reinterpret_cast<void***>(op.a);
            if (vtable) {
                auto fn = reinterpret_cast<PFN_ControlHandleInputEvent>(
                    vtable[kVtableHandleInputEvent]);
                if (fn) {
                    acclog::Write("Update", "FireActivate dispatch target=%p vtable=%p fn=%p",
                                  op.a, vtable, fn);
                    fn(op.a, kInputActivate, 1);
                    acclog::Write("Update", "FireActivate returned target=%p", op.a);
                }
            }
            // Self-destroying-button hazard: some buttons free themselves
            // inside the onClick we just dispatched. Annehmen on
            // InGameLevelUp is the known case (commits the level-up AND
            // pops the panel — the button's own memory is freed before
            // fn returns). Within the same tick the engine defers both
            // the panels[] pop and the controls[] removal, and the freed
            // memory still reads as the same engine vtable bytes, so
            // panel-presence, controls[]-membership, and vtable-range
            // checks all see a healthy-looking state. The next tick's
            // MonitorFocusedControl would then dereference the freed
            // pointer in g_chain and FromControl's internal SEH-caught
            // AV would interact with /GS to fastfail the process.
            //
            // The guard must be scoped tightly: nulling the chain entry
            // unconditionally is wrong for toggles, sliders, cycles, and
            // tabs — those mutate state in place, the engine does NOT
            // refocus afterward, and the chain entry needs to survive so
            // the next-tick monitor speaks the new value ("ein"/"aus",
            // updated slider %, new cycle text). Nulling them silently
            // drops the post-toggle announce.
            //
            // Scope: only the InGameLevelUp panel kind, which is the only
            // panel known to free a control synchronously inside Activate.
            // If a future panel kind exhibits the same teardown shape,
            // add it to this gate; don't widen unconditionally.
            if (panelKindAtDispatch == acc::engine::PanelKind::InGameLevelUp) {
                int clearedIdx = -1;
                for (int i = 0; i < acc::menus::chain::g_chainCount; ++i) {
                    if (acc::menus::chain::g_chain[i].control == op.a) {
                        acc::menus::chain::g_chain[i].control = nullptr;
                        clearedIdx = i;
                        break;
                    }
                }
                if (clearedIdx >= 0) {
                    acclog::Write("Update",
                                  "FireActivate post: chain[%d].control=%p "
                                  "nulled (self-destroy-risk, panel=InGameLevelUp)",
                                  clearedIdx, op.a);
                }
            }
            // Chargen sub-screen close — drop the whole chain so the
            // parent InGameLevelUp's chain restore is treated as a fresh
            // binding. Same mechanism the InGameOptions sub-screen Esc
            // path uses (menus.cpp escIsOptionsSub branch). ClearPendingAnnounce
            // partners with InvalidateChain to also flush any in-flight
            // SetActive echo that would carry pre-close text past the
            // chain rebuild.
            if (chargenSubClosing) {
                acc::menus::chain::InvalidateChain();
                acc::menus::ClearPendingAnnounce();
                acclog::Write("Update",
                              "FireActivate post: chain invalidated "
                              "(chargen sub-screen close, target=%p)",
                              op.a);
            }
            // Journal Sort/Swap repair. Both buttons mutate the quest list,
            // which leaves our cached chain pointing at the old (Sort: about
            // to be freed and rebuilt) rows. Sort rebuilds lazily in Draw()
            // next frame, so force a synchronous PopulateItemListBox first or
            // the chain rebuild would capture half-constructed rows (base
            // CSWGuiObject vtable, "control N" text — patch-20260603-090028.log).
            // Swap already repopulated synchronously inside its handler, so
            // just invalidate the chain to re-bind to the new list. The new
            // title is announced separately by the ContentChange monitor.
            if (panelKindAtDispatch == acc::engine::PanelKind::InGameJournal) {
                if (acc::menus::journal::IsSortButton(
                        acc::menus::chain::g_chainPanel, op.a)) {
                    acc::menus::journal::ForceRepopulate(
                        acc::menus::chain::g_chainPanel);
                    acc::menus::chain::InvalidateChain();
                    acclog::Write("Update",
                                  "FireActivate post: journal Sort — "
                                  "repopulated + chain invalidated");
                } else if (acc::menus::journal::IsSwapButton(
                               acc::menus::chain::g_chainPanel, op.a)) {
                    acc::menus::chain::InvalidateChain();
                    acclog::Write("Update",
                                  "FireActivate post: journal Swap — "
                                  "chain invalidated");
                }
            }

            // Companion full-panel-pop guard: if the engine cleared the
            // panel from mgr.panels[] inside the dispatch (rarer — the
            // observed cases defer the pop), drop the whole chain.
            acc::menus::chain::ValidateChainPanel();
        } else {
            acclog::Write("Update", "FireActivate target=NULL (skipped)");
        }
        break;
    }

    // Equip-screen slot activation. Mirrors the engine's mouse-driven path:
    //   1. Raise slot_btn->is_active = 1 (LMouseDown sets this flag normally;
    //      OnSelectSlot's prologue gates on it and returns early if zero).
    //   2. OnEnterSlot(panel, slot_btn) — populates panel.items_listbox with
    //      items matching the slot's type. Sets panel.selected_slot.
    //   3. OnSelectSlot(panel, slot_btn) — if items_listbox.size > 1, stages
    //      the equip (raises panel.field33_0x4270 |= 1, pre-selects row 1,
    //      shows description). If size == 1 (only protoitem template), pops
    //      the engine's "Für diesen Slot..." modal — which is the correct
    //      behaviour for a slot the player has no fitting items for.
    //
    // Dispatched here (not synchronously from the input hook) for the same
    // reason MoveMouseToPosition is — these functions reach deep into GUI
    // state that's mid-update during input dispatch.
    case Kind::EquipSelect: {
        void* panel   = op.a;
        void* slotBtn = op.b;
        if (panel && slotBtn) {
            uint32_t* isActive = reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(slotBtn) + kControlIsActiveOffset);
            uint32_t prevIsActive = *isActive;
            // Conditional raise — see Kind::Activate above for the
            // 5→1 corruption rationale. Equip slot buttons can carry
            // non-zero engine state (slot.equipped_state) we mustn't
            // clobber.
            if (prevIsActive == 0) *isActive = 1;
            auto onEnter  = reinterpret_cast<PFN_InGameEquipOnEnterSlot>(
                kAddrInGameEquipOnEnterSlot);
            auto onSelect = reinterpret_cast<PFN_InGameEquipOnSelectSlot>(
                kAddrInGameEquipOnSelectSlot);
            acclog::Write("Update", "EquipSelect panel=%p slot=%p is_active=%u%s",
                          panel, slotBtn, prevIsActive,
                          prevIsActive == 0 ? "->1" : " (preserved)");
            onEnter(panel, slotBtn);
            onSelect(panel, slotBtn);
            acclog::Write("Update", "EquipSelect done panel=%p slot=%p", panel, slotBtn);
        }
        break;
    }

    // Equip-row commit. OnItemSelected is the function that calls EquipItem
    // and actually equips the item; OnOKPressed is just cleanup (closes the
    // description panel, clears previously_equipped_*). Both gates the
    // engine cares about (description_listbox.bit_flags & 2,
    // items_listbox.bit_flags & 8, panel.field33_0x4270 & 1) were raised
    // earlier by OnSelectSlot — we just need to raise row->is_active and
    // btn_equip->is_active before invoking, mirroring what HandleLMouseDown
    // would do in mouse-driven play.
    case Kind::EquipCommit: {
        void* panel = op.a;
        void* row   = op.b;
        void* btn   = op.c;
        if (panel && row && btn) {
            uint32_t* rowIsActive = reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(row) + kControlIsActiveOffset);
            uint32_t* btnIsActive = reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(btn) + kControlIsActiveOffset);
            uint32_t prevRowActive = *rowIsActive;
            uint32_t prevBtnActive = *btnIsActive;
            // Conditional raise — see Kind::Activate above.
            if (prevRowActive == 0) *rowIsActive = 1;
            if (prevBtnActive == 0) *btnIsActive = 1;
            auto onItem = reinterpret_cast<PFN_InGameEquipOnItemSelected>(
                kAddrInGameEquipOnItemSelected);
            auto onOK   = reinterpret_cast<PFN_InGameEquipOnOKPressed>(
                kAddrInGameEquipOnOKPressed);
            acclog::Write("Update", "EquipCommit panel=%p row=%p btn=%p "
                          "row.is_active=%u%s btn.is_active=%u%s",
                          panel, row, btn,
                          prevRowActive,
                          prevRowActive == 0 ? "->1" : " (preserved)",
                          prevBtnActive,
                          prevBtnActive == 0 ? "->1" : " (preserved)");
            onItem(panel, row);
            onOK(panel, btn);
            acclog::Write("Update", "EquipCommit done panel=%p row=%p btn=%p",
                          panel, row, btn);
        }
        break;
    }

    // Workbench slot activation. Mirrors EquipSelect's shape but against
    // the RE'd CSWGuiUpgrade slot-pick chain at 0x006c3c30 (OnEnterSlot)
    // and 0x006c6500 (OnSlotSelected). Direct call bypasses click-sim
    // because the upgrade.gui labels at IDs 5..11 cover the slot buttons
    // at IDs 12..18 in z-order — MoveMouseToPosition's hit-test resolves
    // mouseOver to a label, so the engine's mouse pipeline never reaches
    // the slot button. OnSlotSelected is the function that populates
    // LB_ITEMS with the inventory mods compatible with the slot.
    case Kind::WorkbenchSlotSelect: {
        void* panel   = op.a;
        void* slotBtn = op.b;
        if (panel && slotBtn) {
            uint32_t* isActive = reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(slotBtn) + kControlIsActiveOffset);
            uint32_t prevIsActive = *isActive;
            // Conditional raise — see Kind::Activate above. Workbench slot
            // buttons carry non-zero engine bookkeeping (slot index + state)
            // we don't want to clobber.
            if (prevIsActive == 0) *isActive = 1;
            auto onEnter  = reinterpret_cast<PFN_CSWGuiUpgradeOnEnterSlot>(
                kAddrCSWGuiUpgradeOnEnterSlot);
            auto onSelect = reinterpret_cast<PFN_CSWGuiUpgradeOnSlotSelected>(
                kAddrCSWGuiUpgradeOnSlotSelected);
            acclog::Write("Update", "WorkbenchSlotSelect panel=%p slot=%p is_active=%u%s",
                          panel, slotBtn, prevIsActive,
                          prevIsActive == 0 ? "->1" : " (preserved)");

            // Slot index lives at slot_btn+0x58 (set by OnPanelAdded). Engine
            // reads it via `edi = [ebx+0x58]` at OnSlotSelected+0x46. The
            // installed-upgrade pointer for each slot lives at
            // panel.field35_0x2f74[slot_idx*4] (4-byte CSWSItem*); the
            // non-saber install path mutates this from 0 → item on install
            // and item → 0 on remove (verified in OnSlotSelected non-saber
            // branch and the InsertUpgrade path).
            int slotIdx = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(slotBtn) + 0x58);
            void** field35Slot = reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(panel) + 0x2f74 + slotIdx * 4);
            void* prevItem = *field35Slot;

            onEnter(panel, slotBtn);
            onSelect(panel, slotBtn);

            void* newItem = *field35Slot;

            // Post-call: decide whether the picker should stay armed.
            // OnSlotSelected branches on panel.field25_0x2f4c:
            //   * value 1 (true lightsaber): builds compatible-mods list
            //     from upcrystals_2da / upgrades_2da and AddControls-
            //     replaces LB_ITEMS — picker UX engages.
            //   * value 2/3/4 (4-slot / 3-slot / 2-slot non-saber categories):
            //     directly installs the upgrade staged by OnPanelAdded
            //     (panel.field_0x2f84[slot_idx] tag lookup against party
            //     inventory), or removes the existing upgrade if the slot
            //     was already filled. LB_ITEMS is NOT populated.
            //
            // If LB_ITEMS row count is still 0 after dispatch, we're in
            // the non-saber install/remove path — disarm the picker and
            // announce the outcome by comparing field35_0x2f74[slot_idx]
            // before vs after.
            uint8_t saberFlag = *(reinterpret_cast<unsigned char*>(panel) + 0x2f4c);
            int lbRows = 0;
            auto* controls = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
            if (controls && controls->data) {
                for (int i = 0; i < controls->size; ++i) {
                    void* c = controls->data[i];
                    if (!c) continue;
                    int cid = *reinterpret_cast<int*>(
                        reinterpret_cast<unsigned char*>(c) + kControlIdOffset);
                    if (cid == 0) {
                        auto* lbList = reinterpret_cast<CExoArrayList*>(
                            reinterpret_cast<unsigned char*>(c) + kListBoxControlsOffset);
                        lbRows = (lbList && lbList->data) ? lbList->size : 0;
                        break;
                    }
                }
            }

            acclog::Write("Update", "WorkbenchSlotSelect done panel=%p slot=%p "
                          "saber=%d slot_idx=%d field35: %p -> %p lb_rows=%d",
                          panel, slotBtn, (int)saberFlag, slotIdx,
                          prevItem, newItem, lbRows);

            if (lbRows == 0) {
                acc::menus::listbox::DisarmWorkbenchUpgradePicker();
                acc::strings::Id outcome = acc::strings::Id::Count_;
                const char* outcomeTag = "no-change";
                if (prevItem == nullptr && newItem != nullptr) {
                    outcome = acc::strings::Id::WorkbenchSlotInstalled;
                    outcomeTag = "installed";
                } else if (prevItem != nullptr && newItem == nullptr) {
                    outcome = acc::strings::Id::WorkbenchSlotRemoved;
                    outcomeTag = "removed";
                } else if (prevItem == nullptr && newItem == nullptr) {
                    outcome = acc::strings::Id::WorkbenchSlotNoMatch;
                    outcomeTag = "no-match";
                }
                // Look up the slot's actual type name from the engine's
                // strref table (e.g. "Vibrationszelle", "Energiezelle",
                // "Sch\xE4rfe") so the user hears WHICH slot they just
                // acted on, not just the generic position number.
                char slotName[128] = {0};
                int tableIdx = (slotIdx - 4) + (int)saberFlag * 4;
                if (tableIdx >= 0 && tableIdx < 16) {
                    auto* entry = reinterpret_cast<unsigned char*>(
                        kAddrUpgradeSlotTypeTable + tableIdx * kUpgradeSlotTypeStride);
                    int upgradeType = *reinterpret_cast<int*>(entry);
                    uint32_t strref = *reinterpret_cast<uint32_t*>(
                        entry + kUpgradeSlotTypeStrRefOff);
                    if (upgradeType != -1 && strref != 0) {
                        acc::engine::LookupTlk(strref, slotName, sizeof(slotName));
                    }
                    acclog::Write("WorkbenchSlotSelect",
                                  "non-saber path (saber=%d slot_idx=%d table_idx=%d "
                                  "upgrade_type=%d strref=%u name=\"%s\") -> %s; picker disarmed",
                                  (int)saberFlag, slotIdx, tableIdx,
                                  upgradeType, strref, slotName, outcomeTag);
                } else {
                    acclog::Write("WorkbenchSlotSelect",
                                  "non-saber path (saber=%d slot_idx=%d table_idx=%d "
                                  "out-of-range) -> %s; picker disarmed",
                                  (int)saberFlag, slotIdx, tableIdx, outcomeTag);
                }
                if (outcome != acc::strings::Id::Count_) {
                    const char* result = acc::strings::Get(outcome);
                    char msg[256];
                    if (slotName[0] != '\0') {
                        snprintf(msg, sizeof(msg), "%s, %s", slotName, result);
                    } else {
                        snprintf(msg, sizeof(msg), "%s", result);
                    }
                    prism::Speak(msg, /*interrupt=*/false);
                }
            }
        }
        break;
    }

    // Workbench upgrade commit. Two-step via direct engine dispatch on
    // the RE'd OnUpgradeSelected (row-stage) + OnAssemble (install).
    // OnAssemble calls FinishUpgrading on the parent UpgradeItemSelect
    // and PopModalPanel — the upgrade panel closes synchronously when
    // this returns. Mirrors EquipCommit's row-then-OK shape.
    case Kind::WorkbenchUpgradeCommit: {
        void* panel = op.a;
        void* row   = op.b;
        void* btn   = op.c;
        if (panel && row && btn) {
            uint32_t* rowIsActive = reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(row) + kControlIsActiveOffset);
            uint32_t* btnIsActive = reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(btn) + kControlIsActiveOffset);
            uint32_t prevRowActive = *rowIsActive;
            uint32_t prevBtnActive = *btnIsActive;
            if (prevRowActive == 0) *rowIsActive = 1;
            if (prevBtnActive == 0) *btnIsActive = 1;
            auto onRow = reinterpret_cast<PFN_CSWGuiUpgradeOnUpgradeSelected>(
                kAddrCSWGuiUpgradeOnUpgradeSelected);
            auto onAsm = reinterpret_cast<PFN_CSWGuiUpgradeOnAssemble>(
                kAddrCSWGuiUpgradeOnAssemble);
            acclog::Write("Update", "WorkbenchUpgradeCommit panel=%p row=%p btn=%p "
                          "row.is_active=%u%s btn.is_active=%u%s",
                          panel, row, btn,
                          prevRowActive,
                          prevRowActive == 0 ? "->1" : " (preserved)",
                          prevBtnActive,
                          prevBtnActive == 0 ? "->1" : " (preserved)");
            onRow(panel, row);
            onAsm(panel, btn);
            acclog::Write("Update", "WorkbenchUpgradeCommit done panel=%p row=%p btn=%p",
                          panel, row, btn);
        } else {
            acclog::Write("Update", "WorkbenchUpgradeCommit panel=%p row=%p btn=%p (skipped)",
                          panel, row, btn);
        }
        break;
    }

    // Slider value adjustment via vtable[15].HandleInputEvent(500/501, 1).
    // The slider's HandleInputEvent runs SetCurValue (clamped to
    // [0, max_value]) and the gui_object callback that propagates to the
    // audio system, then plays the click feedback sound. Per-frame focus
    // monitor catches the cur_value change at +0x74 on the next tick.
    case Kind::SliderInput: {
        if (op.a) {
            void** vtable = *reinterpret_cast<void***>(op.a);
            if (vtable) {
                auto fn = reinterpret_cast<PFN_ControlHandleInputEvent>(
                    vtable[kVtableHandleInputEvent]);
                if (fn) {
                    acclog::Write("Update", "slider HandleInputEvent target=%p code=%d",
                                  op.a, op.code);
                    fn(op.a, op.code, 1);
                }
            }
        }
        break;
    }


    // Store item Enter — dispatch the engine's per-mode click handler
    // with the row as param_1. The engine reads the row's obj_id at
    // +0x1c4 and either opens the confirmation MessageBox or sells/buys
    // directly (depending on its internal cost vs. level threshold).
    // After the engine call, PopulateInventoryItemListBox /
    // PopulateStoreItemListBox repopulates rows; menus_store's per-tick
    // monitor detects the listbox size change and rebinds the chain.
    case Kind::StoreItemActivate: {
        acc::menus::store::DispatchTradeAction(op.a, op.b);
        break;
    }

    // Galaxy-map planet cycle / accept / cancel. DispatchInput calls the
    // panel's own HandleInputEvent (the engine's NextPlanet/PrevPlanet skip
    // hidden / unselectable planets), then re-reads LBL_PLANETNAME when this
    // was an Up/Down cycle (op.x == 1).
    case Kind::GalaxyInput: {
        acc::menus::galaxymap::DispatchInput(op.a, op.code, op.x != 0);
        break;
    }
    }
}

}  // namespace acc::menus::pending
