#include "diag_input_pipeline.h"

#include <windows.h>
#include <cstdint>

#include "engine_input.h"
#include "engine_manager.h"  // kAddrGuiManagerPtr, modal_stack offsets
#include "log.h"

namespace acc::diag::input {

namespace {

// Process-wide. ULONG so InterlockedIncrement is the simplest tool. Wraps
// after 4G events — fine, the diagnostic only cares about adjacency.
volatile LONG s_seq = 0;

// Per-frame counter incremented in OnProcessInput so log lines can be
// grouped by frame without the reader counting interleaved entries by hand.
volatile LONG s_frame = 0;

}  // namespace

unsigned int NextSeq() {
    return static_cast<unsigned int>(InterlockedIncrement(&s_seq));
}

}  // namespace acc::diag::input

// -----------------------------------------------------------------------------
// CClientExoAppInternal::ProcessInput @ 0x006227e0 — frame entry marker.
//
// Hooked at 0x006227fb (after SEH frame + locals are set up, BEFORE the
// register-save PUSHes write to the relocated cut bytes' positions). At
// hook entry: ECX = this. The 5-byte cut covers PUSH EBX, PUSH EBP, PUSH
// ESI, MOV ESI,ECX — all register-only, position-independent.
//
// We don't actually need `this` to log — the marker just delineates frames.
// But pulling the parameter through is cheap and lets future diagnostics
// read sw_gui_status / input_class without relocating the hook.
extern "C" void __cdecl OnProcessInput(void* /*this_ptr*/) {
    LONG frame = InterlockedIncrement(&acc::diag::input::s_frame);
    unsigned int seq = acc::diag::input::NextSeq();
    acclog::Write("Diag.ProcInput", "frame=%ld seq=%u", frame, seq);
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

    // Bug-1 fix: kill held-Esc auto-repeat. Vanilla case 0xdf calls
    // ClearEvents on the open-pause path but NOT on the close path, and
    // ClearEvents only drains events already in the buffer — it doesn't
    // stop the engine's per-key repeat loop (CExoInputInternal::GetEvents
    // LAB_005e2d32) from emitting a fresh held-key event on the next
    // frame. The user can't release Esc in <300ms (Windows' default
    // repeat threshold), so the second emit reaches the manager (or
    // upstream, depending on the just-changed sw_gui_status) and triggers
    // the inverse state change — pause flickers open then closes, or
    // vice versa.
    //
    // Engine-native cure: CExoInput::CoolDownEvent(eventID, ms) — vanilla
    // already uses this for case 0xda quickload (1000ms). We apply it on
    // every Esc PRESS the upstream sees, covering both 0xdf (Esc2) and
    // 0xb4 (Esc1) since either binding can produce the same symptom.
    // Trade-off: deliberate Esc-double-tap within the cooldown window
    // is suppressed. 350ms covers the OS repeat threshold while leaving
    // headroom for fast double-tap, which is rare for screen-reader users.
    //
    // Skip val=0 (releases) and val=1 (synthesised reissues — those don't
    // come from the held-state tracker so cooldown wouldn't help anyway,
    // and we don't want to swallow the engine's internal re-dispatch).
    if (param_2 == 0x80 &&
        (param_1 == 0xb4 || param_1 == 0xdf))
    {
        acc::engine::CoolDownInputEvent(0xb4, 350);
        acc::engine::CoolDownInputEvent(0xdf, 350);
        acclog::Write("Diag.ClientHIE",
                      "seq=%u Esc cooldown armed (0xb4 + 0xdf, 350ms)", seq);
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
