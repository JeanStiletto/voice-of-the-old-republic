#include "probe_world_hover.h"

#include <windows.h>
#include <cstdint>

#pragma comment(lib, "user32.lib")

#include "engine_manager.h"     // kAddrGuiManagerPtr, kAddrMoveMouseToPosition,
                                // PFN_MoveMouseToPosition
#include "engine_offsets.h"     // Vector
#include "engine_player.h"      // kAddrAppManagerPtr,
                                // kAppManagerClientAppOffset,
                                // GetPlayerPosition
#include "log.h"

namespace acc::probe::world_hover {

namespace {

typedef uint32_t (__thiscall* PFN_GetLastTarget)(void* this_);

// Walk *kAddrAppManagerPtr → AppManager + 0x4 → CClientExoApp*. Same chain
// as engine_player's GetPlayerServerObject prelude, separated here because
// the probe doesn't need the rest of the chain (we want the client app to
// call client-side methods, not the server creature).
void* GetClientExoApp() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// 0 = "no target" sentinel. The engine's invalid-handle sentinels in
// other contexts are 0 (uninitialised) or 0xFFFFFFFF (removed); we only
// need to detect change here, not classify validity.
uint32_t ReadLastTargetHandle() {
    void* exoApp = GetClientExoApp();
    if (!exoApp) return 0;
    __try {
        auto fn = reinterpret_cast<PFN_GetLastTarget>(
            kAddrCClientExoAppGetLastTarget);
        return fn(exoApp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// CSWGuiManager mouseOverControl — per the menu-nav click-sim docs, the
// engine's mouseOverControl pointer lives at manager+0x8. Reading it as a
// raw pointer for logging purposes only (no deref — just want to know
// whether warping the cursor changed which control the engine considers
// hovered).
constexpr size_t kMgrMouseOverControlOffset = 0x8;

void* ReadMouseOverControl(void* mgr) {
    if (!mgr) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(mgr) +
            kMgrMouseOverControlOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetGuiManager() {
    __try {
        return *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

void TickMonitor() {
    // Self-gate on player-loaded — in menus / chargen / pre-spawn the
    // probe stays silent (LastTarget is meaningful only when an in-world
    // CClientExoApp has a game-state to report against).
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    static uint32_t s_lastLogged = 0xDEADBEEFu;  // unused sentinel

    uint32_t handle = ReadLastTargetHandle();
    if (handle == s_lastLogged) return;

    acclog::Write("Probe: LastTarget changed: 0x%08x -> 0x%08x",
                  s_lastLogged, handle);
    s_lastLogged = handle;
}

void PollHotkey() {
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    static bool s_prevP = false;
    bool alt = down(VK_MENU);
    bool p   = down('P');
    bool risingP = p && !s_prevP;
    s_prevP = p;

    if (!risingP || !alt) return;

    // Foreground-window gate — copied from cycle_input::PollWin32 so a
    // blind Alt+P in a backgrounded console doesn't fire the probe.
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    void* mgr = GetGuiManager();
    if (!mgr) {
        acclog::Write("Probe: Alt+P -> GuiManager NULL, abort");
        return;
    }

    uint32_t targetBefore = ReadLastTargetHandle();
    void*    moverBefore  = ReadMouseOverControl(mgr);

    acclog::Write("Probe: Alt+P warp BEFORE LastTarget=0x%08x mouseOver=%p",
                  targetBefore, moverBefore);

    // Centre of 640×480 reference frame. The engine's GUI hit-testing uses
    // this internal coord system regardless of actual rendered resolution
    // (per docs/menu-nav-design.md callsite analysis). If world-hover uses
    // a different coord system, this probe will reveal that by *not*
    // changing state — which is itself the answer we want.
    constexpr int kProbeX = 320;
    constexpr int kProbeY = 240;

    __try {
        auto warp = reinterpret_cast<PFN_MoveMouseToPosition>(
            kAddrMoveMouseToPosition);
        warp(mgr, kProbeX, kProbeY);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Probe: Alt+P -> MoveMouseToPosition faulted under SEH");
        return;
    }

    uint32_t targetAfter = ReadLastTargetHandle();
    void*    moverAfter  = ReadMouseOverControl(mgr);

    bool targetChanged = (targetAfter != targetBefore);
    bool moverChanged  = (moverAfter  != moverBefore);

    acclog::Write("Probe: Alt+P warp AFTER  LastTarget=0x%08x mouseOver=%p "
                  "(target_changed=%d mover_changed=%d) warp=(%d,%d)",
                  targetAfter, moverAfter,
                  targetChanged ? 1 : 0,
                  moverChanged  ? 1 : 0,
                  kProbeX, kProbeY);

    if (targetChanged || moverChanged) {
        acclog::Write("Probe: VERDICT MoveMouseToPosition reaches in-world "
                      "hover state — Layer C cursor-warp polish is a free "
                      "ride for Phase 4 view mode");
    } else {
        acclog::Write("Probe: VERDICT MoveMouseToPosition did NOT change "
                      "world-hover state — Layer C requires a separate "
                      "world-hover hook (or a different coord system; try "
                      "the same probe at varied coords before concluding)");
    }
}

}  // namespace acc::probe::world_hover
