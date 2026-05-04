#include "camera_announce.h"

#include <windows.h>
#include <cmath>

#pragma comment(lib, "user32.lib")

#include "engine_player.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::camera_announce {

namespace {

// Defaults per the user's verified swkotor.ini "Keyboard Camera DPS=200.0".
// If a user has retuned this in their config, the dead-reckoning will
// drift faster between W-press resyncs but stays correct after each
// resync. Reading from CClientOptions @+0x94 is a future polish.
constexpr float kCameraDpsDefault = 200.0f;

// Sign convention. Flip these if in-game testing shows A/D produce
// reversed direction announcements.
constexpr float kSignA = -1.0f;  // A = rotate CCW = compass yaw decreases
constexpr float kSignD = +1.0f;  // D = rotate CW  = compass yaw increases

// Sector geometry — same parameters as turn_announce.
constexpr int   kSectorCount = 8;
constexpr float kSectorSize  = 45.0f;
constexpr float kHalfSector  = 22.5f;
constexpr float kHysteresis  = 5.0f;

acc::strings::Id SectorString(int sector) {
    using S = acc::strings::Id;
    switch (sector) {
        case 0: return S::DirNorth;
        case 1: return S::DirNortheast;
        case 2: return S::DirEast;
        case 3: return S::DirSoutheast;
        case 4: return S::DirSouth;
        case 5: return S::DirSouthwest;
        case 6: return S::DirWest;
        case 7: return S::DirNorthwest;
    }
    return S::DirNorth;
}

float AngularDelta(float a, float b) {
    return std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
}

float EngineYawToCompass(float engineYawDeg) {
    float c = std::fmod(90.0f - engineYawDeg + 360.0f, 360.0f);
    if (c < 0.0f) c += 360.0f;
    return c;
}

int CompassToSector(float compassDeg) {
    float adj = std::fmod(compassDeg + kHalfSector + 360.0f, 360.0f);
    int s = static_cast<int>(adj / kSectorSize);
    if (s < 0)              s = 0;
    if (s >= kSectorCount)  s = kSectorCount - 1;
    return s;
}

void NormalizeYaw(float& yaw) {
    while (yaw <    0.0f) yaw += 360.0f;
    while (yaw >= 360.0f) yaw -= 360.0f;
}

}  // namespace

void Tick() {
    // -1 sentinels = "first usable tick this DLL load — initialise + suppress
    // first speech".
    static float s_camYawCompass    = -1.0f;
    static float s_lastCharCompass  = -1.0f;
    static int   s_lastSector       = -1;
    static DWORD s_lastTick         = 0;

    // Self-gate: silent in menus / chargen / pre-spawn / degenerate facing.
    float charEngineYaw = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(charEngineYaw)) {
        // While not in-game, reset estimate so the first in-game tick
        // re-anchors cleanly rather than carrying stale state across
        // saves / area transitions.
        s_camYawCompass = -1.0f;
        s_lastSector    = -1;
        s_lastTick      = 0;
        return;
    }

    float charCompass = EngineYawToCompass(charEngineYaw);
    DWORD now = GetTickCount();

    // First-tick init: anchor camera estimate to current character yaw,
    // log it, but suppress speech (the user hasn't rotated yet).
    if (s_camYawCompass < 0.0f) {
        s_camYawCompass   = charCompass;
        s_lastCharCompass = charCompass;
        s_lastSector      = CompassToSector(charCompass);
        s_lastTick        = now;
        acclog::Write(
            "CameraAnnounce: first-tick anchor; charCompass=%.1f sector=%d",
            charCompass, s_lastSector);
        return;
    }

    // Re-sync on character yaw change. Each W press snaps the character
    // to face the camera, so character.yaw == camera.yaw at that moment.
    // Detect via "character compass moved more than 1°" — turn_announce's
    // hysteresis is 5° to suppress micro-jitter; we want to catch any
    // real turn including small ones, so use a tighter threshold here.
    float charDelta = std::fabs(AngularDelta(charCompass, s_lastCharCompass));
    if (charDelta > 1.0f) {
        s_camYawCompass   = charCompass;
        s_lastCharCompass = charCompass;
        // Don't return — let sector-change check below decide if a
        // boundary was crossed by the resync. (Usually it was, but
        // turn_announce will narrate the character-side change; we
        // suppress here to avoid double-announcement.) See sector check.
    }

    // Integrate A/D inputs into the estimated camera yaw.
    float dt = static_cast<float>(now - s_lastTick) / 1000.0f;
    s_lastTick = now;
    if (dt < 0.0f)  dt = 0.0f;
    if (dt > 0.5f)  dt = 0.5f;  // cap to avoid huge jumps after long stalls

    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    bool aHeld = down('A');
    bool dHeld = down('D');
    if (aHeld && !dHeld) s_camYawCompass += kSignA * kCameraDpsDefault * dt;
    if (dHeld && !aHeld) s_camYawCompass += kSignD * kCameraDpsDefault * dt;
    // Both held: net zero rotation, no integration (skip).
    NormalizeYaw(s_camYawCompass);

    // Sector check with hysteresis (matches turn_announce). Suppress
    // re-announce if the new sector matches what turn_announce would
    // already speak this tick — i.e. when the resync just happened. We
    // don't have direct access to turn_announce's state, so use a
    // recency check: if charDelta > 1 on this tick (resync fired), skip
    // sector announce — turn_announce will handle it.
    if (charDelta > 1.0f) {
        // Update s_lastSector so the next genuine A/D rotation registers
        // against the post-resync sector, not the pre-resync one.
        int snappedSector = CompassToSector(s_camYawCompass);
        if (snappedSector != s_lastSector) {
            s_lastSector = snappedSector;
        }
        return;
    }

    float lastCentre   = s_lastSector * kSectorSize;
    float distFromLast = std::fabs(AngularDelta(s_camYawCompass, lastCentre));
    if (distFromLast <= kHalfSector + kHysteresis) return;

    int newSector = CompassToSector(s_camYawCompass);
    if (newSector == s_lastSector) return;

    auto id = SectorString(newSector);
    const char* phrase = acc::strings::Get(id);
    tolk::Speak(phrase, /*interrupt=*/false);
    acclog::Write(
        "CameraAnnounce: sector %d -> %d (%s); estCamYaw=%.1f "
        "charCompass=%.1f (a=%d d=%d)",
        s_lastSector, newSector, phrase, s_camYawCompass, charCompass,
        aHeld ? 1 : 0, dHeld ? 1 : 0);

    s_lastSector = newSector;
}

}  // namespace acc::camera_announce
