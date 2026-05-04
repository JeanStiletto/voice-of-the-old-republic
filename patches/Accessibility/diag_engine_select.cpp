#include "diag_engine_select.h"

#include <windows.h>
#include <cstdint>

#pragma comment(lib, "user32.lib")

#include "engine_offsets.h"     // Vector
#include "engine_player.h"      // GetPlayerPosition + AppManager / ClientApp
                                // chain constants
#include "log.h"

// Same address baked into passive_narrate / interact_hotkey — the engine
// LastTarget read primitive. Replicated as a constant rather than added to
// engine_player.h to keep this file self-contained (it's removable as a
// single commit when no longer needed).
constexpr uintptr_t kAddrCClientExoAppGetLastTarget = 0x005EDD80;

namespace acc::diag::engine_select {

namespace {

typedef uint32_t (__thiscall* PFN_GetLastTarget)(void* this_);

// AppManager → +0x4 → CClientExoApp* — same chain as passive_narrate /
// interact_hotkey. Repeated locally to keep this diagnostic self-contained.
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

void LogPress(const char* keyName) {
    uint32_t lastTarget = ReadLastTargetHandle();
    acclog::Write("DiagSelect: %s pressed; LastTarget=0x%08x", keyName,
                  lastTarget);
}

}  // namespace

void Tick() {
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    static bool s_prevQ   = false;
    static bool s_prevE   = false;
    static bool s_prevTab = false;

    bool q   = down('Q');
    bool e   = down('E');
    bool tab = down(VK_TAB);

    bool risingQ   = q   && !s_prevQ;
    bool risingE   = e   && !s_prevE;
    bool risingTab = tab && !s_prevTab;

    s_prevQ   = q;
    s_prevE   = e;
    s_prevTab = tab;

    if (!risingQ && !risingE && !risingTab) return;

    // Foreground gate — same as cycle_input::PollWin32.
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    if (risingQ)   LogPress("Q");
    if (risingE)   LogPress("E");
    if (risingTab) LogPress("Tab");
}

}  // namespace acc::diag::engine_select
