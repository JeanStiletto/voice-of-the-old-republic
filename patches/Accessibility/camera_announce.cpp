#include "camera_announce.h"

#include <windows.h>
#include <cmath>

#pragma comment(lib, "user32.lib")

#include "camera_orient.h"
#include "core_tick.h"   // NowMs: QPC-backed tick clock; the rate math below
                        // divides by an inter-tick interval, which GetTickCount
                        // cannot resolve (see the NowMs contract)
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

// The camera counts as "actively rotating" inside this band. Derived from the
// compass velocity rather than from specific turn keys, so it is independent
// of which keys the player has bound turning to and also covers mouse-edge
// turns.
//
// Floor: well above the few °/s of jitter the position-derived compass shows
// while standing/walking straight, well below a real key-driven turn
// (~200°/s), so the stop edge fires cleanly ~0.1s after the player stops.
//
// Ceiling: a turn has a top speed. The engine's keyboard camera runs at 200°/s
// by default (swkotor.ini "Keyboard Camera DPS") and mouse-edge turning stays
// in the same band, so a sample an order of magnitude above that did not come
// from turning — see the discontinuity note in Tick. This bound used to be
// absent: the predicate started life as "is a turn key held", which could not
// be satisfied by a camera that merely moved, and when it was generalised to a
// rate test the upper limit implied by the old form was not carried over. That
// is what let a one-frame jump of 11570°/s register as sustained rotation and
// speak a direction the camera never faced.
constexpr float kRotatingThresholdDps = 30.0f;
constexpr float kMaxTurnDps           = 1200.0f;

// After a discontinuity, hold all speech until the compass has been quiet this
// long. Longer than kQuietMs on purpose: a Tab burst (1→2→3) re-arms the window
// on every swap, so the player hears only the leader they stopped on, and the
// gap between two deliberate Tab presses is nearer half a second than a
// quarter. Only the discontinuity path pays this; ordinary turning keeps its
// immediate stop-edge cue.
constexpr DWORD kDiscontinuitySettleMs = 500;

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
float s_lastRotDps        = 0.0f;   // last rate measured over a real interval;
                                    // carried through same-quantum ticks so the
                                    // log never reports a rate we didn't measure
bool  s_prevRotating      = false;  // was-rotating; falling edge is the stop
                                    // edge that speaks the final sector
void* s_lastLeader        = nullptr;  // active leader (Tab); identity change is
                                      // a compass discontinuity, not a turn
