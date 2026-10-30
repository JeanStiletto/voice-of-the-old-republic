#include "camera_orient.h"

#include <windows.h>
#include <cmath>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")

#include "camera_announce.h"  // TryGetCameraEngineYawDegrees
#include "engine_app.h"      // GetCamera
#include "engine_area.h"     // GetCurrentArea
#include "engine_compass.h"
#include "engine_keymap.h"    // TurnScancode — bound turn key
#include "engine_offsets.h"
#include "engine_player.h"
#include "engine_rebase.h"    // PickGlobal — the desired-orientation request
#include "guidance_beacon.h"
#include "hotkeys.h"
#include "key_inject.h"       // Send — the shared scancode injection
#include "log.h"
#include "room_topology.h"   // LookupAt — the region's exits and their points

namespace acc::camera_orient {

namespace {

// Two drive mechanisms. First choice is the snap: the camera's own one-shot
// desired-orientation request (see "Snap" below), which turns it in a frame.
// When the active camera never reads that request (combat camera, unmapped
// address), the key-hold turn takes over:
//
// Key-hold turn: we synthesise the player's *bound* turn key via SendInput
// with KEYEVENTF_SCANCODE, so the engine's own UpdateCamera runs its full
// poll-axis → AcclTurnCamera → apply sequence exactly as if the player held
// the key. This is the mechanism that has always worked; the only change vs.
// the old hardcoded A/D is that the scancode now comes from engine_keymap's
// TurnScancode() (read from swkotor.ini [Keymapping]), so a turn rebind is
// honoured. (Calling AcclTurnCamera directly from our out-of-band tick was
// tried and does NOT reliably move the chase-cam — its accumulator is only
// consumed inside the engine's own per-frame UpdateCamera pass.) The engine
// reads keyboard via DirectInput, which sees scancodes only — plain-VK
// SendInput is invisible to it.

// Single rotation at a time. Rate-based predictive release: each tick
// samples yaw + timestamp so the next tick projects time-to-target and
// releases the key ahead of arrival to absorb input-pipeline latency.
// Without prediction the engine keeps rotating ~30-50ms after our keyup.
struct Rotation {
    bool   active             = false;
    WORD   holdScan           = 0;       // DIK scancode of the held turn key
    char   debugKey           = 0;       // 'L' / 'R' for log readability
    float  targetEngineYawRad = 0.0f;
    float  initialAbsDeltaRad = 0.0f;    // overshoot detection
    DWORD  startedMs          = 0;
    float  prevYawRad         = 0.0f;
    DWORD  prevTickMs         = 0;
    bool   haveRateSample     = false;
};
Rotation g_rot;

// How far ahead of arrival to release: keyup → DirectInput release →
// engine pipeline drain → the follow-cam easing out. Measured, not
// estimated: 15 K1 turns (log 2026-10-30 15:25) kept rotating for
// rate × 75..123ms after release (the old 40ms left every turn 7-16°
// past its exit). 100ms lands within a few degrees across that spread.
constexpr DWORD kReleaseLookaheadMs = 100;

// A key-hold turn keeps coasting after release; its landing (log line +
// facing readout) waits until this tick time for the camera to stop.
constexpr DWORD kLandSettleMs = 150;
DWORD g_settleUntilMs = 0;
bool  g_landPending   = false;  // LogLanding owes a line for the last key turn

// Fallback when no rate sample yet or rate too small to project. 2.9°.
constexpr float kFallbackArrivalRad = 0.05f;

// Below this the time-to-target projection explodes. 1e-5 rad/ms ≈
// 0.57°/s, well below any real key-driven rate.
constexpr float kMinRateRadPerMs = 1e-5f;

// Note: driving the engine's turn function directly (AcclTurnCamera @0x640090)
// from our out-of-band tick was tried and does NOT reliably move the chase-cam
// — its yaw accumulator is only consumed inside the engine's own per-frame
// UpdateCamera pass. The synthesised-keypress path below is the proven one.

// Safety cap for "engine ignored our input" (load screen, modal, scripted
// takeover) — must not strand the synthesised key held.
constexpr DWORD kTimeoutMs = 3000;

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kTwoPi   = 6.28318530717958647692f;
constexpr float kRadToDeg = 57.29577951308232f;
constexpr float kDegToRad = 0.017453292519943295f;

// ---- Snap: the camera's one-shot desired-orientation request ---------------
//
// CSWCameraOnAStick::Control_UpdateCameraDesiredOrientation (K1 @0x006391a0,
// K2 @0x007dea10, reached from the free-roam camera's Control every update)
// ends with
//     if (desiredOrientation >= 0) {
//         orientation = YawPitchRoll(desiredOrientation, 0, 0);
//         desiredOrientation = -1;
//     }
// Nothing in the engine writes it — K1's only references are that read and
// that reset — so it is a leftover one-shot "face this yaw" request, and
// writing it IS the turn: no key held, no keyboard DPS/acceleration ramp
// (Keyboard Camera DPS 200 / Acceleration 500 made a 150° key turn take a
// second). Degrees in the engine's Yaw() frame, atan2(-x, y): North = 0,
// CCW+ — our engine frame (East = 0, CCW+) minus 90°. In both games it sits
// 4 bytes below screenFramePercentage (camera_spin_guard's 0x007A2444 /
// 0x00A10910). The combat camera (CSWBehaviorCameraCombat) never reads it:
// the request then stays pending and the key-hold turn takes over.
const uintptr_t kAddrDesiredOrientation = acc::addr::PickGlobal(0x007A2440, 0x00A1090C);  // float

// The camera reads the request on its next update; 200ms covers a slow
// frame or two. Still pending by then = this camera does not read it.
constexpr DWORD kSnapTakeMs = 200;
// Taken but the heading is not there yet (a follow-cam that eases instead of
// jumping): wait this long before the key-hold turn finishes the job.
constexpr DWORD kSnapArriveMs = 600;

struct Snap {
    bool        active             = false;
    bool        taken              = false;  // the camera consumed the request
    float       targetEngineYawRad = 0.0f;
    DWORD       startedMs          = 0;
    const char* mode               = "";     // log label, a literal
};
Snap g_snap;

bool WriteDesiredOrientation(float deg) {
    if (!kAddrDesiredOrientation) return false;
    __try {
        *reinterpret_cast<volatile float*>(kAddrDesiredOrientation) = deg;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// False on no address / fault. `pending` = the camera has not taken it yet.
bool ReadDesiredOrientationPending(bool& pending) {
    if (!kAddrDesiredOrientation) return false;
    __try {
        pending = *reinterpret_cast<volatile float*>(kAddrDesiredOrientation) >= 0.0f;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Prefer camera_announce's position-derived yaw — atan2(player - camera)
// is single-valued, the quaternion path returns antipodal readings 360°
// apart and breaks the arrival check. Falls back to the quaternion if
// announce hasn't anchored yet.
// No camera parameter: the announce path has its own source and the
// quaternion fallback re-walks the chain inside GetCameraYawRadians. Callers
// still resolve the camera themselves — they need it for their own "camera
// vanished" check — they just have nothing to hand to this.
bool ReadCurrentEngineYawRad(float& out) {
    float degsFromAnnounce = 0.0f;
    if (acc::camera_announce::TryGetCameraEngineYawDegrees(
            degsFromAnnounce)) {
        out = degsFromAnnounce * kDegToRad;
        return true;
    }
    // Quaternion fallback (camera directly above player → position-derived
    // yaw degenerates). Reads the camera orientation quaternion correctly
    // (engine layout w,x,y,z; the earlier 2*atan2(qz,qw) read the wrong
    // fields — engine y/z — which is why this path looked "multi-valued"
    // and got abandoned). GetCameraYawRadians yields the same East=0 frame
    // as the position-derived path. See engine_player.h.
    return acc::engine::GetCameraYawRadians(out);
}

void SendKey(WORD scan, bool down) {
    // The injection itself lives in key_inject — the KOTOR 1 pad layer drives
    // the engine the same way (walking, camera, menu navigation), and two
    // copies of the SendInput call would be the same knowledge written twice.
    // The predictive-release policy above stays here; key_inject is
    // deliberately policy-free.
    acc::key_inject::Send(scan, down);
}

float NormaliseRad(float r) {
    while (r >  kPi)  r -= kTwoPi;
    while (r <= -kPi) r += kTwoPi;
    return r;
}

float CompassDegToEngineRad(float compassDeg) {
    float engineDeg = std::fmod(90.0f - compassDeg + 360.0f, 360.0f);
    if (engineDeg < 0.0f) engineDeg += 360.0f;
    return engineDeg * kDegToRad;
}

// The exit N last turned to, so the next press in the same region cycles
// on instead of re-aiming at it. Only trusted while the player is still in
// that region and still faces it; walking into another region retires it.
struct LastExit {
    bool   valid     = false;
    void*  area      = nullptr;
    int    clusterId = acc::room_topology::kClusterIdNone;
    Vector pos       = {0.0f, 0.0f, 0.0f};
};
LastExit g_lastExit;

// "Still facing the exit N turned to": the release lands a few degrees
// short or long (predictive release + coast), so exact equality never holds.
constexpr float kStillFacingRad = 10.0f * 0.017453292519943295f;

// An exit point this close to the player has no usable bearing.
constexpr float kMinExitDistM = 0.5f;

// Exits of the player's region as engine-frame bearings (0=East, CCW+),
// measured from where the player stands — the real direction, not the
// octant word the label speaks. Bearings within the arrival tolerance of
// one already kept collapse into it: a turn between them would be a no-op.
struct ExitBearing {
    float  yawRad;
    Vector pos;
};

bool CollectExitBearings(const Vector& playerPos, void*& outArea,
                         int& outClusterId, std::vector<ExitBearing>& out) {
    out.clear();
    outArea = acc::engine::GetCurrentArea();
    outClusterId = acc::room_topology::kClusterIdNone;
    if (!outArea) return false;
    std::string label;
    int sig = 0;
    std::vector<acc::room_topology::ExitMark> exits;
    if (!acc::room_topology::LookupAt(outArea, playerPos, label, sig,
                                      outClusterId,
                                      /*allowDiagLog=*/false,
                                      /*requireWallReachable=*/true,
                                      /*zTrusted=*/true, &exits)) {
        return false;
    }
    for (const auto& e : exits) {
        float dx = e.pos.x - playerPos.x;
        float dy = e.pos.y - playerPos.y;
        if (dx * dx + dy * dy < kMinExitDistM * kMinExitDistM) continue;
        float yaw = std::atan2(dy, dx);
        bool dup = false;
        for (const auto& k : out) {
            if (std::fabs(NormaliseRad(yaw - k.yawRad)) <= kFallbackArrivalRad) {
                dup = true;
                break;
            }
        }
        if (!dup) out.push_back({yaw, e.pos});
    }
    return !out.empty();
}

// Next exit clockwise from `fromYawRad` (engine frame is CCW+, so
// clockwise = decreasing yaw). Never `fromIdx` itself unless it is the
// only exit.
int NextExitClockwise(const std::vector<ExitBearing>& exits, int fromIdx) {
    int best = fromIdx;
    float bestCw = kTwoPi + 1.0f;
    for (int i = 0; i < static_cast<int>(exits.size()); ++i) {
        if (i == fromIdx) continue;
        float cw = std::fmod(exits[fromIdx].yawRad - exits[i].yawRad + 2.0f * kTwoPi,
                             kTwoPi);
        if (cw < bestCw) {
            bestCw = cw;
            best = i;
        }
    }
    return best;
}

// Pick the exit to turn to:
//   - still in the region of the last N turn and still facing that exit →
//     the next one clockwise (the cycle);
//   - otherwise the exit nearest the camera heading, however small the
//     offset — N lines the player up exactly. Only when that exit is
//     already within the arrival tolerance (nothing to turn) does it move
//     on clockwise.
int ChooseExit(const std::vector<ExitBearing>& exits, void* area,
               int clusterId, float curYawRad, const char*& outWhy) {
    if (g_lastExit.valid && g_lastExit.area == area &&
        g_lastExit.clusterId == clusterId) {
        for (int i = 0; i < static_cast<int>(exits.size()); ++i) {
            float dx = exits[i].pos.x - g_lastExit.pos.x;
            float dy = exits[i].pos.y - g_lastExit.pos.y;
            if (dx * dx + dy * dy > 0.01f) continue;
            if (std::fabs(NormaliseRad(exits[i].yawRad - curYawRad)) <=
                kStillFacingRad) {
                outWhy = "cycle";
                return NextExitClockwise(exits, i);
            }
            break;
        }
    }
    int nearest = 0;
    float nearestAbs = kTwoPi;
    for (int i = 0; i < static_cast<int>(exits.size()); ++i) {
        float a = std::fabs(NormaliseRad(exits[i].yawRad - curYawRad));
        if (a < nearestAbs) {
            nearestAbs = a;
            nearest = i;
        }
    }
    if (nearestAbs <= kFallbackArrivalRad) {
        outWhy = "already-facing";
        return NextExitClockwise(exits, nearest);
    }
    outWhy = "nearest";
    return nearest;
}

// Next cardinal in CW order (N → E → S → W → N).
float NextCardinalCompassDeg(float currentCompassDeg) {
    if (currentCompassDeg < 0.0f) {
        currentCompassDeg = std::fmod(currentCompassDeg, 360.0f) + 360.0f;
    }
    if (currentCompassDeg >= 360.0f) {
        currentCompassDeg = std::fmod(currentCompassDeg, 360.0f);
    }
    int curIdx = static_cast<int>(currentCompassDeg / 90.0f);
    int nextIdx = (curIdx + 1) % 4;
    return static_cast<float>(nextIdx) * 90.0f;
}

void ReleaseAndDisarm(const char* reason, float curYawRad) {
    if (!g_rot.active) return;
    SendKey(g_rot.holdScan, /*down=*/false);
    float curCompassDeg = acc::engine::EngineYawToCompass(
        curYawRad * kRadToDeg);
    int curSector = acc::engine::CompassToSector(curCompassDeg);

    // Final direction is announced by camera_announce's hysteresis once
    // IsActive() drops on the next tick.

    acclog::Write("CameraOrient",
                  "release: reason=%s dir=%c cur=%.1f° (compass=%.1f° "
                  "sector=%d) target=%.1f° elapsed=%ums",
                  reason,
                  g_rot.debugKey,
                  curYawRad * kRadToDeg,
                  curCompassDeg,
                  curSector,
                  g_rot.targetEngineYawRad * kRadToDeg,
                  GetTickCount() - g_rot.startedMs);
    g_rot.active = false;
    g_settleUntilMs = GetTickCount() + kLandSettleMs;
    g_landPending = true;
}

// One line per turn once the coast has finished: where the camera actually
// stopped against the target. The release line can't show this — the
// camera is still moving when it is written.
void LogLanding() {
    if (!g_landPending) return;
    if (static_cast<int>(GetTickCount() - g_settleUntilMs) < 0) return;
    g_landPending = false;
    float curYawRad = 0.0f;
    if (!ReadCurrentEngineYawRad(curYawRad)) return;
    acclog::Write("CameraOrient", "landed: cur=%.1f° target=%.1f° error=%.1f°",
                  curYawRad * kRadToDeg,
                  g_rot.targetEngineYawRad * kRadToDeg,
                  NormaliseRad(curYawRad - g_rot.targetEngineYawRad) * kRadToDeg);
}

// Start a key-hold turn toward the target. False when already there (no
// key to hold).
bool ArmKeyTurn(float targetEngineYawRad, float curYawRad, const char* mode) {
    float delta = NormaliseRad(targetEngineYawRad - curYawRad);
    if (std::fabs(delta) <= kFallbackArrivalRad) {
        acclog::Write("CameraOrient",
                      "no-op: already at target (delta=%.2f° within "
                      "tolerance)",
                      delta * kRadToDeg);
        return false;
    }

    // Engine frame (0=East, CCW+): positive delta = CCW = turn LEFT, negative
    // = CW = turn RIGHT. The scancode to synthesise for each direction comes
    // from the player's bound turn keys (engine_keymap), so a rebind is honoured.
    bool isLeft = (delta > 0.0f);
    WORD scan = static_cast<WORD>(acc::engine_keymap::TurnScancode(isLeft));
    char debugKey = isLeft ? 'L' : 'R';

    DWORD nowMs = GetTickCount();
    g_rot.active              = true;
    g_rot.holdScan            = scan;
    g_rot.debugKey            = debugKey;
    g_rot.targetEngineYawRad  = targetEngineYawRad;
    g_rot.initialAbsDeltaRad  = std::fabs(delta);
    g_rot.startedMs           = nowMs;
    // Seed the rate window with arm-time yaw so the first in-flight tick
    // already has a baseline.
    g_rot.prevYawRad          = curYawRad;
    g_rot.prevTickMs          = nowMs;
    g_rot.haveRateSample      = true;

    SendKey(scan, /*down=*/true);

    acclog::Write("CameraOrient",
                  "arm: mode=%s cur=%.1f° target=%.1f° delta=%.1f° dir=%c "
                  "(scan=0x%02x)",
                  mode,
                  curYawRad * kRadToDeg,
                  targetEngineYawRad * kRadToDeg,
                  delta * kRadToDeg,
                  debugKey,
                  scan);
    return true;
}

// In-flight key-hold turn: the predictive-release state machine.
void TickKeyTurn(void* camera) {
    if (!camera) {
        // Camera vanished (area transition / shutdown) — release the key
        // so it isn't stranded across the load screen.
        SendKey(g_rot.holdScan, /*down=*/false);
        acclog::Write("CameraOrient",
                      "release: reason=camera_lost dir=%c",
                      g_rot.debugKey);
        g_rot.active = false;
        g_landPending = false;
        return;
    }
    float curYawRad = 0.0f;
    if (!ReadCurrentEngineYawRad(curYawRad)) {
        ReleaseAndDisarm("getyaw_fault", 0.0f);
        return;
    }
    float remaining = NormaliseRad(g_rot.targetEngineYawRad - curYawRad);
    DWORD nowMs = GetTickCount();

    // NormaliseRad on delta-yaw handles ±π wrap.
    bool   useRate     = false;
    float  rateRadPerMs = 0.0f;
    float  ttaMs       = 0.0f;
    if (g_rot.haveRateSample) {
        DWORD dtMs = nowMs - g_rot.prevTickMs;
        if (dtMs > 0) {
            float dyaw = NormaliseRad(curYawRad - g_rot.prevYawRad);
            rateRadPerMs = dyaw / static_cast<float>(dtMs);
            // Same-sign on rate + remaining = rotating toward target;
            // opposite = wrong-side wrap, fall back to static window.
            if (std::fabs(rateRadPerMs) >= kMinRateRadPerMs &&
                ((remaining > 0.0f) == (rateRadPerMs > 0.0f))) {
                ttaMs = remaining / rateRadPerMs;
                useRate = true;
            }
        }
    }

    bool arrived = useRate
        ? (ttaMs <= static_cast<float>(kReleaseLookaheadMs))
        : (std::fabs(remaining) <= kFallbackArrivalRad);

    // |remaining| grown past initial + 45° = sailed past in the wrong
    // direction (engine ignored input + user drove the opposite way).
    bool overshot = std::fabs(remaining) >
                    g_rot.initialAbsDeltaRad + kPi * 0.25f;

    DWORD elapsed = nowMs - g_rot.startedMs;
    bool timedOut = elapsed >= kTimeoutMs;

    g_rot.prevYawRad     = curYawRad;
    g_rot.prevTickMs     = nowMs;
    g_rot.haveRateSample = true;

    if (arrived || overshot || timedOut) {
        const char* reason = arrived ? "arrived"
                           : overshot ? "overshot"
                           : "timeout";
        acclog::Write("CameraOrient",
                      "decide: %s remaining=%.2f° rate=%.3f°/ms tta=%.0fms "
                      "(useRate=%d)",
                      reason,
                      remaining * kRadToDeg,
                      rateRadPerMs * kRadToDeg,
                      ttaMs,
                      useRate ? 1 : 0);
        ReleaseAndDisarm(reason, curYawRad);
    }
}

// Issue the snap. False when the request cannot be written (address unknown
// on this build, or a fault) — the caller turns by key instead.
bool StartSnap(float targetEngineYawRad, float curYawRad, const char* mode) {
    float deg = std::fmod(targetEngineYawRad * kRadToDeg - 90.0f, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    if (!WriteDesiredOrientation(deg)) return false;
    g_snap.active             = true;
    g_snap.taken              = false;
    g_snap.targetEngineYawRad = targetEngineYawRad;
    g_snap.startedMs          = GetTickCount();
    g_snap.mode               = mode;
    acclog::Write("CameraOrient",
                  "snap: mode=%s cur=%.1f° target=%.1f° delta=%.1f° "
                  "request=%.1f°",
                  mode,
                  curYawRad * kRadToDeg,
                  targetEngineYawRad * kRadToDeg,
                  NormaliseRad(targetEngineYawRad - curYawRad) * kRadToDeg,
                  deg);
    return true;
}

// In-flight snap: wait for the camera to take the request, then for the
// heading to show it. Either wait running out hands the rest to the key turn.
void TickSnap(void* camera) {
    DWORD nowMs = GetTickCount();
    DWORD elapsed = nowMs - g_snap.startedMs;
    if (!camera) {
        WriteDesiredOrientation(-1.0f);
        acclog::Write("CameraOrient", "snap: camera lost after %ums", elapsed);
        g_snap.active = false;
        return;
    }
    if (!g_snap.taken) {
        bool pending = true;
        if (ReadDesiredOrientationPending(pending) && !pending) {
            g_snap.taken = true;
            acclog::Write("CameraOrient", "snap: taken after %ums", elapsed);
        } else if (elapsed < kSnapTakeMs) {
            return;
        } else {
            // Withdraw it: a request taken later would yank the camera in
            // the middle of the key turn that replaces it.
            WriteDesiredOrientation(-1.0f);
            g_snap.active = false;
            acclog::Write("CameraOrient",
                          "snap: not taken in %ums (combat camera?) — key turn",
                          elapsed);
            float curYawRad = 0.0f;
            if (ReadCurrentEngineYawRad(curYawRad)) {
                ArmKeyTurn(g_snap.targetEngineYawRad, curYawRad, g_snap.mode);
            }
            return;
        }
    }
    float curYawRad = 0.0f;
    if (!ReadCurrentEngineYawRad(curYawRad)) {
        acclog::Write("CameraOrient", "snap: yaw read faulted after %ums", elapsed);
        g_snap.active = false;
        return;
    }
    float remaining = NormaliseRad(g_snap.targetEngineYawRad - curYawRad);
    if (std::fabs(remaining) <= kFallbackArrivalRad) {
        acclog::Write("CameraOrient",
                      "snap landed: cur=%.1f° target=%.1f° error=%.1f° "
                      "elapsed=%ums",
                      curYawRad * kRadToDeg,
                      g_snap.targetEngineYawRad * kRadToDeg,
                      -remaining * kRadToDeg,
                      elapsed);
        g_snap.active = false;
        return;
    }
    if (elapsed >= kSnapArriveMs) {
        acclog::Write("CameraOrient",
                      "snap: taken but still %.1f° off after %ums — key turn "
                      "for the rest",
                      remaining * kRadToDeg, elapsed);
        g_snap.active = false;
        ArmKeyTurn(g_snap.targetEngineYawRad, curYawRad, g_snap.mode);
    }
}

}  // namespace

// Covers the window g_rot.active alone misses: the rising-edge tick itself,
// since camera_announce::Tick runs before us in core_tick and would otherwise
// speak the pre-rotation sector on the frame N is pressed.
bool IsActive() {
    if (g_rot.active || g_snap.active) return true;
    return acc::hotkeys::Held(acc::hotkeys::Action::CameraOrient);
}

void Tick() {
    void* camera = acc::engine::GetCamera();

    // In-flight turn: tick whichever mechanism is driving it.
    if (g_snap.active || g_rot.active) {
        if (g_snap.active) TickSnap(camera);
        else               TickKeyTurn(camera);
        // A snap lands in a frame or two, so a press can only fall inside a
        // turn on the key-hold fallback; it is dropped there.
        if (acc::hotkeys::Pressed(acc::hotkeys::Action::CameraOrient)) {
            acclog::Write("CameraOrient",
                          "ignore re-press: rotation in flight");
        }
        return;
    }

    LogLanding();

    // Rising edge — arm a new rotation.
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::CameraOrient)) return;

    if (!camera) {
        acclog::Write("CameraOrient", "skip: no camera (main menu / pre-spawn)");
        return;
    }
    float curYawRad = 0.0f;
    if (!ReadCurrentEngineYawRad(curYawRad)) {
        acclog::Write("CameraOrient", "skip: Camera::GetYaw call faulted");
        return;
    }

    bool beaconMode = acc::guidance::beacon::IsActive();
    const char* mode = beaconMode ? "beacon" : "cardinal";
    float targetEngineYawRad = 0.0f;
    Vector waypoint = {0.0f, 0.0f, 0.0f};
    Vector playerPos = {0.0f, 0.0f, 0.0f};
    void* exitArea = nullptr;
    int exitCluster = acc::room_topology::kClusterIdNone;
    std::vector<ExitBearing> exits;

    if (beaconMode && acc::guidance::beacon::GetCurrentTarget(waypoint)) {
        Vector playerPos;
        if (!acc::engine::GetPlayerPosition(playerPos)) {
            acclog::Write("CameraOrient", "skip: beacon active but no "
                          "player position resolved");
            return;
        }
        float dx = waypoint.x - playerPos.x;
        float dy = waypoint.y - playerPos.y;
        if (std::fabs(dx) < 1e-4f && std::fabs(dy) < 1e-4f) {
            acclog::Write("CameraOrient", "skip: beacon waypoint coincides "
                          "with player position");
            return;
        }
        targetEngineYawRad = std::atan2(dy, dx);
    } else if (acc::engine::GetPlayerPosition(playerPos) &&
               CollectExitBearings(playerPos, exitArea, exitCluster, exits)) {
        // Face the region's exits — the real bearing from where the player
        // stands, so a press or two lines the walk up with where to go.
        const char* why = "nearest";
        int pick = ChooseExit(exits, exitArea, exitCluster, curYawRad, why);
        targetEngineYawRad = exits[pick].yawRad;
        g_lastExit.valid     = true;
        g_lastExit.area      = exitArea;
        g_lastExit.clusterId = exitCluster;
        g_lastExit.pos       = exits[pick].pos;
        mode = "exit";
        acclog::Write("CameraOrient",
                      "exit pick: %s idx=%d of %d cluster=%d bearing=%.1f°",
                      why, pick, static_cast<int>(exits.size()), exitCluster,
                      targetEngineYawRad * kRadToDeg);
    } else {
        // No region or no exit with a usable bearing (open-area fallback,
        // graph not built): the old compass step.
        g_lastExit.valid = false;
        mode = "cardinal";
        float curCompassDeg = acc::engine::EngineYawToCompass(
            curYawRad * kRadToDeg);
        float targetCompassDeg = NextCardinalCompassDeg(curCompassDeg);
        targetEngineYawRad = CompassDegToEngineRad(targetCompassDeg);
    }

    float delta = NormaliseRad(targetEngineYawRad - curYawRad);
    if (std::fabs(delta) <= kFallbackArrivalRad) {
        acclog::Write("CameraOrient",
                      "no-op: already at target (delta=%.2f° within "
                      "tolerance)",
                      delta * kRadToDeg);
        return;
    }

    // The snap first; the key-hold turn when this build cannot write it.
    if (StartSnap(targetEngineYawRad, curYawRad, mode)) return;
    ArmKeyTurn(targetEngineYawRad, curYawRad, mode);
}

}  // namespace acc::camera_orient
