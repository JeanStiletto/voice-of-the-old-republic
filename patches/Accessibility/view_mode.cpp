#include "view_mode.h"

#include <windows.h>

#pragma comment(lib, "user32.lib")

#include "engine_options.h"
#include "engine_player.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::view_mode {

namespace {

struct ViewModeState {
    bool active = false;
};

ViewModeState g_state;

// Why no Mouse Look forcing / cursor recentring:
//
// User-verified 2026-05-06: in stock KOTOR (regardless of Caps Lock /
// Mouse Look state), A/D rotates the camera only — the character does
// not turn. The character only "snaps" to camera direction at the
// instant W or S is pressed to commit forward motion. So the engine
// already has a native "look around without rotating character"
// primitive — A/D is it. View mode just needs to suppress the
// W/S-driven snap-and-walk so the camera-pan persists.
//
// `SetPlayerInputEnabled(false)` (CSWPlayerControl::SetEnabled @0x6792e0
// per memory `project_player_control_toggle.md`) gates the per-tick
// movement clobber that drives W/S character motion. With it off,
// W/S key presses don't translate the character; A/D still pans the
// camera (camera-pan lives outside the movement-clobber path).
//
// The Phase 4 lay-off 2 probe verified Mouse Look ON makes mouse motion
// drive the camera, but that's a separate primitive — we're using key
// input directly through the engine's existing A/D camera-rotate path,
// not synthesising mouse deltas. So no Mouse Look state to manage,
// no SendInput, no cursor recentring.

void EnterViewMode() {
    // Sustained-disable (armAutoRestore=false): view mode lasts until
    // the user toggles back. Default armAutoRestore=true would auto-
    // restore after 3s — verified 2026-05-06 in patch-20260506-113051.log
    // line 41+44 (W/S regained walkability mid-session).
    if (!acc::engine::SetPlayerInputEnabled(false, /*armAutoRestore=*/false)) {
        acclog::Write(
            "ViewMode: enter REFUSED — SetPlayerInputEnabled(false) "
            "failed (chain unresolved or SEH); skipping toggle");
        return;
    }

    g_state.active = true;
    tolk::Speak(acc::strings::Get(acc::strings::Id::ViewModeOn),
                /*interrupt=*/true);
    acclog::Write("ViewMode: ENTER (player input disabled, no auto-restore)");
}

void ExitViewMode() {
    bool restored = acc::engine::SetPlayerInputEnabled(true);
    g_state.active = false;
    tolk::Speak(acc::strings::Get(acc::strings::Id::ViewModeOff),
                /*interrupt=*/true);
    acclog::Write("ViewMode: EXIT restored=%d", restored ? 1 : 0);
}

void ToggleViewMode() {
    if (g_state.active) ExitViewMode();
    else                EnterViewMode();
}

// Camera-behavior probe (Shift+B). Snapshot the documented chain pointers
// + every byte we currently know about that could plausibly hold Free
// Look / Look About state.
//
// 2026-05-06 outcome: probe + manual Caps Lock test showed no
// CClientOptions bits change in response to Caps Lock. User-blind audio
// test (AltGr heading + A/D press) further confirmed Caps Lock has no
// audible effect — A/D pans camera-only in *both* Caps Lock states.
// Free Look in K1 is therefore either cut, visual-only, or accessed
// through a chain we haven't located yet (Camera::SetBehavior @0x45c230).
// The probe stays in the build for now as a diagnostic — it's cheap and
// might catch other state changes in unrelated future RE work.
void DumpCameraStateProbe() {
    void* clientOptions = acc::engine::GetClientOptions();
    if (!clientOptions) {
        acclog::Write(
            "ViewModeProbe: Shift+B — GetClientOptions returned null "
            "(chain unresolved or SEH); nothing to snapshot");
        return;
    }

    unsigned int bitfield     = 0;
    unsigned int neighbour_4  = 0;
    unsigned int neighbour_c  = 0;
    unsigned int neighbour_10 = 0;
    unsigned int neighbour_14 = 0;
    bool fault = false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(clientOptions);
        bitfield     = *reinterpret_cast<unsigned int*>(base +
                          kClientOptionsBitFieldOffset);
        neighbour_4  = *reinterpret_cast<unsigned int*>(base + 0x4);
        neighbour_c  = *reinterpret_cast<unsigned int*>(base + 0xc);
        neighbour_10 = *reinterpret_cast<unsigned int*>(base + 0x10);
        neighbour_14 = *reinterpret_cast<unsigned int*>(base + 0x14);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fault = true;
    }

    if (fault) {
        acclog::Write(
            "ViewModeProbe: Shift+B — SEH fault while reading "
            "CClientOptions @%p; field-by-field dump aborted",
            clientOptions);
        return;
    }

    auto bit = [&](unsigned int mask) -> int {
        return (bitfield & mask) != 0 ? 1 : 0;
    };

    acclog::Write(
        "ViewModeProbe: Shift+B SNAPSHOT options=%p bitfield=0x%08x "
        "[auto_level=%d mouse_look=%d autosave=%d minigame_yaxis=%d "
        "combat_movement=%d undocumented_bits=0x%08x] "
        "neighbours @+0x4=0x%08x @+0xc=0x%08x @+0x10=0x%08x "
        "@+0x14=0x%08x view_mode_active=%d",
        clientOptions, bitfield,
        bit(0x01), bit(kClientOptionsMouseLookMask),
        bit(0x04), bit(0x08), bit(0x10),
        bitfield & ~static_cast<unsigned int>(0x1f),
        neighbour_4, neighbour_c, neighbour_10, neighbour_14,
        g_state.active ? 1 : 0);
}

}  // namespace

bool IsActive() { return g_state.active; }

void PollWin32() {
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    bool b     = down('B');
    bool shift = down(VK_SHIFT) || down(VK_LSHIFT) || down(VK_RSHIFT);

    static bool s_prevAlone = false;
    static bool s_prevShift = false;

    bool nowAlone = b && !shift;
    bool nowShift = b &&  shift;

    bool risingAlone = nowAlone && !s_prevAlone;
    bool risingShift = nowShift && !s_prevShift;
    s_prevAlone = nowAlone;
    s_prevShift = nowShift;

    if (!risingAlone && !risingShift) return;

    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        acclog::Write(
            "ViewMode: B (shift=%d) fired without player loaded; "
            "skipping", shift ? 1 : 0);
        return;
    }

    if (risingShift) {
        DumpCameraStateProbe();
        return;
    }

    if (risingAlone) {
        ToggleViewMode();
    }
}

}  // namespace acc::view_mode
