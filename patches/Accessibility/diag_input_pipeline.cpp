#include "diag_input_pipeline.h"

#include <windows.h>
#include <cstdint>

#include "actionbar_menu.h"    // CurrentSelection — read the user's last
                               // chosen variant per slot so bare 4..7
                               // fires the same variant the submenu last
                               // announced
#include "engine_actionbar.h"  // PrepareBareDispatch — keeps action_lists
                               // fresh against narrated_target so the
                               // engine's bare 1..7 switch hits a valid
                               // creature_id instead of stale data;
                               // SelectVariant — stamp the engine's
                               // selected_action_id so DoPersonalAction
                               // fires the user's choice, not variant 0
#include "engine_area.h"       // ResolveServerObjectHandle (sanity-check the
                               // narrated handle still resolves to a live
                               // game object before stamping it)
#include "engine_input.h"
#include "engine_radial.h"     // SelectActionInRow — stamp field1[target_type*3
                               // +row] so DoTargetAction fires the user's
                               // last-cycled variant for bare 1..3
#include "engine_manager.h"  // kAddrGuiManagerPtr, modal_stack offsets
#include "engine_offsets.h"
#include "engine_player.h"   // GetPlayerServerCreature
#include "log.h"
#include "narrated_target.h" // TryGet — pull the unified "current focus"
                             // slot to drive deterministic targeting
#include "passive_narrate.h" // ReannounceCurrentShowObjectTarget — Q/E
                             // re-announce path for the single-enemy
                             // combat case where the engine's cycle
                             // is a no-op but the user still expects
#include "target_action_menu.h"  // CurrentSelection — read the user's last
                                  // chosen variant per row so bare 1..3
                                  // fires the same variant the submenu
                                  // last announced
                             // an audible confirmation

namespace acc::diag::input {

namespace {

// Process-wide. ULONG so InterlockedIncrement is the simplest tool. Wraps
// after 4G events — fine, the diagnostic only cares about adjacency.
volatile LONG s_seq = 0;

}  // namespace

unsigned int NextSeq() {
    return static_cast<unsigned int>(InterlockedIncrement(&s_seq));
}

}  // namespace acc::diag::input

// -----------------------------------------------------------------------------
// CClientExoAppInternal::ProcessInput @ 0x006227e0 — frame-boundary seq tick.
//
// Hooked at 0x006227fb (after SEH frame + locals are set up, BEFORE the
// register-save PUSHes write to the relocated cut bytes' positions). At
// hook entry: ECX = this. The 5-byte cut covers PUSH EBX, PUSH EBP, PUSH
// ESI, MOV ESI,ECX — all register-only, position-independent.
//
// Originally emitted "Diag.ProcInput: frame=N seq=M" each frame as part of
// the input-routing investigation (Esc/pause Bug 1 + 2a, both fixed). The
// visible per-frame line was deleted because at 60 fps it was 99.8% of
// log volume. The seq bump stays so frame boundaries remain encoded as
// gaps in the Diag.ClientHIE / Menus.Input streams. If a future
// investigation needs explicit frame markers, restore the acclog::Write
// call here — one line of code.
extern "C" void __cdecl OnProcessInput(void* /*this_ptr*/) {
    acc::diag::input::NextSeq();
}

