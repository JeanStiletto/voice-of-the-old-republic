#include "engine_subscreen.h"

#include <windows.h>
#include <cstdint>

#include "engine_manager.h"  // kAddrGuiManagerPtr, kMgrModalStackSizeOffset
#include "engine_panels.h"   // HasActiveSubScreen, CallPrevSWInGameGui
#include "log.h"

namespace acc::engine {

bool g_switchHookEverFired = false;

namespace {

// CSWCMessage::SendPlayerToServerInput_TogglePauseRequest @ 0x00677800.
// Static function (no `this` needed — reaches AppManager via the global
// pointer slot). Toggles the server-side game-world pause state — when
// game is paused, walking, NPC AI, time-based events all freeze.
//
// Call chain: SendPlayerToServerInput_TogglePauseRequest → AppManager
// →server → CServerExoApp::TogglePauseState(2). The "2" is a bitmask
// for the "menu pause" source (other bits cover other pause sources
// like dialog auto-pause, console pause, etc.).
//
// Why we need this: HideSWInGameGui (the universal sub-screen close
// primitive) calls TogglePauseRequest on its pause-screen branch
// (`field119_0xb38 == 0`) — that's how Esc-on-pause unpauses the
// world. Alt+F4 / save-overwrite / dialog-skip popups go through
// MessageBoxModal close, NOT HideSWInGameGui — so the engine's
// pause-coupled toggle never fires on close, leaving the world paused
// after the popup dismisses. Walking is gated on the world ticking,
// so it stays frozen.
//
// Signature: undefined4 SendPlayerToServerInput_TogglePauseRequest(void).
// __cdecl (no `this`) per the decompile.
constexpr uintptr_t kAddrTogglePauseRequest = 0x00677800;
using PFN_TogglePauseRequest = int(*)(void);

}  // namespace

void TickInputClassReassert() {
    // Edge-triggered separately for two transitions:
    //   * modal_stack non-zero → 0  (popup just closed)
    //   * any-menu non-zero → 0     (any menu condition just cleared)
    //
    // The popup-close transition is the one that needs the pause toggle:
    // engine's HideSWInGameGui (sub-screen close primitive) calls
    // TogglePauseRequest on its pause-screen branch — but Alt+F4 /
    // save-overwrite / dialog-skip popups go through MessageBoxModal
    // close instead, never hitting that branch. Game stays paused →
    // walking blocked. We compensate by sending TogglePauseRequest
    // ourselves when modal_stack pops to empty.
    //
    // Pause-screen lives in panels[], not modal_stack — so its
    // open/close cycle keeps modal_stack at 0 throughout. Our trigger
    // doesn't fire for it; the engine's HideSWInGameGui correctly
    // unpauses, no double-toggle.
    static int s_prevModalSize = 0;

    int modalSize = 0;
    __try {
        void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
        if (mgr) {
            modalSize = *reinterpret_cast<int*>(
                static_cast<unsigned char*>(mgr) + kMgrModalStackSizeOffset);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (s_prevModalSize > 0 && modalSize == 0) {
        // Modal popup just closed. Engine's MessageBoxModal close path
        // doesn't toggle pause; we do it here.
        auto fn = reinterpret_cast<PFN_TogglePauseRequest>(
            kAddrTogglePauseRequest);
        __try {
            fn();
            acclog::Write("PauseToggle",
                          "popup closed (modal_stack %d->0): "
                          "sent TogglePauseRequest to unpause world",
                          s_prevModalSize);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("PauseToggle",
                          "fault calling TogglePauseRequest");
        }
    }

    s_prevModalSize = modalSize;
}

}  // namespace acc::engine

extern "C" void __cdecl OnSwitchToSWInGameGui(void* thisPtr, int guiId) {
    // First-fire diagnostic — single line per session so absence proves
    // the hook never installed (vs. installing but firing silently in
    // the cold path with no active sub-screen to clean up).
    if (!acc::engine::g_switchHookEverFired) {
        acc::engine::g_switchHookEverFired = true;
        acclog::Write("SubScreen.Switch",
                      "first fire: this=%p GUI_id=%d (hook is alive)",
                      thisPtr, guiId);
    }

    // Only act if there's already a sub-screen alive in panels[]. The
    // very first sub-screen open (cold path: world → strip → Inventory)
    // has nothing to clean up; we let SwitchToSWInGameGui do its normal
    // push without our intervention.
    //
    // For warm paths (sub-screen already open and user opens a different
    // one — via strip Enter, hotkey, or anything else that calls this
    // function), fire PrevSWInGameGui synchronously to pop the current
    // sub-screen. After we return, the original SwitchToSWInGameGui
    // executes its body and pushes the new sub-screen onto a clean
    // panels[]. Both calls land in the same engine call chain, so the
    // sequencing is safe (the engine's own OnQuit→Schliess paths do
    // exactly this kind of in-engine cascade).
    if (!acc::engine::HasActiveSubScreen()) {
        return;
    }

    acclog::Write("SubScreen.Switch",
                  "redrill cleanup: this=%p new GUI_id=%d (closing prior "
                  "sub-screen via PrevSWInGameGui)",
                  thisPtr, guiId);
    acc::engine::CallPrevSWInGameGui();
}

// Diagnostic: every CGuiInGame::SetSWGuiStatus call logs the new status
// value plus the calling instruction's return EIP. Read-only — passes
// through to the original function. The return EIP lets us identify
// which engine code path is closing pause (status 3→1 transitions) so
// we can either suppress that path or compensate.
extern "C" void __cdecl OnSetSWGuiStatus(void* thisPtr,
                                          void* p1_addr,
                                          void* p2_addr) {
    if (!p1_addr || !p2_addr) return;

    int new_status = -1;
    int param_2 = -1;
    uint32_t caller_eip = 0;

    __try {
        new_status = *reinterpret_cast<int*>(p1_addr);
        param_2    = *reinterpret_cast<int*>(p2_addr);
        // [esp+0] (one slot below esp+4) is the return address.
        caller_eip = *(reinterpret_cast<uint32_t*>(p1_addr) - 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("SubScreen.Status",
                      "deref faulted (this=%p p1=%p p2=%p)",
                      thisPtr, p1_addr, p2_addr);
        return;
    }

    acclog::Write("SubScreen.Status",
                  "this=%p new_status=%d p2=%d caller=0x%08x",
                  thisPtr, new_status, param_2, caller_eip);
}

// Diagnostic: every CGuiInGame::HideSWInGameGui call logs the caller
// EIP. Pause-flicker: pause auto-closes via HideSWInGameGui within ~1s
// of opening. The SetSWGuiStatus log confirms HideSWInGameGui's body is
// running (sets status to 4). This hook tells us WHICH engine path
// invokes HideSWInGameGui — the caller_eip is the return address of the
// CALL instruction, so we can identify the engine function that's
// closing pause and decide how to suppress it.
extern "C" void __cdecl OnHideSWInGameGui(void* thisPtr, void* p1_addr) {
    if (!p1_addr) return;

    int param_1 = -1;
    uint32_t caller_eip = 0;

    __try {
        param_1 = *reinterpret_cast<int*>(p1_addr);
        caller_eip = *(reinterpret_cast<uint32_t*>(p1_addr) - 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("SubScreen.Hide",
                      "deref faulted (this=%p p1=%p)", thisPtr, p1_addr);
        return;
    }

    acclog::Write("SubScreen.Hide",
                  "this=%p param_1=%d caller=0x%08x",
                  thisPtr, param_1, caller_eip);
}

