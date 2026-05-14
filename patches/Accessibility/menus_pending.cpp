// KOTOR Accessibility — deferred menu-side operation queue.
//
// Step 3 of the menus.cpp refactor. See menus_pending.h for the rationale
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

#include "menus_pending.h"

#include "engine_input.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"   // CallPrevSWInGameGui
#include "log.h"

namespace acc::menus::pending {

namespace {

enum class Kind {
    None,
    MoveCursor,        // x, y, a = target (self-dedup)
    ClickAt,           // x, y, a = target (self-dedup)
    Activate,          // a = target
    EquipSelect,       // a = panel, b = slot
    EquipCommit,       // a = panel, b = row, c = btn
    SliderInput,       // a = target, code = direction (500 inc / 501 dec)
    PrevSWInGameGui,   // no payload — pops current in-game sub-screen
    SwitchSubScreen,   // code = engine GUI_id (0..7)
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

bool QueueSliderInput(void* target, int code) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::SliderInput;
    g_op.a = target;
    g_op.code = code;
    return true;
}

bool QueuePrevSWInGameGui() {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::PrevSWInGameGui;
    return true;
}

bool QueueSwitchSubScreen(int guiId) {
    if (g_op.kind != Kind::None) return false;
    g_op.kind = Kind::SwitchSubScreen;
    g_op.code = guiId;
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
    // Post-activation re-announce is handled generically by
    // MonitorFocusedControl on the next tick: toggles flip +0x1c8 bit 0
    // synchronously inside FireActivate, cycles rewrite the value-display
    // button's CExoString in place, sliders mutate cur_value at +0x74. All
    // three produce a different ExtractAnnounceableText on next entry, and
    // the monitor speaks the diff.
    case Kind::Activate: {
        if (op.a) {
            uint32_t* isActive = reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(op.a) + kControlIsActiveOffset);
            uint32_t prevIsActive = *isActive;
            *isActive = 1;
            acclog::Write("Update", "FireActivate target=%p is_active=%u->1",
                          op.a, prevIsActive);
            void** vtable = *reinterpret_cast<void***>(op.a);
            if (vtable) {
                auto fn = reinterpret_cast<PFN_ControlHandleInputEvent>(
                    vtable[kVtableHandleInputEvent]);
                if (fn) fn(op.a, kInputActivate, 1);
            }
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
            *isActive = 1;
            auto onEnter  = reinterpret_cast<PFN_InGameEquipOnEnterSlot>(
                kAddrInGameEquipOnEnterSlot);
            auto onSelect = reinterpret_cast<PFN_InGameEquipOnSelectSlot>(
                kAddrInGameEquipOnSelectSlot);
            acclog::Write("Update", "EquipSelect panel=%p slot=%p is_active=%u->1",
                          panel, slotBtn, prevIsActive);
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
            *rowIsActive = 1;
            *btnIsActive = 1;
            auto onItem = reinterpret_cast<PFN_InGameEquipOnItemSelected>(
                kAddrInGameEquipOnItemSelected);
            auto onOK   = reinterpret_cast<PFN_InGameEquipOnOKPressed>(
                kAddrInGameEquipOnOKPressed);
            acclog::Write("Update", "EquipCommit panel=%p row=%p btn=%p "
                          "row.is_active=%u->1 btn.is_active=%u->1",
                          panel, row, btn, prevRowActive, prevBtnActive);
            onItem(panel, row);
            onOK(panel, btn);
            acclog::Write("Update", "EquipCommit done panel=%p row=%p btn=%p",
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

    // Pop the active in-game sub-screen via the engine's own primitive.
    // CallPrevSWInGameGui resolves CGuiInGame and dispatches; logs its own
    // diagnostic line. Used by the drill-back Esc handler and (when the
    // close-on-redrill path lands) the strip-icon Enter handler.
    case Kind::PrevSWInGameGui: {
        acc::engine::CallPrevSWInGameGui();
        break;
    }

    // Switch to a different in-game sub-screen (Tab / Shift+Tab cycle).
    // CallSwitchToSWInGameGui goes through our 0x62cf2d detour, which pops
    // any active sub-screen first, so panels[] ends with just the new one.
    case Kind::SwitchSubScreen: {
        acc::engine::CallSwitchToSWInGameGui(op.code);
        break;
    }
    }
}

}  // namespace acc::menus::pending