// -----------------------------------------------------------------------------
// CClientExoAppInternal::HandleInputEvent @ 0x00621210 — upstream client-app
// route. Captures every event ProcessInput dispatches via the upstream branch
// (in-world hotkeys, screenshot, pause, party-cycle, target actions, Esc
// path, hotkeys 0xd1-0xd8, etc.). Pair with the existing manager-route hook
// to see both routes; events that appear in both with adjacent seq values
// indicate upstream→manager synthesis (the val=1 hypothesis from the doc).
//
// Hooked at function entry. 5-byte cut covers PUSH ECX/EBX/EBP/ESI/EDI —
// all register-only. ECX = this; param_1 / param_2 live at [esp+4] /
// [esp+8] in the engine's frame at hook time.
//
// `source = "esp+X"` emits LEA per project_kpatchmanager_lea_bug — the
// handler receives the *address* of each stack slot and dereferences once.
// We also reach back to (slot - 1) for the return EIP, useful for
// distinguishing direct ProcessInput dispatches from synthetic re-issues
// (the doc-mentioned LAB_00622111 path inside `case 0xdf`).
extern "C" void __cdecl OnClientHandleInputEvent(void* this_ptr,
                                                 void* p1_addr,
                                                 void* p2_addr) {
    if (!p1_addr || !p2_addr) return;

    int param_1 = 0;
    int param_2 = 0;
    uint32_t caller_eip = 0;

    __try {
        auto* slot1 = reinterpret_cast<int*>(p1_addr);
        auto* slot2 = reinterpret_cast<int*>(p2_addr);
        param_1 = *slot1;
        param_2 = *slot2;
        // [esp+0] (one slot below esp+4) holds the caller's return address.
        caller_eip = *(reinterpret_cast<uint32_t*>(p1_addr) - 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Diag.ClientHIE",
                      "deref faulted (this=%p p1=%p p2=%p)",
                      this_ptr, p1_addr, p2_addr);
        return;
    }

    unsigned int seq = acc::diag::input::NextSeq();
    int translated = acc::engine::ManagerTranslateCode(param_1);
    if (translated != param_1) {
        acclog::Write("Diag.ClientHIE",
                      "seq=%u this=%p caller=0x%08x key=logical(%d) -> %s(%d) val=%d",
                      seq, this_ptr, caller_eip, param_1,
                      acc::engine::InputIndexName(translated), translated,
                      param_2);
    } else {
        acclog::Write("Diag.ClientHIE",
                      "seq=%u this=%p caller=0x%08x key=%s(%d) val=%d",
                      seq, this_ptr, caller_eip,
                      acc::engine::InputIndexName(param_1), param_1, param_2);
    }

    // Bare 1..7 dispatch prep. Decompile session 2026-05-21 confirmed:
    //   * The engine's keymap routes bare 1..3 → logical action codes
    //     0xe2/0xe4/0xe6 (DoTargetAction row 0/1/2).
    //   * Bare 4..7 → 0xe8/0xea/0xec/0xee. Slot mapping is NOT linear:
    //         key 4 → DoPersonalAction(slot=0)  Friendly Force
    //         key 5 → DoPersonalAction(slot=1)  Medical
    //         key 6 → DoPersonalAction(slot=3)  ← Mine (engine swaps)
    //         key 7 → DoPersonalAction(slot=2)  ← Misc (engine swaps)
    //   * Both DoTargetAction and DoPersonalAction read `creature_id` from
    //     the action-list item (CSWGuiInterfaceAction+0x1c) and bail
    //     silently when GetGameObject(creature_id) returns NULL.
    //   * action_list items are only repopulated by
    //     CSWGuiMainInterface::PopulateMenus, which the engine triggers on
    //     combat-mode flip, sub-screen close, or mouse passive-cursor
    //     hover — NOT on Q/E target change. Between rounds the items
    //     carry whatever creature_id was last baked (often the wrong
    //     target — the grenades-at-friends footgun).
    //
    // Closing the gap synchronously here means the engine's switch-case
    // (which runs after our void handler returns + the wrapper's cut
    // bytes execute) lands on action_lists rebuilt against our
    // deterministically-chosen target.
    //
    // Target choice:
    //   - If acc::narrated_target::TryGet returns a game-object slot AND
    //     the handle still resolves to a live server creature: stamp it.
    //     The narrated slot's TryGet does its own staleness check, but
    //     we re-resolve here too because PopulateMenus internally hits
    //     GetGameObject and we want the same answer to feed both calls.
    //   - Otherwise stamp kInvalidObjectId (0x7F000000). PopulateMenus
    //     then builds empty target_actions and, for personal-action
    //     items the engine flags hostile-targeted, leaves creature_id
    //     unresolved — engine bails silently instead of mistargeting.
    //     Self-buff personal items (Medikit (Selbst), stims) still get
    //     creature_id=player from GetPersonalActions regardless, so
    //     pressing 5 still fires the medikit even without a narrated
    //     enemy.
    //
    // Engine convention is client-side handles (high bit 0x80000000);
    // narrated_target stores server-side. OR the bit before stamping.
    if (param_2 != 0 &&
        (param_1 == 0xe2 || param_1 == 0xe4 || param_1 == 0xe6 ||
         param_1 == 0xe8 || param_1 == 0xea || param_1 == 0xec ||
         param_1 == 0xee))
    {
        uint32_t targetClient = 0x7F000000u;
        acc::narrated_target::Slot slot{};
        if (acc::narrated_target::TryGet(slot) && !slot.isMapPin &&
            slot.handle != 0u && slot.handle != 0x7F000000u)
        {
            void* obj = acc::engine::ResolveServerObjectHandle(slot.handle);
            if (obj) {
                targetClient = (slot.handle & 0x80000000u)
                    ? slot.handle
                    : (slot.handle | 0x80000000u);
            }
        }
        // Fire the refresh. PrepareBareDispatch logs its own status line
        // (`ActionBar.Prep: target=0x... — SetTarget + RePopulate done`)
        // — between that and the upstream `Diag.ClientHIE: ... key=?(N)
        // val=128` line printed earlier in this same call, the trigger
        // and its outcome are both already in the log without us adding
        // a third line per press.
        (void)acc::engine_actionbar::PrepareBareDispatch(targetClient);

        // Stamp the engine's per-column selected_action_id with the
        // user's last-chosen variant for the personal-action keys
        // (0xe8/0xea/0xec/0xee = bare 4/5/6/7). DoPersonalAction
        // reads `*(mi + 0x1bac + slot*4)` and falls back to variant
        // 0 if the field doesn't match any list entry — and
        // PopulateMenus (called by PrepareBareDispatch above) freshly
        // assigns action_ids, invalidating any previously-stamped
        // value. So we re-stamp AFTER RePopulate, using the current
        // descriptor's action_id at the index our submenu shadow
        // tracks.
        int barSlot = -1;
        switch (param_1) {
            case 0xe8: barSlot = 0; break;
            case 0xea: barSlot = 1; break;
            case 0xec: barSlot = 3; break;
            case 0xee: barSlot = 2; break;
        }
        if (barSlot >= 0) {
            void* mi = acc::engine_actionbar::ResolveMainInterface();
            if (mi) {
                int idx = acc::actionbar_menu::CurrentSelection(barSlot);
                (void)acc::engine_actionbar::SelectVariant(mi, barSlot, idx);
            }
        }

        // Same restamp for the target-action keys (0xe2/0xe4/0xe6 = bare
        // 1/2/3). DoTargetAction reads field1[target_type*3+row] and
        // searches action_lists[row] for the matching action_id, falling
        // back to data[0] on no-match. PopulateMenus reassigns action_ids
        // just like for personal columns, so we re-stamp at the shadow
        // index target_action_menu tracks. Without this, a Shift+1+cycle
        // session would only affect Enter inside the submenu, not the
        // subsequent bare 1 fires — breaking parity with Shift+4..7.
        int targetRow = -1;
        switch (param_1) {
            case 0xe2: targetRow = 0; break;
            case 0xe4: targetRow = 1; break;
            case 0xe6: targetRow = 2; break;
        }
        if (targetRow >= 0) {
            void* tam = acc::engine_radial::ResolveTargetActionMenu();
            if (tam) {
                int idx = acc::target_action_menu::CurrentSelection(targetRow);
                (void)acc::engine_radial::SelectActionInRow(tam, targetRow, idx);
            }
        }
    }

    // Q/E hostile-cycle re-announce. The engine's HandleInputEvent
    // case 0xcc (E=next, 204) / 0xcd (Q=prev, 205) dispatches to
    // SelectNearestObject + ShowObject; the sighted player sees the
    // red hostile-hilite shift to the new target (or stay put if only
    // one valid target exists in 30u). Our delta-detection paths
    // (Tick poll on last_target, eventually ShowObject delta) catch
    // the multi-target case fine, but the single-enemy case is
    // engine-side a no-op — no transition, no announcement, blind
    // player has no audible confirmation that they're still on the
    // same target. Force-announce on every Q/E press from the cached
    // ShowObject value (immune to AI churn that writes last_target
    // every combat round).
    //
    // Press-edge only (val != 0) so the release doesn't re-fire.
    // ReannounceCurrentShowObjectTarget self-gates on player-loaded
    // and silently no-ops when no engine target exists.
    if (param_2 != 0 && (param_1 == 204 || param_1 == 205)) {
        acc::passive_narrate::ReannounceCurrentShowObjectTarget();
    }

    // Bug-2a fix: arrow-key navigation in modal popups. When the engine
    // pushes a MessageBox (Alt+F4 quit-confirm, save-overwrite warning,
    // dialog-skip prompt, …) it lands on modal_stack but DOES NOT change
    // sw_gui_status — popups are modals, not sub-screens. So
    // ProcessInput's routing rule (which uses sw_gui_status==1 as the
    // primary gate to client-app) sends arrow keys 0xb6-0xb9 here to the
    // upstream-app handler. Upstream's switch has no case for direction
    // codes → they fall to the default return-zero arm and are silently
    // dropped. The manager (where the modal would receive them) never
    // sees them, so the user can't move focus between popup buttons.
    //
    // Fix: when modal_stack is non-empty, forward direction codes
    // ourselves by calling CSWGuiManager::HandleInputEvent directly. This
    // mirrors what the engine SHOULD do but doesn't. Press-only —
    // releases for nav arrows are no-ops in the panel-class overrides
    // anyway (see project memory note on CSWGuiNavigable: focus walk runs
    // on press, release falls through to event_list scan but no
    // subscribers exist for direction codes on most panels).
    //
    // Engine address @ 0x0040c8e0 from project memory (existing manager
    // hook lives mid-function at 0x0040c907). Manager pointer is the same
    // global slot the existing modal-stack reader uses.
    if (param_2 != 0 &&
        param_1 >= 0xb6 && param_1 <= 0xb9)
    {
        void* mgr = nullptr;
        int modalSize = 0;
        __try {
            mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
            if (mgr) {
                auto* base = reinterpret_cast<unsigned char*>(mgr);
                modalSize = *reinterpret_cast<int*>(
                    base + kMgrModalStackSizeOffset);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            mgr = nullptr;
        }

        if (mgr && modalSize > 0) {
            using PFN_MgrHIE = void(__thiscall*)(void*, int, int);
            constexpr uintptr_t kAddrMgrHIE = 0x0040c8e0;
            auto fn = reinterpret_cast<PFN_MgrHIE>(kAddrMgrHIE);
            __try {
                fn(mgr, param_1, param_2);
                acclog::Write("Diag.ClientHIE",
                              "seq=%u arrow forwarded to manager (modal_stack=%d)",
                              seq, modalSize);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                acclog::Write("Diag.ClientHIE",
                              "seq=%u arrow-forward fault (mgr=%p)",
                              seq, mgr);
            }
        }
    }
}
