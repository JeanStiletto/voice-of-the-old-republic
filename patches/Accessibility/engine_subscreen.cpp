#include "engine_subscreen.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "engine_manager.h"  // kAddrGuiManagerPtr, kMgrModalStackSizeOffset
#include "engine_panels.h"   // HasActiveSubScreen, CallPrevSWInGameGui
#include "log.h"
#include "menus.h"           // ClearPendingAnnounce — partner of InvalidateChain
#include "menus_chain.h"     // InvalidateChain — teardown-window stale-pointer guard
#include "prism.h"           // Speak — pause/resume cue
#include "strings.h"         // Id::GamePaused / Id::GameResumed

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

// SetPauseState's first arg is the bit MASK itself (engine does
// `byte | mask` / `byte & ~mask`), NOT a bit index. Bit 0x02 is the
// menu/manual pause source per project_messagebox_close_unpause —
// both CSWCMessage::SendPlayerToServerInput_TogglePauseRequest (Space)
// and every sub-screen / popup open route through this bit.
constexpr unsigned char kPauseBitManualOrMenu = 0x02;

// Live shadow of the server's pause byte. Updated by OnSetPauseState
// on every engine fire. Distinct from "is the byte at +0x178 the live
// state" — Ghidra's `pause_state_` field label was misleading; polling
// that offset returned 0 even when sub-screens paused the game. The
// hook approach sidesteps the question entirely: we accumulate (mask,
// on_off) pairs and that IS the live state by definition.
unsigned char g_pauseShadow = 0;

// Set to true around our own SetPauseState calls so the OnSetPauseState
// hook can recognise its own footsteps and skip the user-facing speech.
// The mask flip still updates g_pauseShadow because the engine state
// did genuinely change — only the announcement is suppressed.
bool g_inOwnPauseCall = false;

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
                // OnSetPauseState fires synchronously inside this call —
                // flag it so the hook stays silent for our own footsteps.
                g_inOwnPauseCall = true;
                fn(server, kPauseBitManualOrMenu, 0);
                g_inOwnPauseCall = false;
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
        // Suppress modal-edge unpause when a sub-screen is still alive:
        // TutorialBox / MessageBoxModal popups can fire on top of an open
        // panel (e.g. a tutorial that appears while the user is in
        // Inventar, or an in-Options confirm dialog). Dismissing the
        // popup leaves the user still inside a panel — pause must stay
        // until the sub-screen itself closes. The sub-screen edge below
        // handles the eventual unpause when panels[] empties of
        // sub-screens.
        //
        // Counter-cases this still covers: Alt+F4 quit-confirm with no
        // sub-screen open (modal pops, dismiss → unpause); save-overwrite
        // confirm during in-world save (same shape).
        if (!hasSubScreen) {
            char tag[64];
            snprintf(tag, sizeof(tag),
                     "popup closed (modal_stack %d->0)", s_prevModalSize);
            DispatchUnpauseCleanup(tag);
        } else {
            acclog::Write("PauseToggle",
                          "popup closed (modal_stack %d->0): SUPPRESSED — "
                          "sub-screen still in panels[]",
                          s_prevModalSize);
        }
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

    // Teardown-window stale-pointer guard. status=4 = sub-screen finishing.
    // The engine is about to free the active panel's child controls; any
    // pointer in g_chain (cached at the last SetActiveControl) becomes
    // stale within the same tick. Drop the chain so MonitorFocusedControl
    // short-circuits on its g_chainCount > 0 gate until OnSetActiveControl
    // rebuilds it against the next live panel.
    //
    // thisPtr here is the CGuiInGame singleton, not the panel — same
    // value across status changes. We invalidate unconditionally on
    // status=4; the chain rebuilds on the very next active-control event
    // (typically the parent panel re-taking focus, or the next screen's
    // first control), so the brief empty-chain window is invisible at
    // the announce layer.
    //
    // Also drop the pending-announce slot. OnSetActiveControl writes it
    // on every focus event, so a SetActive fired just before the area
    // transition (e.g. PartySelection re-taking focus when the "Bist du
    // mit der Zusammenstellung..." MessageBox closes on OK) leaves a
    // dangling control pointer that the next tick's DrainPendingAnnounce
    // would deref through its now-freed vtable. Crash dumps 20996 / 2288
    // (2026-05-22, ESI = the cached Hinzuf. button) caught this exact
    // path during the Versteck → tar_m02aa load.
    if (new_status == 4) {
        acc::menus::chain::InvalidateChain();
        acc::menus::ClearPendingAnnounce();
    }
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

