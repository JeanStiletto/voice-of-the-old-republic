#include "turn_announce.h"

#include <windows.h>
#include <cmath>

#include "engine_player.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::turn_announce {

namespace {

// 8 sectors centered on cardinal/intercardinal points (compass frame —
// 0° = North, 90° = East, CW positive). Each sector spans 45°; the
// strict boundary is ±22.5° from the centre.
//
// Hysteresis: once a sector is active, leaving it requires the yaw to
// exceed the strict boundary by an additional 5° (per the long-term
// plan §"Locked defaults — Pillar 2"). Prevents border-thrashing
// announcements when the player parks near a sector boundary and
// rotates micro-amounts.
constexpr int   kSectorCount = 8;
constexpr float kSectorSize  = 45.0f;   // 360 / 8
constexpr float kHalfSector  = 22.5f;   // strict boundary
constexpr float kHysteresis  = 5.0f;    // sticky boundary = 22.5 + 5

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
    return S::DirNorth;  // unreachable
}

// Compute the smallest signed angular difference (a - b), normalized
// to (-180, +180]. Used for hysteresis — measures "how far is the
// current yaw from the centre of the active sector".
float AngularDelta(float a, float b) {
    float d = std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
    return d;  // in (-180, +180]
}

// Engine yaw frame: 0° = +X = East, CCW positive ([0, 360)).
// Compass frame: 0° = North, 90° = East, CW positive.
// Conversion: compass = (90 - engine + 360) mod 360.
float EngineYawToCompass(float engineYawDeg) {
    float c = std::fmod(90.0f - engineYawDeg + 360.0f, 360.0f);
    if (c < 0.0f) c += 360.0f;
    return c;
}

// Strict nearest-sector lookup (no hysteresis). Returns 0..7.
int CompassToSector(float compassDeg) {
    // Centre of sector i is at i * 45°. Find nearest by adding half a
    // sector before bucketing.
    float adj = std::fmod(compassDeg + kHalfSector + 360.0f, 360.0f);
    int s = static_cast<int>(adj / kSectorSize);
    if (s < 0)              s = 0;
    if (s >= kSectorCount)  s = kSectorCount - 1;
    return s;
}

}  // namespace

void Tick() {
    float engineYawDeg = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(engineYawDeg)) return;

    float compass = EngineYawToCompass(engineYawDeg);

    // -1 = "first observation since DLL load — set sector but don't speak".
    static int s_lastSector = -1;

    if (s_lastSector < 0) {
        s_lastSector = CompassToSector(compass);
        acclog::Write(
            "TurnAnnounce: first-tick suppress; engineYaw=%.1f compass=%.1f "
            "sector=%d", engineYawDeg, compass, s_lastSector);
        return;
    }

    // Hysteresis: stay in last sector while within (kHalfSector +
    // kHysteresis)° of its centre. Only re-evaluate when we leave that
    // band.
    float lastCentre = s_lastSector * kSectorSize;
    float distFromLast = std::fabs(AngularDelta(compass, lastCentre));
    if (distFromLast <= kHalfSector + kHysteresis) return;

    int newSector = CompassToSector(compass);
    if (newSector == s_lastSector) return;  // jitter near far boundary

    auto id = SectorString(newSector);
    const char* phrase = acc::strings::Get(id);

    // interrupt=false — direction shouldn't talk over an in-flight
    // passive_narrate / cycle announcement. NVDA queues by default.
    tolk::Speak(phrase, /*interrupt=*/false);

    acclog::Write(
        "TurnAnnounce: sector %d -> %d (%s); engineYaw=%.1f compass=%.1f",
        s_lastSector, newSector, phrase, engineYawDeg, compass);

    s_lastSector = newSector;
}

}  // namespace acc::turn_announce