DWORD s_discontinuityAt   = 0;      // tick of the most recent discontinuity;
                                    // 0 = none this session. Stored as a
                                    // timestamp, not a deadline, so the elapsed
                                    // test is the same wrap-safe unsigned
                                    // subtraction the other windows use.
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
        s_lastRotDps       = 0.0f;
        s_mutedByCutscene  = false;
        s_lastLeader       = nullptr;
        s_discontinuityAt  = 0;
        return;
    }

    s_lastCamCompass = camCompass;
    DWORD now = acc::tick::NowMs();

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
        s_prevRotating     = false;  // drop any stale rotation stop edge
        s_prevVelCompass   = -1.0f;  // re-seed velocity after the mute gap
        s_lastRotDps       = 0.0f;
        s_lastLeader       = acc::engine::GetClientLeader();
        s_discontinuityAt  = 0;
        acclog::Write("CameraAnnounce", "cutscene end; silent re-anchor "
            "camCompass=%.1f sector=%d", camCompass, s_lastSpokenSector);
        return;
    }

    // Mute while camera_orient drives the camera. Hysteresis + kQuietMs
    // then announces the post-rotation final sector iff it differs. Force the
    // was-rotating flag false and drop the velocity sample so the first
    // post-orient tick can't fire a spurious stop-edge announce mid-settle.
    if (acc::camera_orient::IsActive()) {
        s_prevRotating   = false;
        s_prevVelCompass = -1.0f;
        s_lastRotDps     = 0.0f;
        return;
    }

    // First valid tick — anchor silently (no transition yet).
    if (s_lastSpokenSector < 0) {
        s_lastSpokenSector = acc::engine::CompassToSector(camCompass);
        s_pendingSector    = s_lastSpokenSector;
        s_lastChangeAt     = now;
        s_lastSpokenAt     = now;
        s_prevRotating     = false;
        s_prevVelCompass   = -1.0f;
        s_lastRotDps       = 0.0f;
        s_lastLeader       = acc::engine::GetClientLeader();
        s_discontinuityAt  = 0;
        acclog::Write("CameraAnnounce", "first-tick anchor; camCompass=%.1f sector=%d",
            camCompass, s_lastSpokenSector);
        return;
    }

    // Angular velocity of the compass, from its own samples.
    //
    // A tick is one rendered frame, so the interval is whatever the framerate
    // is — there is no fixed tact to assume. NowMs resolves finely enough to
    // measure one, but a sub-millisecond double tick can still yield dt == 0,
    // and holding the previous sample (rather than replacing it and calling the
    // rate 0) is the difference between a measurement we do not have yet and a
    // measurement of zero. Treating it as zero mid-turn read as "not rotating"
    // and fired a stop edge — which has no rate limit, so it machine-gunned
    // three sector cues in one second during a single continuous turn
    // (patch-20260813-200714, 20:07:44, back when this read GetTickCount).
    float rotDps   = s_lastRotDps;
    bool  haveRate = false;
    if (s_prevVelCompass < 0.0f) {
        s_prevVelCompass = camCompass;   // first sample after a reset or mute
        s_prevVelAt      = now;
    } else {
        DWORD dtMs = now - s_prevVelAt;
        if (dtMs > 0) {
            rotDps = std::fabs(AngularDelta(camCompass, s_prevVelCompass)) /
                     (static_cast<float>(dtMs) * 0.001f);
            haveRate         = true;
            s_lastRotDps     = rotDps;
            s_prevVelCompass = camCompass;
            s_prevVelAt      = now;
        }
        // dt == 0: same quantum as the previous sample. Leave it in place so
        // the next tick measures across a real interval.
    }

    // --- Discontinuity: the compass jumped, it did not turn ----------------
    // ReadCameraCompass derives the heading from (player position - camera
    // position), and the two halves do not always describe the same moment. A
    // Tab leader swap switches the player half to the new leader at once while
    // the camera half is still where it was, so for a frame or two the heading
    // is "old camera → new leader": it flips ~180° and snaps back once the
    // camera catches up. Any engine camera cut we don't already mute has the
    // same shape.
    //
    // The direction such a frame reports never existed, so neither the turning
    // readout nor the stop edge may fire off it, and speech waits until the
    // compass has been quiet for kDiscontinuitySettleMs. That is also what
    // collapses a Tab burst: every swap re-arms the window, so the player hears
    // one cue, for the leader they stopped on. A flip that ends where it began
    // says nothing at all — the return jump re-arms the window too, and by the
    // time it expires the sector matches what was last spoken.
    void* leader = acc::engine::GetClientLeader();
    bool leaderChanged = (leader != s_lastLeader);
    s_lastLeader = leader;
    bool jumped = haveRate && (rotDps > kMaxTurnDps);
    if (leaderChanged || jumped) {
        s_discontinuityAt = now;
        acclog::Write("CameraAnnounce", "discontinuity (%s%s%s); camCompass=%.1f "
            "rot=%.0f°/s — holding cues %ums",
            leaderChanged ? "leader swap" : "",
            (leaderChanged && jumped) ? " + " : "",
            jumped ? "compass jump" : "",
            camCompass, rotDps,
            static_cast<unsigned>(kDiscontinuitySettleMs));
    }

    // Rotation state. Forced false on a discontinuity frame so the jump can
    // neither start a turn nor end one — a real turn that happens to straddle
    // the frame simply re-arms `rotating` on the next sample. With no fresh
    // rate this tick, the previous verdict stands: a tick that measured nothing
    // is not evidence that rotation ended, so it cannot produce a stop edge.
    bool rotating = !(leaderChanged || jumped) &&
                    (haveRate ? (rotDps >= kRotatingThresholdDps)
                              : s_prevRotating);
    bool stopped  = !(leaderChanged || jumped) && s_prevRotating && !rotating;
    s_prevRotating = rotating;

    // Sticky hysteresis: the active sector stays put while the camera is
    // within (kHalfSector + kHysteresis)° of its centre.
    float lastCentre   = s_lastSpokenSector * kSectorSize;
    float distFromLast = std::fabs(AngularDelta(camCompass, lastCentre));
    int   stickySector = (distFromLast <= kHalfSector + kHysteresis)
                             ? s_lastSpokenSector
                             : acc::engine::CompassToSector(camCompass);
    if (stickySector != s_pendingSector) {
        s_pendingSector = stickySector;
        s_lastChangeAt  = now;
    }

    // The stop edge reports the RAW sector, every other reason the sticky one.
    // That difference is deliberate: hysteresis exists to stop a camera resting
    // on a boundary from chattering, but once the player has finished turning,
    // going quiet because the overshoot landed inside the sticky band would
    // leave a deliberate turn unacknowledged.
    int sector = stopped ? acc::engine::CompassToSector(camCompass)
                         : s_pendingSector;
    if (sector == s_lastSpokenSector) return;

    // --- One decision, three reasons ---------------------------------------
    //   stopped — the turn just ended: say the final sector now, because
    //             waiting out kQuietMs here reads as lag.
    //   turning — still rotating: keep a running readout, rate-limited to
    //             kMinIntervalHeldMs (~1.3 sectors at the default 200°/s) so a
    //             long spin doesn't machine-gun. Without this path a held turn
    //             is silent until release, since the sector never holds still
    //             long enough to count as settled.
    //   settled — the sector has held for kQuietMs: the final state after a
    //             burst, a nudge too small to register as rotation, or the tail
    //             of a discontinuity.
    const char* reason = nullptr;
    if (s_discontinuityAt != 0 &&
        (now - s_discontinuityAt) < kDiscontinuitySettleMs) {
        return;
    } else if (stopped) {
        reason = "stopped";
    } else if (rotating && (now - s_lastSpokenAt >= kMinIntervalHeldMs)) {
        reason = "turning";
    } else if (now - s_lastChangeAt >= kQuietMs) {
        reason = "settled";
    } else {
        return;
    }

    auto id = acc::engine::SectorString(sector);
    const char* phrase = acc::strings::Get(id);
    // Urgent SAPI: mid-turn a normal Speak gets eaten by NVDA's typed-char
    // cancel when the turn key is a typing key.
    prism::SpeakUrgent(phrase, /*voiceId=*/0);
    acclog::Write("CameraAnnounce", "sector %d -> %d (%s); camCompass=%.1f "
        "(rot=%.0f°/s %s fg=%d)",
        s_lastSpokenSector, sector, phrase, camCompass, rotDps, reason,
        acc::hotkeys::IsForegroundGame() ? 1 : 0);

    s_lastSpokenSector = sector;
    s_pendingSector    = sector;
    s_lastChangeAt     = now;
    s_lastSpokenAt     = now;
}

bool AnnounceCurrentFacing(unsigned int dedupMs) {
    float camCompass = 0.0f;
    if (!ReadCameraCompass(camCompass)) return false;

    // Never speak a scripted-camera direction as a player-facing readout.
    if (acc::engine::HasActiveDialogPanel()) return false;

    DWORD now    = acc::tick::NowMs();
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
