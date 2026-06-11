#include "camera_orient.h"

#include <windows.h>
#include <cmath>

#pragma comment(lib, "user32.lib")

#include "camera_announce.h"  // TryGetCameraEngineYawDegrees
#include "engine_compass.h"
#include "engine_offsets.h"
#include "engine_player.h"
#include "guidance_beacon.h"
#include "hotkeys.h"
#include "log.h"

namespace acc::camera_orient {

namespace {

// Camera chain: CClientExoAppInternal +0x18 = CSWCModule; +0x40 = camera.
constexpr size_t kClientInternalModuleOffset = 0x18;
constexpr size_t kCSWCModuleCameraOffset     = 0x40;

// DirectInput scan codes. Engine reads keyboard via DirectInput, which
// sees scancodes only — plain VK SendInput is invisible to it.
constexpr WORD kDikA = 0x1E;
constexpr WORD kDikD = 0x20;

// Single rotation at a time. Rate-based predictive release: each tick
// samples yaw + timestamp so the next tick projects time-to-target and
// releases the key ahead of arrival to absorb input-pipeline latency.
// Without prediction the engine keeps rotating ~30-50ms after our keyup.
struct Rotation {
    bool   active             = false;
    WORD   holdScan           = 0;
    char   debugKey           = 0;       // 'A' / 'D' for log readability
    float  targetEngineYawRad = 0.0f;
    float  initialAbsDeltaRad = 0.0f;    // overshoot detection
    DWORD  startedMs          = 0;
    float  prevYawRad         = 0.0f;
    DWORD  prevTickMs         = 0;
    bool   haveRateSample     = false;
};
Rotation g_rot;

// 40ms ≈ 8° look-ahead at default 200°/s DPS — covers SendInput keyup →
// DirectInput release → engine pipeline drain.
constexpr DWORD kReleaseLookaheadMs = 40;

// Fallback when no rate sample yet or rate too small to project. 2.9°.
constexpr float kFallbackArrivalRad = 0.05f;

// Below this the time-to-target projection explodes. 1e-5 rad/ms ≈
// 0.57°/s, well below any real key-driven rate.
constexpr float kMinRateRadPerMs = 1e-5f;

// Safety cap for "engine ignored our input" (load screen, modal, scripted
// takeover) — must not strand the key pressed.
constexpr DWORD kTimeoutMs = 3000;

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kTwoPi   = 6.28318530717958647692f;
constexpr float kRadToDeg = 57.29577951308232f;
constexpr float kDegToRad = 0.017453292519943295f;

// ----- Chain walk helpers ------------------------------------------------

void* SafeDeref(void* base, size_t offset) {
    if (!base) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetModule() {
    void* appManager = SafeDeref(
        reinterpret_cast<void*>(kAddrAppManagerPtr), 0);
    if (!appManager) return nullptr;
    void* clientApp = SafeDeref(appManager, kAppManagerClientAppOffset);
    if (!clientApp) return nullptr;
    void* clientInternal = SafeDeref(clientApp, kClientExoAppInternalOffset);
    if (!clientInternal) return nullptr;
    return SafeDeref(clientInternal, kClientInternalModuleOffset);
}

void* GetCamera() {
    void* module = GetModule();
    if (!module) return nullptr;
    return SafeDeref(module, kCSWCModuleCameraOffset);
}

// Prefer camera_announce's position-derived yaw — atan2(player - camera)
// is single-valued, the quaternion path returns antipodal readings 360°
// apart and breaks the arrival check. Falls back to the quaternion if
// announce hasn't anchored yet.
bool ReadCurrentEngineYawRad(void* camera, float& out) {
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
    (void)camera;  // chain re-walked inside the helper
    return acc::engine::GetCameraYawRadians(out);
}

void SendKey(WORD scan, bool down) {
    INPUT inp = {};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wScan = scan;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &inp, sizeof(INPUT));
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
                  "release: reason=%s key=%c cur=%.1f° (compass=%.1f° "
                  "sector=%d) target=%.1f° elapsed=%ums",
                  reason,
                  g_rot.debugKey,
                  curYawRad * kRadToDeg,
                  curCompassDeg,
                  curSector,
                  g_rot.targetEngineYawRad * kRadToDeg,
                  GetTickCount() - g_rot.startedMs);
    g_rot.active = false;
}

}  // namespace