// CServerExoAppInternal::SetPauseState @ 0x004b8110 — fires on every
// pause-state mutation. param_1 is the bit MASK (not an index); param_2
// is the new value (0 = clear bits in mask, 1 = set them). Bit 0x02 is
// the manual+menu pause source. Announce only on bit 0x02 transitions
// that come from outside our own DispatchUnpauseCleanup AND when no UI
// is up (menu opens/closes have their own panel-walk speech).
//
// Engine-side state lives wherever the engine's `pause_state_` field
// actually maps to (Ghidra labelled +0x178 but that offset returned 0
// in live testing — the real offset is elsewhere). We don't need to
// know: g_pauseShadow tracks our own (mask, on_off) accumulator and
// IS the live state by definition once the hook is in place.
extern "C" void __cdecl OnSetPauseState(void* thisPtr,
                                         void* p1_addr,
                                         void* p2_addr) {
    if (!p1_addr || !p2_addr) return;

    int mask = 0;
    int onOff = 0;
    uint32_t caller_eip = 0;
    __try {
        mask  = *reinterpret_cast<int*>(p1_addr);
        onOff = *reinterpret_cast<int*>(p2_addr);
        caller_eip = *(reinterpret_cast<uint32_t*>(p1_addr) - 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Pause", "deref faulted (this=%p)", thisPtr);
        return;
    }

    const unsigned char prev = acc::engine::g_pauseShadow;
    const unsigned char maskByte = static_cast<unsigned char>(mask);
    const unsigned char next = onOff
        ? static_cast<unsigned char>(prev | maskByte)
        : static_cast<unsigned char>(prev & ~maskByte);
    acc::engine::g_pauseShadow = next;

    acclog::Write(
        "Pause", "fire mask=0x%02x on_off=%d shadow %02x->%02x caller=0x%08x%s",
        static_cast<unsigned>(maskByte), onOff,
        static_cast<unsigned>(prev), static_cast<unsigned>(next),
        caller_eip,
        acc::engine::g_inOwnPauseCall ? " (SELF)" : "");

    if (acc::engine::g_inOwnPauseCall) return;
    if ((maskByte & acc::engine::kPauseBitManualOrMenu) == 0) return;

    const unsigned char prevBit = prev & acc::engine::kPauseBitManualOrMenu;
    const unsigned char nowBit  = next & acc::engine::kPauseBitManualOrMenu;
    if (prevBit == nowBit) return;  // idempotent, engine short-circuits anyway

    // UI-state suppression — menu opens, menu closes, and popup show/dismiss
    // all flip bit 0x02. The panel-walk speech is the user's cue in those
    // cases; speaking "Paused"/"Resumed" on top would be noise.
    int modalSize = 0;
    bool hasSubScreen = false;
    __try {
        void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
        if (mgr) {
            modalSize = *reinterpret_cast<int*>(
                static_cast<unsigned char*>(mgr) + kMgrModalStackSizeOffset);
        }
        hasSubScreen = acc::engine::HasActiveSubScreen();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (modalSize > 0 || hasSubScreen) {
        acclog::Write(
            "Pause", "speech SUPPRESSED (modal=%d subscreen=%d)",
            modalSize, hasSubScreen ? 1 : 0);
        return;
    }

    const bool nowPaused = (nowBit != 0);
    prism::Speak(
        acc::strings::Get(
            nowPaused ? acc::strings::Id::GamePaused
                      : acc::strings::Id::GameResumed),
        /*interrupt=*/false);
}

