#include "diag_engine_select.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#pragma comment(lib, "user32.lib")

#include "engine_area.h"        // GetObjectName, GetObjectHandle,
                                // GetObjectPosition
#include "engine_offsets.h"     // Vector
#include "engine_player.h"      // GetPlayerPosition + GetPlayerServerCreature
                                // + AppManager / ClientApp chain constants
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

    uint32_t lastTarget = ReadLastTargetHandle();

    // Resolve the controlled creature once for whichever key fired this
    // tick — we want the snapshot AFTER the engine has processed the key,
    // so this read happens on the next OnUpdate after the engine has had
    // its own chance to mutate state. Cheap (one chain walk + name read).
    void*    creature = acc::engine::GetPlayerServerCreature();
    uint32_t creatureId = creature
        ? acc::engine::GetObjectHandle(creature) : 0u;
    char     creatureName[64] = "?";
    if (creature) {
        acc::engine::GetObjectName(creature, creatureName,
                                   sizeof(creatureName));
        // Player creature: CSWSCreatureStats.first_name + tag are both
        // empty in vanilla saves (chargen writes the chosen name to
        // CClientExoAppInternal::player_character_name @+0x294, not the
        // creature stats). Fall back to the dedicated accessor so Tab
        // announces "Test" / "Revan" / etc. instead of silence when the
        // PC is the active leader. Companion NPCs (Trask, Carth, ...)
        // resolve via the generic path above and never reach this branch.
        // GetObjectName always rewrites outBuf[0] (sets '\0' on failure),
        // so the "?" sentinel above is overwritten on entry — only the
        // empty-string case reaches the fallback.
        if (creatureName[0] == '\0') {
            acc::engine::GetPlayerCharacterName(creatureName,
                                                sizeof(creatureName));
        }
    }
    Vector creaturePos{};
    bool   havePos = acc::engine::GetPlayerPosition(creaturePos);

    auto logKey = [&](const char* keyName) {
        if (havePos) {
            acclog::Write("DiagSelect", "%s pressed; LastTarget=0x%08x "
                "leader=%p id=0x%08x name=[%s] pos=(%.2f,%.2f,%.2f)",
                keyName, lastTarget, creature, creatureId, creatureName,
                creaturePos.x, creaturePos.y, creaturePos.z);
        } else {
            acclog::Write("DiagSelect", "%s pressed; LastTarget=0x%08x "
                "leader=%p id=0x%08x name=[%s] pos=?",
                keyName, lastTarget, creature, creatureId, creatureName);
        }
    };

    if (risingQ)   logKey("Q");
    if (risingE)   logKey("E");
    if (risingTab) logKey("Tab");

    // Speech moved out to party_leader_announce (2026-05-11) — that TU
    // owns the user-facing leader announce + foreground-blocking gate.
    // This file stays as pure diagnostic logging for the Q/E/Tab
    // investigation context.
}

}  // namespace acc::diag::engine_select
