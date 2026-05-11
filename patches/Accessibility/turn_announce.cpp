#include "turn_announce.h"

#include <windows.h>
#include <cmath>

#include "engine_compass.h"
#include "engine_player.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::turn_announce {

namespace {

// Sector geometry — shared with camera_announce. Compass frame
// (0° = North, CW positive), 8 × 45° wedges. The conversion math lives
// in engine_compass.{h,cpp}; only the hysteresis state stays here.
//
// Hysteresis: once a sector is active, leaving it requires the yaw to
// exceed the strict boundary by an additional 5° (per the long-term
// plan §"Locked defaults — Pillar 2"). Prevents border-thrashing
// announcements when the player parks near a sector boundary and
// rotates micro-amounts.
constexpr float kSectorSize  = 45.0f;   // 360 / 8
constexpr float kHalfSector  = 22.5f;   // strict boundary
constexpr float kHysteresis  = 5.0f;    // sticky boundary = 22.5 + 5

// Final-state debounce: only speak after the sector has been stable for
// this many ms. Collapses bursts like W→S (character spins 180° across
// 4 sectors in <1s) to a single announcement of the final direction.
constexpr DWORD kQuietMs = 250;

// Compute the smallest signed angular difference (a - b), normalized
// to (-180, +180]. Used for hysteresis — measures "how far is the
// current yaw from the centre of the active sector".
float AngularDelta(float a, float b) {
    float d = std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
    return d;  // in (-180, +180]
}

}  // namespace

void Tick() {
    float engineYawDeg = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(engineYawDeg)) return;

    float compass = acc::engine::EngineYawToCompass(engineYawDeg);
    DWORD now = GetTickCount();

    // -1 = "first observation since DLL load — set sector but don't speak".
    static int   s_lastSpokenSector = -1;
    static int   s_pendingSector    = -1;
    static DWORD s_lastChangeAt     = 0;

    if (s_lastSpokenSector < 0) {
        s_lastSpokenSector = acc::engine::CompassToSector(compass);
        s_pendingSector    = s_lastSpokenSector;
        s_lastChangeAt     = now;
        acclog::Write("TurnAnnounce", "first-tick suppress; engineYaw=%.1f compass=%.1f "
            "sector=%d", engineYawDeg, compass, s_lastSpokenSector);
        return;
    }

    // Hysteresis around last *spoken* sector: while within (kHalfSector +
    // kHysteresis)° of its centre, treat current sector as unchanged.
    // Outside the band, compute the strict nearest sector.
    float lastCentre   = s_lastSpokenSector * kSectorSize;
    float distFromLast = std::fabs(AngularDelta(compass, lastCentre));
    int   currentSector = (distFromLast <= kHalfSector + kHysteresis)
                              ? s_lastSpokenSector
                              : acc::engine::CompassToSector(compass);

    // Track most-recent-observed sector + when it last changed. While the
    // player is mid-turn (e.g. W→S 180° spin), this fires every ~50ms and
    // keeps last_change_at moving — no announcement until the spin ends.
    if (currentSector != s_pendingSector) {
        s_pendingSector = currentSector;
        s_lastChangeAt  = now;
    }

    if (s_pendingSector == s_lastSpokenSector) return;
    if (now - s_lastChangeAt < kQuietMs)        return;  // not stable yet

    auto id = acc::engine::SectorString(s_pendingSector);
    const char* phrase = acc::strings::Get(id);

    // interrupt=false — direction shouldn't talk over an in-flight
    // passive_narrate / cycle announcement. NVDA queues by default.
    tolk::Speak(phrase, /*interrupt=*/false);

    acclog::Write("TurnAnnounce", "sector %d -> %d (%s); engineYaw=%.1f compass=%.1f "
        "(debounced %ums)",
        s_lastSpokenSector, s_pendingSector, phrase, engineYawDeg, compass,
        static_cast<unsigned>(now - s_lastChangeAt));

    s_lastSpokenSector = s_pendingSector;
}

}  // namespace acc::turn_announce
