#include "engine_subscreen.h"

#include <windows.h>
#include <cstdint>

#include "engine_manager.h"  // kAddrGuiManagerPtr, kMgrModalStackSizeOffset
#include "engine_panels.h"   // HasActiveSubScreen, CallPrevSWInGameGui
#include "log.h"
#include "tolk.h"

namespace acc::engine {

bool g_switchHookEverFired = false;

namespace {

// Runtime toggle for the modal-pop close-handler. Default ON. Alt+U at
// runtime flips this so we can A/B test against "no hook" without
// rebuilding. When OFF, TickInputClassReassert still tracks modal_stack
// edges and logs the transition (so the log records that the popup-close
// was detected) but does not call HideSWInGameGui — keeping the two
// halves of the test comparable.
//
// Earlier iteration of this hook called
// CSWCMessage::SendPlayerToServerInput_TogglePauseRequest (0x00677800)
// directly. That only flipped the pause bitmask and left the audio
// subsystem out of sync — walking + NPC narration resumed but footstep
// audio and nav cues stayed silent until the user manually pressed Space
// twice. Current iteration mimics the Esc-menu close path exactly:
// CSWGuiInGameOptions::HandleInputEvent (0x006aaec0) calls
// CGuiInGame::HideSWInGameGui(this, 0) on Esc — and that path produces
// the full unpause + audio resync.
bool s_pauseToggleEnabled = true;

}  // namespace

void TickInputClassReassert() {
    // Edge-triggered on modal_stack non-zero → 0 (popup just closed).
    //
    // Engine's HideSWInGameGui is invoked by CSWGuiInGameOptions on Esc-
    // close of the in-game save/load menu and does the full unpause +
    // audio resync. MessageBoxModal close (Alt+F4 quit-confirm, save-
    // overwrite, dialog-skip, …) skips HideSWInGameGui — world stays
    // half-paused. Mirror the engine's path by invoking
    // HideSWInGameGui(this, 0) on modal pop.
    //
    // Pause-screen / sub-screens live in panels[], not modal_stack — so
    // their open/close cycle keeps modal_stack at 0 throughout. Our
    // trigger doesn't fire for them; the engine's own HideSWInGameGui
    // path runs on its own and we don't double-invoke.
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
        if (s_pauseToggleEnabled) {
            acclog::Write("PauseToggle",
                          "popup closed (modal_stack %d->0): "
                          "invoking HideSWInGameGui(0) (Esc-menu mirror)",
                          s_prevModalSize);
            CallHideSWInGameGui(0);
        } else {
            acclog::Write("PauseToggle",
                          "popup closed (modal_stack %d->0): "
                          "DISABLED (Alt+U) — hook suppressed",
                          s_prevModalSize);
        }
    }

    s_prevModalSize = modalSize;
}

void PollPauseToggleHotkey() {
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    static bool s_prevU = false;
    bool alt = down(VK_MENU);
    bool u   = down('U');
    bool risingU = u && !s_prevU;
    s_prevU = u;

    if (!risingU || !alt) return;

    // Foreground-window gate — don't fire if KOTOR isn't focused.
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    s_pauseToggleEnabled = !s_pauseToggleEnabled;
    acclog::Write("PauseToggle",
                  "Alt+U: hook %s",
                  s_pauseToggleEnabled ? "ENABLED" : "DISABLED");
    tolk::Speak(s_pauseToggleEnabled ? L"Pausen-Hook an"
                                     : L"Pausen-Hook aus",
                /*interrupt=*/true);
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

