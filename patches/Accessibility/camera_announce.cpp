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

// Final-state debounce (matches turn_announce). Speak only when the
// sector has been stable this long. Collapses transient sector flips
// during fast rotation to a single announcement.
constexpr DWORD kQuietMs = 250;

// Held-key override: while exactly one of A/D is held, announce at most
// once per this interval even if the sector keeps changing. At the
// default 200°/s DPS, the camera crosses a sector every ~225ms — without
// this override, dedup would suppress announcements indefinitely while
// rotating. ≈600ms gives one short German direction word per beat.
constexpr DWORD kMinIntervalHeldMs = 600;

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

// Dead-reckoned camera-yaw state. Lifted from function-static to
// namespace-static (Phase 4 lay-off 4a) so `TryGetCameraEngineYawDegrees`
// can read it without bouncing through Tick(). Tick() owns all writes;
// the getter is read-only.
//
// `s_camYawCompass = -1.0f` is the "not yet anchored" sentinel — first
// in-game tick replaces it with the live character compass yaw, and the
// "lost player" branch resets it back to -1 so the next save / area
// load re-anchors cleanly.
float s_camYawCompass     = -1.0f;
float s_lastCharCompass   = -1.0f;
int   s_lastSpokenSector  = -1;
int   s_pendingSector     = -1;
DWORD s_lastChangeAt      = 0;
DWORD s_lastSpokenAt      = 0;
DWORD s_lastTick          = 0;

}  // namespace

void Tick() {

    // Self-gate: silent in menus / chargen / pre-spawn / degenerate facing.
    float charEngineYaw = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(charEngineYaw)) {
        // While not in-game, reset estimate so the first in-game tick
        // re-anchors cleanly rather than carrying stale state across
        // saves / area transitions.
        s_camYawCompass     = -1.0f;
        s_lastSpokenSector  = -1;
        s_pendingSector     = -1;
        s_lastTick          = 0;
        return;
    }

    float charCompass = EngineYawToCompass(charEngineYaw);
    DWORD now = GetTickCount();

    // First-tick init: anchor camera estimate to current character yaw,
    // log it, but suppress speech (the user hasn't rotated yet).
    if (s_camYawCompass < 0.0f) {
        s_camYawCompass    = charCompass;
        s_lastCharCompass  = charCompass;
        s_lastSpokenSector = CompassToSector(charCompass);
        s_pendingSector    = s_lastSpokenSector;
        s_lastChangeAt     = now;
        s_lastSpokenAt     = now;
        s_lastTick         = now;
        acclog::Write(
            "CameraAnnounce: first-tick anchor; charCompass=%.1f sector=%d",
            charCompass, s_lastSpokenSector);
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
        // Update spoken/pending so the next genuine A/D rotation registers
        // against the post-resync sector, not the pre-resync one.
        int snappedSector = CompassToSector(s_camYawCompass);
        if (snappedSector != s_lastSpokenSector) {
            s_lastSpokenSector = snappedSector;
            s_pendingSector    = snappedSector;
            s_lastChangeAt     = now;
            s_lastSpokenAt     = now;  // suppress immediate held-override fire
        }
        return;
    }

    // Hysteresis around last spoken sector.
    float lastCentre   = s_lastSpokenSector * kSectorSize;
    float distFromLast = std::fabs(AngularDelta(s_camYawCompass, lastCentre));
    int   currentSector = (distFromLast <= kHalfSector + kHysteresis)
                              ? s_lastSpokenSector
                              : CompassToSector(s_camYawCompass);

    if (currentSector != s_pendingSector) {
        s_pendingSector = currentSector;
        s_lastChangeAt  = now;
    }

    if (s_pendingSector == s_lastSpokenSector) return;

    // Two paths to speech:
    //   (a) sector stable for kQuietMs — final-state announcement after
    //       a burst (e.g. brief A/D tap that crossed a boundary then
    //       settled, or releasing the held key partway through a sector).
    //   (b) exactly one of A/D held AND kMinIntervalHeldMs since last
    //       announcement — keeps the user oriented during sustained
    //       rotation instead of going silent until they release.
    bool stable       = (now - s_lastChangeAt >= kQuietMs);
    bool relevantHeld = (aHeld != dHeld);  // XOR — both held = no rotation
    bool heldOverride = relevantHeld &&
                        (now - s_lastSpokenAt >= kMinIntervalHeldMs);

    if (!stable && !heldOverride) return;

    auto id = SectorString(s_pendingSector);
    const char* phrase = acc::strings::Get(id);
    tolk::Speak(phrase, /*interrupt=*/false);
    acclog::Write(
        "CameraAnnounce: sector %d -> %d (%s); estCamYaw=%.1f "
        "charCompass=%.1f (a=%d d=%d %s)",
        s_lastSpokenSector, s_pendingSector, phrase, s_camYawCompass,
        charCompass, aHeld ? 1 : 0, dHeld ? 1 : 0,
        stable ? "quiet" : "held");

    s_lastSpokenSector = s_pendingSector;
    s_lastSpokenAt     = now;
}

bool TryGetCameraEngineYawDegrees(float& out) {
    if (s_camYawCompass < 0.0f) return false;  // not yet anchored
    // Compass → engine: engine = (90 - compass + 360) mod 360. The
    // formula is its own inverse (same as EngineYawToCompass above)
    // because both frames are degree-rotations on the unit circle —
    // a 180° + sign-flip composition is involutive.
    float engine = std::fmod(90.0f - s_camYawCompass + 360.0f, 360.0f);
    if (engine < 0.0f) engine += 360.0f;
    out = engine;
    return true;
}

}  // namespace acc::camera_announce
