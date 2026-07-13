#include "camera_announce.h"

#include <windows.h>
#include <cmath>

#pragma comment(lib, "user32.lib")

#include "camera_orient.h"
#include "engine_compass.h"
#include "engine_panels.h"  // HasActiveDialogPanel: gate engine-driven cinematics
#include "engine_player.h"
#include "hotkeys.h"  // IsForegroundGame: diagnostic only; speech still fires
                      // regardless of focus.
#include "log.h"
#include "strings.h"
#include "prism.h"

namespace acc::camera_announce {

namespace {

// Compass frame: 0° = North, CW positive, 8 × 45° wedges.
constexpr float kSectorSize  = 45.0f;
constexpr float kHalfSector  = 22.5f;
constexpr float kHysteresis  = 5.0f;

// Speak only when the sector has been stable this long — collapses
// transient flips during fast rotation.
constexpr DWORD kQuietMs = 250;

// While the camera is actively rotating, announce at most this often even if
// the sector keeps changing. At default 200°/s DPS that's ~1 announce
// per 1.3 sectors.
constexpr DWORD kMinIntervalHeldMs = 300;

// The camera counts as "actively rotating" above this angular speed. Well
// above the few °/s of jitter the position-derived compass shows while
// standing/walking straight, well below a real key-driven turn (~200°/s), so
// the release edge fires cleanly ~0.1s after the player stops turning.
// Derived from the camera compass velocity rather than reading specific turn
// keys, so it is independent of which keys the player has bound turning to.
constexpr float kRotatingThresholdDps = 30.0f;

// Below this XY distance the (player - camera) direction is unstable
// (vertical component dominates, physics smoothing). Refuse to announce.
constexpr float kMinXYDistance = 0.1f;

float AngularDelta(float a, float b) {
    return std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
}

// -1 sentinel = not yet anchored; reset on un-load so next session re-anchors.
int   s_lastSpokenSector  = -1;
int   s_pendingSector     = -1;
DWORD s_lastChangeAt      = 0;
DWORD s_lastSpokenAt      = 0;
float s_lastCamCompass    = -1.0f;  // cached for TryGetCameraEngineYawDegrees
float s_prevVelCompass    = -1.0f;  // previous tick's compass for velocity calc
DWORD s_prevVelAt         = 0;      // timestamp of s_prevVelCompass
bool  s_prevRelevantHeld  = false;  // was-rotating; falling edge fires the
                                    // release-edge announce
bool  s_mutedByCutscene   = false;  // latched while an engine cinematic drives
                                    // the camera; forces a silent re-anchor on
                                    // exit so the cutscene's final direction is
                                    // never spoken as a player turn.

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
        // Not in-world or unstable. Reset so next valid tick re-anchors.
        s_lastSpokenSector = -1;
        s_pendingSector    = -1;
        s_lastCamCompass   = -1.0f;
        s_prevVelCompass   = -1.0f;
        s_mutedByCutscene  = false;
        return;
    }

    s_lastCamCompass = camCompass;
    DWORD now = GetTickCount();

    // Mute while the engine drives the camera through a cinematic / dialog
    // (a DialogCinematic* panel sits in the stack). The player can't steer
    // the camera during those, so every scripted pan would otherwise be
    // spoken as a bogus "you turned" — the Endar Spire opening cutscene fired
    // a full burst of sector announces this way. Latch so the first tick
    // AFTER the cutscene ends re-anchors silently: the cutscene's final
    // resting direction must not fire a spurious announce as control returns.
    if (acc::engine::HasActiveDialogPanel()) {
        s_mutedByCutscene = true;
        return;
    }
    if (s_mutedByCutscene) {
        s_mutedByCutscene  = false;
        s_lastSpokenSector = acc::engine::CompassToSector(camCompass);
        s_pendingSector    = s_lastSpokenSector;
        s_lastChangeAt     = now;
        s_lastSpokenAt     = now;
        s_prevRelevantHeld = false;  // drop any stale rotation release edge
        s_prevVelCompass   = -1.0f;  // re-seed velocity after the mute gap
        acclog::Write("CameraAnnounce", "cutscene end; silent re-anchor "
            "camCompass=%.1f sector=%d", camCompass, s_lastSpokenSector);
        return;
    }

    // Mute while camera_orient drives the camera. Hysteresis + kQuietMs
    // then announces the post-rotation final sector iff it differs. Force the
    // was-rotating flag false and drop the velocity sample so the first
    // post-orient tick can't fire a spurious release-edge announce mid-settle.
    if (acc::camera_orient::IsActive()) {
        s_prevRelevantHeld = false;
        s_prevVelCompass   = -1.0f;
        return;
    }

    // First valid tick — anchor silently (no transition yet).
    if (s_lastSpokenSector < 0) {
        s_lastSpokenSector = acc::engine::CompassToSector(camCompass);
        s_pendingSector    = s_lastSpokenSector;
        s_lastChangeAt     = now;
        s_lastSpokenAt     = now;
        s_prevRelevantHeld = false;
        s_prevVelCompass   = -1.0f;
        acclog::Write("CameraAnnounce", "first-tick anchor; camCompass=%.1f sector=%d",
            camCompass, s_lastSpokenSector);
        return;
    }

