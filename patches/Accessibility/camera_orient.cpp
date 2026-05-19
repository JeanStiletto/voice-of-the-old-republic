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
#include "strings.h"
#include "tolk.h"

namespace acc::camera_orient {

namespace {

// ----- Chain walk constants ----------------------------------------------
// Same chain probe_camera_distance walks for its GetCamera() helper. The
// camera is stored on the CSWCModule at +0x40; CSWCModule itself is at
// CClientExoAppInternal +0x18.
constexpr size_t    kClientInternalModuleOffset = 0x18;
constexpr size_t    kCSWCModuleCameraOffset     = 0x40;

// Rendered camera orientation quaternion at `modCamera + 0x88` (qx, qy,
// qz, qw). This is the source-of-truth orientation the renderer applies
// each frame — probe_camera_state extracts yaw from it as
// `2 * atan2(qz, qw)`. We use the same expression for our closed-loop
// arrival check.
//
// Why not Camera::GetYaw (@0x45C170, the engine getter):
//   - It reads `behavior+0x3C` through a vtable[7] null-gate.
//   - For the orbital camera (CSWCameraOnAStick) vtable[7] points to
//     `return_zero @0x63e7f0`, so the gate ALWAYS fails and GetYaw
//     returns 0.0 in normal play. Verified live in the 21:46 log
//     where every "cur" sample read 0.0° regardless of the camera's
//     actual rotation.
//   - The quaternion path, by contrast, is what the renderer uses and
//     reflects the camera's true rotation in real time.
constexpr size_t kModCameraQuaternionOffset = 0x88;

// DirectInput scan codes (Set 1, US layout). KOTOR's keyboard input
// reaches the engine through DirectInput's `CExoInputInternal::
// PollInput(0x11c, 0)` path — and DirectInput reads SCAN CODES, not
// virtual keys. SendInput with only `wVk` set posts a Win32 message
// (which GetAsyncKeyState sees, including camera_announce's dead-
// reckoning) but **never reaches DirectInput** — which is exactly the
// failure mode the 21:46 log showed: camera_announce announced sector
// crossings while the engine camera stayed at 0°. The fix is to set
// `KEYEVENTF_SCANCODE` and `ki.wScan = DIK_*` so the input lands in
// the DirectInput / raw-input pipeline the engine reads.
constexpr WORD kDikA = 0x1E;   // DIK_A
constexpr WORD kDikD = 0x20;   // DIK_D

// ----- Rotation state machine --------------------------------------------
// Single live rotation at a time. While `active`, the dispatch holds the
// chosen direction key synthesised via SendInput and re-evaluates
// remaining distance each tick until within tolerance. Tolerance and
// timeout are chosen to swallow the engine's per-frame DPS step
// (~3.3°/frame at 60fps + 200°/s default) so we never miss the target
// arc by more than one frame.
struct Rotation {
    bool   active             = false;
    WORD   holdScan           = 0;       // DIK scan code (kDikA / kDikD)
    char   debugKey           = 0;       // 'A' / 'D' for log readability
    float  targetEngineYawRad = 0.0f;
    float  initialAbsDeltaRad = 0.0f;    // for overshoot detection
    bool   beaconAnnounce     = false;
    DWORD  startedMs          = 0;
    // Rate-based predictive-release state. Each tick stores the
    // observed yaw + timestamp so the next tick can derive a per-ms
    // angular speed and decide whether to release the key NOW so the
    // engine's in-flight rotation lands ON the target rather than
    // past it. Without prediction, the user perceives "swings over"
    // — we'd release at "remaining < tolerance" but the engine kept
    // rotating for another 30-50ms of input-pipeline latency.
    float  prevYawRad         = 0.0f;
    DWORD  prevTickMs         = 0;
    bool   haveRateSample     = false;
};
Rotation g_rot;

// Release-lookahead budget — how far in the future (in ms) we project
// the camera's position when deciding to release. Set to swallow the
// SendInput keyup → DirectInput sees-release → engine stops applying
// AcclTurnCamera round-trip plus our own ~1 frame of remaining
// processing. 40ms at 200°/s = 8° of look-ahead, which lines up with
// the overshoot the user reported in patch-20260518-215110.log.
//
// camera_announce → camera_orient ordering already gives us SAME-tick
// freshness on the yaw read, so this budget is pure "engine still
// applying input after our keyup" latency.
constexpr DWORD kReleaseLookaheadMs = 40;

// Fallback static arrival window — used when we don't have a usable
// rate sample yet (first tick after arm) or when the rate is too
// small to project (rotation stalled / not started). 0.05 rad ≈ 2.9°.
constexpr float kFallbackArrivalRad = 0.05f;

// Minimum |rate| in rad/ms before we trust the rate-based projection.
// Below this we fall back to the static arrival window — projecting
// "time to target" with a near-zero rate explodes to garbage values.
// 1e-5 rad/ms = ~0.57°/s, well below any real keyboard rotation rate
// (engine default 200°/s = 3.5e-3 rad/ms).
constexpr float kMinRateRadPerMs = 1e-5f;

// Safety cap — even at 0°/s DPS the engine should resolve a half-turn
// in well under 2 seconds at default 200°/s. 3 seconds catches the
// "engine ignored our input" failure (loading screen, modal popup,
// scripted camera takeover) without leaving the key wedged down.
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

// Read engine yaw. Prefers camera_announce's position-derived value
// (atan2(player - camera) — single-valued and smooth across full
// rotations) because the rendered quaternion at modCamera+0x88 is
// MULTI-VALUED: q and -q represent the same physical rotation, and
// 2*atan2(qz, qw) returns readings 360° apart depending on which
// hemisphere the engine's quaternion happens to be in. Verified in
// patch-20260518-215110.log: arm cur=-277° / release cur=+83° from
// quaternion read are antipodal representations of the SAME angle
// (`-277 + 360 = +83`), causing our arrival check to never fire and
// the rotation to ride out the 3s safety timeout.
//
// camera_announce::TryGetCameraEngineYawDegrees derives yaw from the
// camera→player position vector (Pillar 2 dead-reckon path runs
// AFTER us in core_tick, so we see last frame's value — one tick of
// lag, comfortably within our ~3° arrival tolerance).
//
// Falls back to the quaternion path if camera_announce hasn't yet
// anchored (first-tick state before its first valid position read).
bool ReadCurrentEngineYawRad(void* camera, float& out) {
    float degsFromAnnounce = 0.0f;
    if (acc::camera_announce::TryGetCameraEngineYawDegrees(
            degsFromAnnounce)) {
        out = degsFromAnnounce * kDegToRad;
        return true;
    }
    if (!camera) return false;
    __try {
        unsigned char* q = reinterpret_cast<unsigned char*>(camera) +
                           kModCameraQuaternionOffset;
        float qz = *reinterpret_cast<float*>(q + 0x8);
        float qw = *reinterpret_cast<float*>(q + 0xc);
        out = 2.0f * std::atan2(qz, qw);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ----- SendInput keyboard primitive --------------------------------------
// Scancode-based — KEYEVENTF_SCANCODE routes through Raw Input which
// DirectInput consumes. Plain-VK SendInput posts only to the Win32
// message queue (GetAsyncKeyState sees it) but is invisible to
// DirectInput, which is why our first attempt rotated camera_announce's
// dead-reckoning but not the engine's actual camera.

void SendKey(WORD scan, bool down) {
    INPUT inp = {};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wScan = scan;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &inp, sizeof(INPUT));
}

// ----- Angle math --------------------------------------------------------

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

// Pick the next cardinal in compass-CW order. Floor compass/90 to find
// the cardinal at-or-just-past us (CW), then +1 mod 4. Repeated presses
// cycle N → E → S → W → N from any starting yaw.
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

// ----- Lifecycle helpers -------------------------------------------------

void ReleaseAndDisarm(const char* reason, float curYawRad) {
    if (!g_rot.active) return;
    SendKey(g_rot.holdScan, /*down=*/false);
    float curCompassDeg = acc::engine::EngineYawToCompass(
        curYawRad * kRadToDeg);
    int curSector = acc::engine::CompassToSector(curCompassDeg);

    if (g_rot.beaconAnnounce) {
        const char* dir = acc::strings::Get(
            acc::engine::SectorString(curSector));
        char msg[64];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(
                          acc::strings::Id::FmtCameraOrientBeacon),
                      dir);
        tolk::Speak(msg, /*interrupt=*/true);
        // Seed camera_announce so the post-release hysteresis doesn't
        // immediately re-announce the same direction word we just
        // spoke as part of the beacon cue. Cardinal mode (else branch)
        // intentionally lets camera_announce announce — that IS the
        // cardinal-cycle speech.
        acc::camera_announce::SeedLastSpokenSector(curSector);
    }

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

// Reports true whenever camera_announce should stay muted. Covers two
// windows the in-flight `g_rot.active` flag alone misses:
//
//   1. The rising-edge tick itself. camera_announce::Tick runs BEFORE
//      camera_orient::Tick in core_tick (so closed-loop arrival reads
//      this-frame's fresh yaw). On the very tick the user presses N,
//      camera_announce would otherwise run while `g_rot.active` is
//      still false from the previous tick — and announce the pre-
//      rotation sector word a beat before our mute kicks in. Held()
//      catches the press synchronously regardless of dispatch order.
//
//   2. The brief gap between camera_orient::ReleaseAndDisarm and the
//      physical key release (typically 0-1 ticks). Not strictly needed
//      since g_rot.active drops at release, but Held() coverage costs
//      nothing.
bool IsActive() {
    if (g_rot.active) return true;
    return acc::hotkeys::Held(acc::hotkeys::Action::CameraOrient);
}

void Tick() {
    void* camera = GetCamera();

    // ----- Continue an in-flight rotation -------------------------------
    if (g_rot.active) {
        if (!camera) {
            // Camera vanished mid-flight (area transition / shutdown).
            // Release the key so we don't strand it pressed across the
            // load screen.
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

                // Rate-based predictive release. NormaliseRad on the
                // delta-yaw between samples handles the wrap at ±π so
                // a rotation crossing 180° doesn't flip sign on us.
                bool   useRate     = false;
                float  rateRadPerMs = 0.0f;
                float  ttaMs       = 0.0f;
                if (g_rot.haveRateSample) {
                    DWORD dtMs = nowMs - g_rot.prevTickMs;
                    if (dtMs > 0) {
                        float dyaw = NormaliseRad(
                            curYawRad - g_rot.prevYawRad);
                        rateRadPerMs = dyaw / static_cast<float>(dtMs);
                        if (std::fabs(rateRadPerMs) >= kMinRateRadPerMs &&
                            // Same sign on rate + remaining = rotating
                            // TOWARD target. Opposite signs = rotating
                            // away (engine ignored us or wrong-direction
                            // user input); fall back to static window so
                            // we don't release prematurely on the wrong
                            // side of the wrap.
                            ((remaining > 0.0f) == (rateRadPerMs > 0.0f))) {
                            ttaMs = remaining / rateRadPerMs;
                            useRate = true;
                        }
                    }
                }

                bool arrived = useRate
                    ? (ttaMs <= static_cast<float>(kReleaseLookaheadMs))
                    : (std::fabs(remaining) <= kFallbackArrivalRad);

                // Overshoot detection — |remaining| growing past the
                // initial delta by more than a 45° margin means we've
                // sailed past target in the wrong direction (engine
                // ignored input + user's own A/D drove the other way).
                bool overshot = std::fabs(remaining) >
                                g_rot.initialAbsDeltaRad + kPi * 0.25f;

                DWORD elapsed = nowMs - g_rot.startedMs;
                bool timedOut = elapsed >= kTimeoutMs;

                // Roll the rate-sample window forward AFTER the decision
                // so the next tick's projection sees a fresh ~16ms
                // baseline.
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
        // Eat any same-tick second N press while a rotation is in flight
        // so the user can't queue another rotation mid-arc.
        if (acc::hotkeys::Pressed(acc::hotkeys::Action::CameraOrient)) {
            acclog::Write("CameraOrient",
                          "ignore re-press: rotation in flight");
        }
        return;
    }

    // ----- Rising edge — arm a new rotation -----------------------------
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
        // Already pointing where we'd send it — still speak the
        // confirmation in beacon mode so the user knows the orient
        // ran. Skip the input synthesis.
        if (beaconMode) {
            int sector = acc::engine::CompassToSector(
                acc::engine::EngineYawToCompass(
                    targetEngineYawRad * kRadToDeg));
            const char* dir = acc::strings::Get(
                acc::engine::SectorString(sector));
            char msg[64];
            std::snprintf(msg, sizeof(msg),
                          acc::strings::Get(
                              acc::strings::Id::FmtCameraOrientBeacon),
                          dir);
            tolk::Speak(msg, /*interrupt=*/true);
        }
        acclog::Write("CameraOrient",
                      "no-op: already at target (delta=%.2f° within "
                      "tolerance)",
                      delta * kRadToDeg);
        return;
    }

    // Direction: in engine frame (0=East, CCW+), positive delta means
    // CCW = A. Negative delta means CW = D. Verified against
    // camera_announce's documented sign convention (A rotates compass
    // CCW = engine yaw increases).
    bool isA = (delta > 0.0f);
    WORD scan = isA ? kDikA : kDikD;
    char debugKey = isA ? 'A' : 'D';

    DWORD nowMs = GetTickCount();
    g_rot.active              = true;
    g_rot.holdScan            = scan;
    g_rot.debugKey            = debugKey;
    g_rot.targetEngineYawRad  = targetEngineYawRad;
    g_rot.initialAbsDeltaRad  = std::fabs(delta);
    g_rot.beaconAnnounce      = beaconMode;
    g_rot.startedMs           = nowMs;
    // Seed the rate-sample window with the arm-time yaw so the first
    // in-flight tick can already derive a rate (rather than burning
    // one tick waiting for a baseline). The first sample's dt will be
    // ~16ms, which is the actual frame period — same accuracy as
    // every subsequent sample.
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
