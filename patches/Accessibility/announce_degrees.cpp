#include "announce_degrees.h"

#include <cmath>
#include <cstdio>

#include "engine_compass.h"
#include "engine_player.h"
#include "hotkeys.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::announce_degrees {

namespace {

void OnAnnounceDegrees() {
    float engineYaw = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(engineYaw)) {
        // Yaw degenerate (mid-spawn / area-load). Stay silent — the
        // user pressed during a transient, no sensible answer.
        acclog::Write("AnnounceDegrees", "yaw unavailable, skipping");
        return;
    }
    float compass = acc::engine::EngineYawToCompass(engineYaw);
    int degrees = static_cast<int>(std::floor(compass + 0.5f)) % 360;
    if (degrees < 0) degrees += 360;

    char msg[32];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtCompassDegrees),
                  degrees);
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write("AnnounceDegrees", "-> [%s] (engineYaw=%.1f compass=%.1f)",
        msg, engineYaw, compass);
}

}  // namespace

void PollWin32() {
    // Binding lives in `hotkeys.cpp` as Action::AnnounceDegrees — AltGr
    // (VK_RMENU) alone, Shift forbidden so it stays distinct from the
    // Shift+AltGr Mouse Look probe. `Pressed()` covers rising-edge,
    // modifier match, and the KOTOR-foreground gate.
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::AnnounceDegrees)) return;

    // In-world gate — speak only when a player creature is loaded. In
    // menus / chargen / mid-load there's no meaningful "facing" to read.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return;

    OnAnnounceDegrees();
}

}  // namespace acc::announce_degrees