    // Detect active rotation from the camera's own angular velocity rather
    // than reading specific turn keys — so it works regardless of which keys
    // the player bound turning to, and also covers mouse-edge turns and Q/E
    // target snaps. (camera_orient / cutscene rotations are muted above, so
    // this only ever sees genuine player-driven turning.)
    float rotDps = 0.0f;
    if (s_prevVelCompass >= 0.0f) {
        DWORD dtMs = now - s_prevVelAt;
        if (dtMs > 0) {
            rotDps = std::fabs(AngularDelta(camCompass, s_prevVelCompass)) /
                     (static_cast<float>(dtMs) * 0.001f);
        }
    }
    s_prevVelCompass = camCompass;
    s_prevVelAt      = now;

    bool relevantHeldNow  = (rotDps >= kRotatingThresholdDps);
    bool relevantHeldPrev = s_prevRelevantHeld;
    s_prevRelevantHeld    = relevantHeldNow;
    bool releaseEdge      = relevantHeldPrev && !relevantHeldNow;

    // Release-edge announce: tell the user the final sector, even if
    // intermediate ones got suppressed by the held-interval debounce.
    bool isForeground = acc::hotkeys::IsForegroundGame();
    if (releaseEdge) {
        int finalSector = acc::engine::CompassToSector(camCompass);
        if (finalSector != s_lastSpokenSector) {
            auto id = acc::engine::SectorString(finalSector);
            const char* phrase = acc::strings::Get(id);
            // Urgent SAPI: mid-turn; a normal Speak gets eaten by NVDA's
            // typed-char cancel when the turn key is a typing key.
            prism::SpeakUrgent(phrase, /*voiceId=*/0);
            acclog::Write("CameraAnnounce", "release-edge sector %d -> %d (%s); "
                "camCompass=%.1f fg=%d",
                s_lastSpokenSector, finalSector, phrase, camCompass,
                isForeground ? 1 : 0);
            s_lastSpokenSector = finalSector;
            s_pendingSector    = finalSector;
            s_lastChangeAt     = now;
            s_lastSpokenAt     = now;
            return;
        }
    }

    // Sticky hysteresis: the active sector stays put while the camera is
    // within (kHalfSector + kHysteresis)° of its centre.
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

    // Two paths to speech: (a) stable for kQuietMs (final state after a
    // burst) or (b) sustained-rotation held-interval to keep the user
    // oriented mid-spin.
    bool stable       = (now - s_lastChangeAt >= kQuietMs);
    bool heldOverride = relevantHeldNow &&
                        (now - s_lastSpokenAt >= kMinIntervalHeldMs);

    if (!stable && !heldOverride) return;

    auto id = acc::engine::SectorString(s_pendingSector);
    const char* phrase = acc::strings::Get(id);
    prism::SpeakUrgent(phrase, /*voiceId=*/0);
    acclog::Write("CameraAnnounce", "sector %d -> %d (%s); camCompass=%.1f "
        "(rot=%.0f°/s %s fg=%d)",
        s_lastSpokenSector, s_pendingSector, phrase, camCompass,
        rotDps,
        stable ? "quiet" : "held",
        isForeground ? 1 : 0);

    s_lastSpokenSector = s_pendingSector;
    s_lastSpokenAt     = now;
}

bool AnnounceCurrentFacing(unsigned int dedupMs) {
    float camCompass = 0.0f;
    if (!ReadCameraCompass(camCompass)) return false;

    // Never speak a scripted-camera direction as a player-facing readout.
    if (acc::engine::HasActiveDialogPanel()) return false;

    DWORD now    = GetTickCount();
    int   sector = acc::engine::CompassToSector(camCompass);

    // Dedup: the autoturn that faced the target just announced this exact
    // sector (within the window), so the door-open readout would only echo it.
    // A tiny autoturn that stayed in-sector never fired an announce, so
    // s_lastSpokenAt is old and this speaks — which is what we want.
    if (sector == s_lastSpokenSector && s_lastSpokenSector >= 0 &&
        (now - s_lastSpokenAt) < dedupMs) {
        acclog::Write("CameraAnnounce",
            "facing readout deduped: sector %d spoken %lums ago (<%ums)",
            sector, now - s_lastSpokenAt, dedupMs);
        return false;
    }

    auto id = acc::engine::SectorString(sector);
    const char* phrase = acc::strings::Get(id);
    // Urgent SAPI: a normal Speak here gets swallowed — the interact keypress
    // that opened the door trips NVDA's typed-char cancel (which ignores
    // priority), so route around it the same way the A/D-held turn announce
    // does.
    prism::SpeakUrgent(phrase, /*voiceId=*/0);
    acclog::Write("CameraAnnounce",
        "facing readout: sector %d (%s) camCompass=%.1f", sector, phrase,
        camCompass);

    // Adopt this as the last-spoken sector so the per-tick announcer treats it
    // as the anchor and won't re-announce the same sector a beat later.
    s_lastSpokenSector = sector;
    s_pendingSector    = sector;
    s_lastChangeAt     = now;
    s_lastSpokenAt     = now;
    return true;
}

bool TryGetCameraEngineYawDegrees(float& out) {
    if (s_lastCamCompass < 0.0f) return false;
    // Compass → engine: involution; same formula as EngineYawToCompass.
    float engine = std::fmod(90.0f - s_lastCamCompass + 360.0f, 360.0f);
    if (engine < 0.0f) engine += 360.0f;
    out = engine;
    return true;
}

}  // namespace acc::camera_announce