// Covers two windows g_rot.active alone misses: (1) the rising-edge tick
// itself, since camera_announce::Tick runs before us in core_tick and
// would otherwise speak the pre-rotation sector; (2) the gap between
// ReleaseAndDisarm and the physical key release.
bool IsActive() {
    if (g_rot.active) return true;
    return acc::hotkeys::Held(acc::hotkeys::Action::CameraOrient);
}

void Tick() {
    void* camera = GetCamera();

    // In-flight rotation: tick the state machine.
    if (g_rot.active) {
        if (!camera) {
            // Camera vanished (area transition / shutdown) — release the
            // key so it isn't stranded across the load screen.
            SendKey(g_rot.holdScan, /*down=*/false);
            acclog::Write("CameraOrient",
                          "release: reason=camera_lost key=%c",
                          g_rot.debugKey);
            g_rot.active = false;
        } else {
            float curYawRad = 0.0f;
            if (!ReadCurrentEngineYawRad(camera, curYawRad)) {
                ReleaseAndDisarm("getyaw_fault", 0.0f);
            } else {
                float remaining = NormaliseRad(
                    g_rot.targetEngineYawRad - curYawRad);
                DWORD nowMs = GetTickCount();

                // NormaliseRad on delta-yaw handles ±π wrap.
                bool   useRate     = false;
                float  rateRadPerMs = 0.0f;
                float  ttaMs       = 0.0f;
                if (g_rot.haveRateSample) {
                    DWORD dtMs = nowMs - g_rot.prevTickMs;
                    if (dtMs > 0) {
                        float dyaw = NormaliseRad(
                            curYawRad - g_rot.prevYawRad);
                        rateRadPerMs = dyaw / static_cast<float>(dtMs);
                        // Same-sign on rate + remaining = rotating toward
                        // target; opposite = wrong-side wrap, fall back
                        // to static window.
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

                // |remaining| grown past initial + 45° = sailed past in
                // the wrong direction (engine ignored input + user drove
                // the opposite way).
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
                                  "decide: %s remaining=%.2f° "
                                  "rate=%.3f°/ms tta=%.0fms "
                                  "(useRate=%d)",
                                  reason,
                                  remaining * kRadToDeg,
                                  rateRadPerMs * kRadToDeg,
                                  ttaMs,
                                  useRate ? 1 : 0);
                    ReleaseAndDisarm(reason, curYawRad);
                }
            }
        }
        // Eat re-press mid-rotation.
        if (acc::hotkeys::Pressed(acc::hotkeys::Action::CameraOrient)) {
            acclog::Write("CameraOrient",
                          "ignore re-press: rotation in flight");
        }
        return;
    }

    // Rising edge — arm a new rotation.
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::CameraOrient)) return;

    if (!camera) {
        acclog::Write("CameraOrient", "skip: no camera (main menu / pre-spawn)");
        return;
    }
    float curYawRad = 0.0f;
    if (!ReadCurrentEngineYawRad(camera, curYawRad)) {
        acclog::Write("CameraOrient", "skip: Camera::GetYaw call faulted");
        return;
    }

    bool beaconMode = acc::guidance::beacon::IsActive();
    float targetEngineYawRad = 0.0f;
    Vector waypoint = {0.0f, 0.0f, 0.0f};

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
    } else {
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

    // Engine frame (0=East, CCW+): positive delta = CCW = A, negative = D.
    bool isA = (delta > 0.0f);
    WORD scan = isA ? kDikA : kDikD;
    char debugKey = isA ? 'A' : 'D';

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
                  "arm: mode=%s cur=%.1f° target=%.1f° delta=%.1f° key=%c "
                  "(scan=0x%02x)",
                  beaconMode ? "beacon" : "cardinal",
                  curYawRad * kRadToDeg,
                  targetEngineYawRad * kRadToDeg,
                  delta * kRadToDeg,
                  debugKey,
                  scan);
}

}  // namespace acc::camera_orient
