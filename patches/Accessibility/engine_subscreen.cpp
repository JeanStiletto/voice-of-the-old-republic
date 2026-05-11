#include "engine_subscreen.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "engine_manager.h"  // kAddrGuiManagerPtr, kMgrModalStackSizeOffset
#include "engine_panels.h"   // HasActiveSubScreen, CallPrevSWInGameGui
#include "log.h"

namespace acc::engine {

bool g_switchHookEverFired = false;

namespace {

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

namespace {

// Dispatch the two-step unpause + audio-resync. Idempotent — calling on
// already-clean state is a no-op (SetPauseState short-circuits when bit 2
// is already at the requested value; SetSoundMode(0) just rewrites the
// mode field). Each closing edge (modal pop, sub-screen exit) calls this
// with a trigger label that lands in the diagnostic line.
void DispatchUnpauseCleanup(const char* trigger) {
    acclog::Write("PauseToggle",
                  "%s: SetPauseState(2,0) + SetSoundMode(0)", trigger);

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
}

}  // namespace

void TickInputClassReassert() {
    // Two edges trigger the same unpause cleanup:
    //
    //   1. modal_stack non-zero → 0 — any MessageBoxModal close path
    //      (Alt+F4 quit-confirm, save-overwrite, dialog-skip, …). The
    //      engine's MessageBoxModal close skips HideSWInGameGui, so the
    //      server pause bit + client SetSoundMode stay set; we set both
    //      to 0 directly. Idempotent — no-op when already clean.
    //
    //   2. has-active-sub-screen true → false — last InGame* sub-screen
    //      just left panels[]. Esc on the bare panel goes through
    //      HideSWInGameGui which usually unpauses natively, BUT when the
    //      user came in via Options → Spiel speichern → CSWGuiSaveLoad,
    //      then Esc(SaveLoad) → Esc(Options), the engine's natural path
    //      gets confused: sw_gui_status oscillates 3→4→3 and the final
    //      close leaves the pause bit set + audio muted. The safety-net
    //      catches that case. Critically, this edge ONLY fires when
    //      panels[] transitions from "any sub-screen visible" to "none":
    //      closing SaveLoad while Options stays open keeps the count
    //      non-zero (Options still in panels[]) so we don't unpause
    //      prematurely. Only fully leaving the panel system unpauses,
    //      which is the user-expected behaviour.
    //
    // Both edges call the same idempotent DispatchUnpauseCleanup helper.
    // Double-firing in scenarios where both fire on the same frame is
    // safe — the second call sees already-clean state and short-circuits.
    static int  s_prevModalSize     = 0;
    static bool s_prevHasSubScreen  = false;

    int  modalSize = 0;
    bool hasSubScreen = false;
    __try {
        void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
        if (mgr) {
            modalSize = *reinterpret_cast<int*>(
                static_cast<unsigned char*>(mgr) + kMgrModalStackSizeOffset);
        }
        hasSubScreen = HasActiveSubScreen();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (s_prevModalSize > 0 && modalSize == 0) {
        char tag[64];
        snprintf(tag, sizeof(tag),
                 "popup closed (modal_stack %d->0)", s_prevModalSize);
        DispatchUnpauseCleanup(tag);
    }

    if (s_prevHasSubScreen && !hasSubScreen) {
        DispatchUnpauseCleanup("last sub-screen left panels[]");
    }

    s_prevModalSize    = modalSize;
    s_prevHasSubScreen = hasSubScreen;
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

    // Drill-arm lives in the menus_monitors per-tick poll (AnnounceNewSubScreens),
    // not here. SwitchToSWInGameGui is only the WARM path (sub-screen already
    // showing → switching to a different one); the COLD path is
    // ShowSWInGameGui (in-world → first sub-screen, e.g. Esc → Options), which
    // we don't hook. Arming the flag here would have left first Esc opens
    // un-armed and walked the user through the InGameMenu strip instead of
    // the Options panel. The monitor catches BOTH paths because it polls the
    // resulting state (panels[] contains a sub-screen kind) — robust to any
    // current or future engine open path. This hook stays for its
    // close-on-redrill cleanup role (a real mutation: pop prior sub-screen
    // before the new one pushes).

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

