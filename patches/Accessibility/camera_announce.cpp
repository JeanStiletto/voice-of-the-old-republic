#include "camera_announce.h"

#include <windows.h>
#include <cmath>

#pragma comment(lib, "user32.lib")

#include "engine_compass.h"
#include "engine_player.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::camera_announce {

namespace {

// Sector geometry — shared with turn_announce. Compass frame
// (0° = North, CW positive), 8 × 45° wedges. The conversion math lives
// in engine_compass.{h,cpp}; only the hysteresis state stays here.
constexpr float kSectorSize  = 45.0f;
constexpr float kHalfSector  = 22.5f;
constexpr float kHysteresis  = 5.0f;

// Final-state debounce (matches turn_announce). Speak only when the
// sector has been stable this long. Collapses transient sector flips
// during fast rotation to a single announcement.
constexpr DWORD kQuietMs = 250;

// Held-key override: while exactly one of A/D is held, announce at most
// once per this interval even if the sector keeps changing. At the
// default 200°/s DPS, the camera crosses a sector every ~225ms.
//
// 300ms ≈ one announce per 1.3 sectors at default DPS. Still avoids
// spamming on quick passes, while keeping the user oriented during
// sustained rotation.
constexpr DWORD kMinIntervalHeldMs = 300;

// Minimum horizontal camera→player distance below which we don't trust
// the derived look direction. At < 10cm the (player - camera) vector
// has unstable direction (vertical component dominates, XY noise from
// camera physics smoothing); refuse to announce until camera floats
// back to its normal orbit distance.
constexpr float kMinXYDistance = 0.1f;

float AngularDelta(float a, float b) {
    return std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
}

// Last announced state. Lives across ticks. -1 sentinels mean
// "not yet anchored" (no in-game tick has fired); reset when the
// player un-loads so the next session re-anchors cleanly.
int   s_lastSpokenSector  = -1;
int   s_pendingSector     = -1;
DWORD s_lastChangeAt      = 0;
DWORD s_lastSpokenAt      = 0;
// Last observed compass yaw — cached so TryGetCameraEngineYawDegrees
// returns the most recent reading without re-walking the engine chain.
// -1.0f sentinel = not yet observed.
float s_lastCamCompass    = -1.0f;
// Tracks "exactly one of A/D held" from the previous tick — falling
// edge fires a release-edge announce so the user always learns the
// final camera direction when they stop rotating.
bool  s_prevRelevantHeld  = false;

// Read camera + player positions, derive look direction, return
// compass yaw. The KOTOR orbital camera always looks at the
// character, so the camera's facing direction = normalize(player - camera).
//
// Returns false on:
//   - either GetCameraPosition or GetPlayerPosition failing
//     (menus / chargen / area transitions / SEH fault)
//   - camera and player too close horizontally to derive a stable
//     direction (camera physics smoothing / clipped against wall)
bool ReadCameraCompass(float& outCompass) {
    Vector cameraPos;
    Vector playerPos;
    if (!acc::engine::GetCameraPosition(cameraPos)) return false;
    if (!acc::engine::GetPlayerPosition(playerPos)) return false;

    float dx = playerPos.x - cameraPos.x;
    float dy = playerPos.y - cameraPos.y;
    float distXY = std::sqrt(dx * dx + dy * dy);
    if (distXY < kMinXYDistance) return false;

    constexpr float kRadToDeg = 57.29577951308232f;
    float engineYaw = std::atan2(dy, dx) * kRadToDeg;
    if (engineYaw < 0.0f) engineYaw += 360.0f;
    outCompass = acc::engine::EngineYawToCompass(engineYaw);
    return true;
}

}  // namespace

