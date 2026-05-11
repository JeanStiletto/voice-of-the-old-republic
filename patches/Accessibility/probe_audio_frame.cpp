#include "probe_audio_frame.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

#include "audio_bus.h"
#include "engine_compass.h"
#include "engine_player.h"
#include "hotkeys.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::probe_audio_frame {

namespace {

constexpr float kProbeDistance  = 5.0f;
constexpr float kProbeGain      = 8.0f;
constexpr float kPi             = 3.14159265358979f;

// Cue resref for the probe — chosen distinct from any current navigation
// cue so the user can hear it as a separate test signal. "gui_open" is
// ~14 KB, a clean UI bloop with no spatial connotation in stock content.
constexpr const char* kProbeResref = "gui_open";

int  g_nextDirection = 0;  // 0..7, advances per press

// Compute the world-space offset 5m in compass sector `sector` from
// origin. Our compass convention: 0=N (+Y), 1=NE (+X,+Y), 2=E (+X),
// 3=SE (+X,-Y), 4=S (-Y), 5=SW (-X,-Y), 6=W (-X), 7=NW (-X,+Y).
//
// Compass angle (CW from N) → world XY:
//   X = sin(compass)   (sin grows toward +X as compass moves to E)
//   Y = cos(compass)   (cos starts at +Y for compass=0, decreases as we
//                       rotate CW)
Vector OffsetFor(int sector) {
    float compassDeg = sector * 45.0f;
    float compassRad = compassDeg * (kPi / 180.0f);
    return Vector{
        kProbeDistance * std::sin(compassRad),
        kProbeDistance * std::cos(compassRad),
        0.0f
    };
}

}  // namespace

// Fire a probe at compass sector `sector` (0..7) relative to the
// player, with the supplied diagnostic-tag in logs and TTS.
void FireProbe(int sector, const char* tag) {
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        acclog::Write("ProbeAudio", "%s ignored — no player loaded", tag);
        return;
    }

    Vector offset    = OffsetFor(sector);
    Vector sourcePos{
        playerPos.x + offset.x,
        playerPos.y + offset.y,
        playerPos.z
    };

    const char* dirName = acc::strings::Get(
        acc::engine::SectorString(sector));

    char msg[64];
    std::snprintf(msg, sizeof(msg), "Probe %s", dirName);
    tolk::Speak(msg, /*interrupt=*/true);

    bool ok = acc::audio::PlayCue3D(kProbeResref, sourcePos, kProbeGain);

    acclog::Write("ProbeAudio",
        "%s sector=%d (%s) source=(%.2f,%.2f,%.2f) "
        "listener=(%.2f,%.2f,%.2f) offset=(%+.2f,%+.2f) ok=%d",
        tag, sector, dirName,
        sourcePos.x, sourcePos.y, sourcePos.z,
        playerPos.x, playerPos.y, playerPos.z,
        offset.x, offset.y, ok ? 1 : 0);
}

void PollWin32() {
    namespace hk = acc::hotkeys;

    // F10 — advancing sector probe.
    if (hk::Pressed(hk::Action::ProbeAudioCycle)) {
        FireProbe(g_nextDirection, "F10");
        g_nextDirection = (g_nextDirection + 1) % 8;
    }

    // F11 — fixed-North probe. Used to disambiguate listener-orientation
    // semantics: pressing F11 twice with camera rotation in between
    // should produce different pans only if the engine's listener
    // orientation is camera-driven. If pan stays the same regardless of
    // camera rotation, listener is character-anchored or world-fixed.
    if (hk::Pressed(hk::Action::ProbeAudioFire)) {
        FireProbe(/*sector=*/0, "F11");
    }
}

}  // namespace acc::probe_audio_frame
