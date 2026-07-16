#include "engine_compass.h"

#include <cmath>

namespace acc::engine {

namespace {

constexpr int   kSectorCount = 8;
constexpr float kSectorSize  = 45.0f;
constexpr float kHalfSector  = 22.5f;

}  // namespace

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

int ClockPosition(float playerYawDeg, float dx, float dy) {
    constexpr float kRadToDeg = 57.29577951308232f;
    float worldAngle = std::atan2(dy, dx) * kRadToDeg;
    float relCcw = worldAngle - playerYawDeg;
    float clockDeg = -relCcw;  // flip to clockwise from +ahead
    while (clockDeg < 0.0f)    clockDeg += 360.0f;
    while (clockDeg >= 360.0f) clockDeg -= 360.0f;
    int hour = static_cast<int>(clockDeg / 30.0f + 0.5f) % 12;
    return hour == 0 ? 12 : hour;
}

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

}  // namespace acc::engine
