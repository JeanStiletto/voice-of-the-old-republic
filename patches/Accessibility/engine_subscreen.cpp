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
// was detected) but does not run the unpause — keeping the two halves
// of the test comparable.
//
// Iteration history:
//  - v1 (commit 8401763): called SendPlayerToServerInput_TogglePauseRequest
//    only. Server unpaused + walking resumed, but client audio (footstep
//    sounds, nav cues) stayed muted because client-side SetSoundMode was
//    never reset. User had to press Space twice for full resync.
//  - v2 (commit 66dc1dd): called HideSWInGameGui(this, 0) to mirror the
//    Esc-menu close path. Fired too late (engine had already set status=4
//    via SetInputClass) and ran into the field119_0xb38 branch — for
//    popup-close field119 is nonzero, so HideSWInGameGui took the "add
//    pause panel back" branch and skipped TogglePauseRequest entirely.
//    Hook effectively a no-op; world stayed paused.
//  - v3 (this): call BOTH primitives directly, the same primitives
//    HideSWInGameGui's field119==0 / param_1==0 branch and end-of-function
//    call. Bypasses the state-precondition problem entirely.
bool s_pauseToggleEnabled = true;

// CServerExoApp::SetPauseState @ 0x004ae9a0. __thiscall.
// `this` = AppManager->server (CServerExoApp*, at AppManager + 0x08).
// param_1 = source bit (we use 2 = "menu pause" source).
// param_2 = on/off (0 = unpause, 1 = pause).
//
// Idempotent: the internal handler returns early if the bit is already
// at the requested value, so calling SetPauseState(2, 0) on a popup
// close is safe regardless of whether bit 2 was actually set by the
// popup-open path. This is the critical fix over TogglePauseRequest,
// which XOR-toggles and therefore alternates the state on consecutive
// calls when the popup-open path does NOT touch bit 2 itself.
//
// We pass source bit 2 to match the engine's own usage:
// CSWCMessage::SendPlayerToServerInput_TogglePauseRequest passes 2 to
// CServerExoApp::TogglePauseState, so bit 2 is the "menu pause" source
// the Esc-menu close path also clears.
constexpr uintptr_t kAddrSetPauseState = 0x004ae9a0;
using PFN_SetPauseState =
    void(__thiscall *)(void* server, int source_bit, unsigned long on_off);

// AppManager global @ 0x007A39FC (pointer slot, dereference to get the
// CAppManager* singleton). AppManager.server lives at offset 0x08
// (struct definition in docs/llm-docs/re/k1_win_gog_swkotor.exe.xml).
// We duplicate the constant locally instead of including engine_panels.cpp's
// internal constants because the latter are file-local.
constexpr uintptr_t kAddrAppManagerPtrLocal = 0x007A39FC;
constexpr size_t    kAppManagerServerOff    = 0x08;

// CExoSoundInternal::SetSoundMode @ 0x005d5e80. __thiscall.
// `this` = global ExoSound (CExoSoundInternal*). Mode 0 = playing,
// mode 2 = paused-by-combat-mute. CClientExoAppInternal::SetPausedByCombat
// is the engine's canonical caller — it flips mode 2 <-> 0 when entering
// or leaving combat-style pauses. We call it with mode 0 to unmute the
// audio mixer after a MessageBoxModal close.
constexpr uintptr_t kAddrSetSoundMode = 0x005d5e80;
using PFN_SetSoundMode = void(__thiscall *)(void* self, int mode);

// ExoSound global @ 0x007a39ec. Pointer to the CExoSoundInternal
// singleton. Dereference to get the `this` for SetSoundMode.
constexpr uintptr_t kAddrExoSoundPtr = 0x007a39ec;

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
                          "SetPauseState(2,0) + SetSoundMode(0)",
                          s_prevModalSize);

            // 1. Server-side unpause: SET pause source bit 2 = 0
            //    (idempotent — no-op if already clear). Server then
            //    resumes world timers and sends SetPauseState back to
            //    the client; client handler re-enables animations and
            //    timer ticks. We use SetPauseState rather than
            //    TogglePauseRequest because Toggle XORs the bit and so
            //    alternates the state on consecutive popup-closes when
            //    the popup-open path doesn't itself touch bit 2 (which
            //    is what we observed empirically — odd cycles unpaused,
            //    even cycles paused).
            __try {
                void* appMgr =
                    *reinterpret_cast<void**>(kAddrAppManagerPtrLocal);
                if (appMgr) {
                    void* server = *reinterpret_cast<void**>(
                        static_cast<unsigned char*>(appMgr) +
                        kAppManagerServerOff);
                    if (server) {
                        auto fn = reinterpret_cast<PFN_SetPauseState>(
                            kAddrSetPauseState);
                        fn(server, 2, 0);
                    } else {
                        acclog::Write("PauseToggle",
                                      "SetPauseState skipped: server NULL");
                    }
                } else {
                    acclog::Write("PauseToggle",
                                  "SetPauseState skipped: AppManager NULL");
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                acclog::Write("PauseToggle", "fault in SetPauseState");
            }

            // 2. Client-side audio resync: SetSoundMode(0) un-mutes the
            //    audio mixer. The server roundtrip in step 1 does NOT
            //    automatically touch SetSoundMode — that's done
            //    separately by SetPausedByCombat (which has gates we
            //    can't reliably satisfy), or directly here.
            __try {
                void* exoSound =
                    *reinterpret_cast<void**>(kAddrExoSoundPtr);
                if (exoSound) {
                    auto fn = reinterpret_cast<PFN_SetSoundMode>(
                        kAddrSetSoundMode);
                    fn(exoSound, 0);
                } else {
                    acclog::Write("PauseToggle",
                                  "SetSoundMode skipped: ExoSound NULL");
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                acclog::Write("PauseToggle", "fault in SetSoundMode");
            }
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