void Tick() {
    float camCompass = 0.0f;
    if (!ReadCameraCompass(camCompass)) {
        // Not in-world or camera too close to player to derive a stable
        // direction. Reset announce state so the next valid tick re-
        // anchors cleanly rather than carrying stale sector across
        // saves / area transitions.
        s_lastSpokenSector = -1;
        s_pendingSector    = -1;
        s_lastCamCompass   = -1.0f;
        return;
    }

    s_lastCamCompass = camCompass;
    DWORD now = GetTickCount();

    // First valid tick — anchor and suppress speech (the user hasn't
    // rotated yet; no transition to announce).
    if (s_lastSpokenSector < 0) {
        s_lastSpokenSector = acc::engine::CompassToSector(camCompass);
        s_pendingSector    = s_lastSpokenSector;
        s_lastChangeAt     = now;
        s_lastSpokenAt     = now;
        acclog::Write("CameraAnnounce", "first-tick anchor; camCompass=%.1f sector=%d",
            camCompass, s_lastSpokenSector);
        return;
    }

    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };
    bool aHeld = down('A');
    bool dHeld = down('D');
    bool relevantHeldNow  = (aHeld != dHeld);  // XOR — both held = no net rotation
    bool relevantHeldPrev = s_prevRelevantHeld;
    s_prevRelevantHeld    = relevantHeldNow;
    bool releaseEdge      = relevantHeldPrev && !relevantHeldNow;

    // Release-edge announce: the user stopped rotating; tell them the
    // final sector they ended up at, even if intermediate sectors got
    // suppressed by the held-interval debounce. Skipped if the final
    // sector matches what we last spoke (no change to narrate).
    if (releaseEdge) {
        int finalSector = acc::engine::CompassToSector(camCompass);
        if (finalSector != s_lastSpokenSector) {
            auto id = acc::engine::SectorString(finalSector);
            const char* phrase = acc::strings::Get(id);
            tolk::Speak(phrase, /*interrupt=*/false);
            acclog::Write("CameraAnnounce", "release-edge sector %d -> %d (%s); "
                "camCompass=%.1f",
                s_lastSpokenSector, finalSector, phrase, camCompass);
            s_lastSpokenSector = finalSector;
            s_pendingSector    = finalSector;
            s_lastChangeAt     = now;
            s_lastSpokenAt     = now;
            return;
        }
    }

    // Hysteresis around last spoken sector. The active sector stays
    // sticky as long as the camera is within (kHalfSector + kHysteresis)°
    // of the spoken sector's centre — prevents flicker when parked near
    // a boundary.
    float lastCentre   = s_lastSpokenSector * kSectorSize;
    float distFromLast = std::fabs(AngularDelta(camCompass, lastCentre));
    int   currentSector = (distFromLast <= kHalfSector + kHysteresis)
                              ? s_lastSpokenSector
                              : acc::engine::CompassToSector(camCompass);

    if (currentSector != s_pendingSector) {
        s_pendingSector = currentSector;
        s_lastChangeAt  = now;
    }

    if (s_pendingSector == s_lastSpokenSector) return;

    // Two paths to speech:
    //   (a) sector stable for kQuietMs — final-state announcement after
    //       a burst (e.g. brief A/D tap that crossed a boundary then
    //       settled).
    //   (b) exactly one of A/D held AND kMinIntervalHeldMs since last
    //       announcement — keeps the user oriented during sustained
    //       rotation instead of going silent until they release.
    bool stable       = (now - s_lastChangeAt >= kQuietMs);
    bool heldOverride = relevantHeldNow &&
                        (now - s_lastSpokenAt >= kMinIntervalHeldMs);

    if (!stable && !heldOverride) return;

    auto id = acc::engine::SectorString(s_pendingSector);
    const char* phrase = acc::strings::Get(id);
    tolk::Speak(phrase, /*interrupt=*/false);
    acclog::Write("CameraAnnounce", "sector %d -> %d (%s); camCompass=%.1f "
        "(a=%d d=%d %s)",
        s_lastSpokenSector, s_pendingSector, phrase, camCompass,
        aHeld ? 1 : 0, dHeld ? 1 : 0,
        stable ? "quiet" : "held");

    s_lastSpokenSector = s_pendingSector;
    s_lastSpokenAt     = now;
}

bool TryGetCameraEngineYawDegrees(float& out) {
    if (s_lastCamCompass < 0.0f) return false;  // no valid observation yet
    // Compass → engine: engine = (90 - compass + 360) mod 360. The
    // formula is its own inverse (same as engine_compass::EngineYawToCompass)
    // because both frames are degree-rotations on the unit circle —
    // a 180° + sign-flip composition is involutive.
    float engine = std::fmod(90.0f - s_lastCamCompass + 360.0f, 360.0f);
    if (engine < 0.0f) engine += 360.0f;
    out = engine;
    return true;
}

}  // namespace acc::camera_announce
