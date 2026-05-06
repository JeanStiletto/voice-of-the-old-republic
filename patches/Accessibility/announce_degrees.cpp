#include "announce_degrees.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

#include "engine_player.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::announce_degrees {

namespace {

// Engine yaw frame: 0° = +X = East, CCW positive ([0, 360)).
// Compass frame:    0° = North,    CW positive (matches turn_announce).
// Conversion: compass = (90 - engine + 360) mod 360.
float EngineYawToCompass(float engineYawDeg) {
    float c = std::fmod(90.0f - engineYawDeg + 360.0f, 360.0f);
    if (c < 0.0f) c += 360.0f;
    return c;
}

void OnAnnounceDegrees() {
    float engineYaw = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(engineYaw)) {
        // Yaw degenerate (mid-spawn / area-load). Stay silent — the
        // user pressed during a transient, no sensible answer.
        acclog::Write("AnnounceDegrees: yaw unavailable, skipping");
        return;
    }
    float compass = EngineYawToCompass(engineYaw);
    int degrees = static_cast<int>(std::floor(compass + 0.5f)) % 360;
    if (degrees < 0) degrees += 360;

    char msg[32];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtCompassDegrees),
                  degrees);
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write(
        "AnnounceDegrees: -> [%s] (engineYaw=%.1f compass=%.1f)",
        msg, engineYaw, compass);
}

}  // namespace

void PollWin32() {
    // VK_RMENU = right Alt only. On German QWERTZ this is the AltGr key
    // directly right of the spacebar. Windows synthesises a phantom
    // VK_LCONTROL alongside RMENU when AltGr is pressed (Win32-message
    // back-compat) — irrelevant here, we look at the right-Alt scancode
    // directly.
    //
    // Deliberately NOT VK_MENU (either Alt): left Alt is on the other
    // side of space and we don't want to poach it — it's also already
    // used as a modifier in `cycle_input::PollWin32` (Alt+- → Force path).
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    static bool s_prev = false;
    bool now = down(VK_RMENU);
    bool rising = now && !s_prev;
    s_prev = now;
    if (!rising) return;

    // Foreground gate — same pattern as `cycle_input::PollWin32`. AltGr
    // is heavily used outside the game (German typing: AltGr+Q = @,
    // AltGr+E = €, etc.) so a global rising-edge would speak whenever
    // the user typed special characters in another window.
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    // In-world gate — speak only when a player creature is loaded. In
    // menus / chargen / mid-load there's no meaningful "facing" to read.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return;

    OnAnnounceDegrees();
}

}  // namespace acc::announce_degrees
