#include "camera_spin_guard.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "camera_orient.h"   // IsActive — our own snap-turn drives the camera
#include "engine_app.h"      // GetClientAppInternal — K2 viewport-object chain
#include "engine_keymap.h"   // AnyMovementCommanded — device-independent input
#include "engine_manager.h"  // kAddrGuiManagerPtr
#include "engine_options.h"  // GetMouseLook
#include "engine_panels.h"   // IsForegroundUiBlocking — cinematic / screen gate
#include "engine_player.h"   // GetCameraYawRadians / GetCameraPosition /
                             // GetPlayerPosition + chain constants
#include "core_tick.h"       // NowMs — the rate below divides by an inter-tick
                             // interval, which GetTickCount cannot resolve
#include "hotkeys.h"         // IsForegroundGame — guard only when focused
#include "log.h"
#include "engine_offsets_select.h"
#include "engine_rebase.h"

#pragma comment(lib, "user32.lib")

namespace acc::camera_spin_guard {

namespace {

constexpr float kRadToDeg = 57.29577951308232f;

// Engine globals (decompiled, see UpdateCamera @0x5f5e10).
// K2 witnessed 2026-08-02 in the K2 edge-band code at 0x0078C520-0x0078C6A0
// (found via the 0.001f-initialised .data global): screenFramePercentage
// twin = 0x00A10910 (both FMULs), mouse_x read as [[GuiManager global]+0]
// (mouse_y +4), viewport width as int16 [+0x6c] on the object at
// [client internal + 0x274] — K2 splits the width off the GuiManager.
const uintptr_t kAddrScreenFramePercent   = acc::addr::PickGlobal(0x007A2444, 0x00A10910);  // float
// rightClickHeld: LOG-ONLY (episode START diagnostic). No K2 witness yet —
// SafeRead declines to 0 there. Not part of the guard's firing logic.
const uintptr_t kAddrRightClickHeld       = acc::addr::TodoGlobal(0x008338F0);  // int
const size_t    kGuiMouseXOffset          = acc::off::Same(0x00);        // ulong
const size_t    kGuiViewportWidthOffset   = acc::off::Same(0x6C);        // int16
// K2 only: the viewport object holding width/height (+0x6c/+0x6e) hangs off
// the client internal at +0x274 (witnessed via the getter 0x0073FEA0). On
// K1 the width lives on the GuiManager itself, so this is unused there.
const size_t    kK2InternalViewportObjOffset = 0x274;

// Cursor-edge guard (Option A): after a load the cursor parks frozen at the
// screen border (mouse_x=0) and the post-load input pipeline ignores
// injected motion (SetCursorPos and SendInput both failed to move it — see
// logs 124537/131734). Since nothing is actively re-setting mouse_x, we
// write the engine cursor position directly to viewport centre, which
// bypasses the broken input path and takes the cursor out of the edge band.
// If the engine turns out to re-derive mouse_x each frame, the diagnostic
// will show it bouncing back to 0 and we move the clamp into a UpdateCamera
// detour instead.

// Guard fires once the camera is actually rotating, so it stays quiet when
// the camera is genuinely static (e.g. a paused menu with the cursor parked
// at an edge). 3 deg/s catches the onset before AcclTurnCamera ramps up.
constexpr float kGuardRateThresh = 3.0f;

// Logging is episode-based, not per-frame: one START line when an edge-guard
// episode begins, one END summary when the cursor has stayed clear of the
// band this long. Collapses a multi-second bounce (hundreds of frames) to
// two lines.
constexpr DWORD kEpisodeQuietMs = 500;

// Sanity tripwire for the camera-yaw read (fix #1). quat- and position-
// derived yaw agreed to 0.0 in bench testing, so a divergence CAN mean
// GetCameraYawRadians regressed — but only under the gates at the call site.
// posYaw is the camera->player BEARING, which equals the camera's facing only
// while the camera sits in its normal follow pose. Ungated, this fired on
// every cinematic shot, every camera lag during a turn and every combat orbit:
// 319 lines across 25 K1 dev sessions at a 136 deg median, and 235 in one
// field session — noise that reads like a regression and is not one.
constexpr float kReadAnomalyDeg = 2.0f;

// A regression is PERSISTENT; legitimate divergence is transient. Require the
// divergence to hold continuously for this long, with every gate satisfied,
// before saying anything.
constexpr DWORD kAnomalyPersistMs = 3000;
constexpr DWORD kAnomalyRepeatMs  = 10000;

// Plausible chase-cam distance to the player, metres. Outside this the camera
// is not in its follow pose (scripted shot, mid-transition) and the bearing
// cross-check means nothing. Deliberately generous — the band exists to
// exclude far cinematic framing, not to tune out real divergence — and the
// measured distance is logged so field data can narrow it.
constexpr float kFollowDistMinM = 1.0f;
constexpr float kFollowDistMaxM = 15.0f;

// ---- Uncommanded-rotation watch -------------------------------------------
// Rotation slower than this is settle/drift, not a spin.
constexpr float kSpinRateThresh = 20.0f;   // deg/s
// Report every full turn the camera makes with no input behind it. One turn is
// unambiguous: no legitimate engine behaviour rotates a player-controlled
// camera 360 degrees while the player commands nothing.
constexpr float kSpinReportDeg  = 360.0f;
// Stop logging after this many turns in one episode — a runaway is established
// by then and the log has the state it needs.
constexpr int   kSpinMaxReports = 5;

template <typename T>
T SafeRead(uintptr_t addr, T fallback) {
    __try {
        return *reinterpret_cast<T*>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

template <typename T>
T SafeReadOff(void* base, size_t off, T fallback) {
    if (!base) return fallback;
    __try {
        return *reinterpret_cast<T*>(
            reinterpret_cast<unsigned char*>(base) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

template <typename T>
bool SafeWriteOff(void* base, size_t off, T value) {
    if (!base) return false;
    __try {
        *reinterpret_cast<T*>(
            reinterpret_cast<unsigned char*>(base) + off) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float NormDeg(float d) {
    d = std::fmod(d, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d;
}

// Signed shortest delta a-b in (-180, 180].
float AngularDelta(float a, float b) {
    return std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
}

struct State {
    bool  have       = false;
    float prevYaw    = 0.0f;   // quaternion-derived, deg
    DWORD prevMs     = 0;
    bool  havePos    = false;
    float prevPosYaw = 0.0f;   // camera->player bearing, deg
    DWORD prevPosMs  = 0;
};
State g_state;

// One edge-guard "episode" = a contiguous bout of edge contact (with brief
// corrected gaps). Logged as START + END-summary instead of per frame.
struct Episode {
    bool  active      = false;
    DWORD startMs     = 0;
    DWORD lastEdgeMs  = 0;
    int   corrections = 0;
    int   edgeFrames  = 0;
    float maxRate     = 0.0f;
    float startYaw    = 0.0f;
};
Episode g_ep;

// ---- Read-anomaly tripwire state (see the gate note at the call site) ------
DWORD g_anomalySinceMs = 0;   // when the CURRENT divergence run began; 0 = none
DWORD g_lastAnomalyMs  = 0;   // last line emitted, for the repeat throttle
bool  g_anomalyOpen    = false;

// ---- Uncommanded-rotation watch state -------------------------------------
// Cause-agnostic: accumulates camera rotation while the player is commanding
// none. The edge-guard above only knows ONE cause (cursor in the edge band);
// this notices the symptom whatever produced it.
struct SpinWatch {
    bool  active      = false;
    DWORD startMs     = 0;
    float accumQuat   = 0.0f;  // signed, deg — from the orientation quaternion
    float accumPos    = 0.0f;  // signed, deg — from the camera->player bearing
    float maxRate     = 0.0f;
    int   frames      = 0;
    int   reports     = 0;     // full turns already reported this episode
};
SpinWatch g_spin;

void ResetSpinWatch() {
    g_spin.active    = false;
    g_spin.accumQuat = 0.0f;
    g_spin.accumPos  = 0.0f;
    g_spin.maxRate   = 0.0f;
    g_spin.frames    = 0;
    g_spin.reports   = 0;
}

// Close the watch, summarising only episodes worth a line.
//
// Releasing a turn key ends "commanded" a moment before the chase-cam finishes
// catching up, so a short uncommanded rotation is NORMAL and must stay silent —
// otherwise every turn the player makes ends in a log line. A reported episode
// always summarises; an unreported one only if it ran long AND swept far enough
// that settle cannot explain it.
constexpr DWORD kSpinSummaryMs  = 2000;
constexpr float kSpinSummaryDeg = 90.0f;

void EndSpinWatch(DWORD now, const char* why) {
    if (g_spin.active) {
        DWORD dur = now - g_spin.startMs;
        float swept = std::fabs(g_spin.accumQuat) > std::fabs(g_spin.accumPos)
                          ? std::fabs(g_spin.accumQuat)
                          : std::fabs(g_spin.accumPos);
        if (g_spin.reports > 0 ||
            (dur >= kSpinSummaryMs && swept >= kSpinSummaryDeg)) {
            acclog::Write("CameraSpinDiag",
                "uncommanded-rotation END: duration=%ums frames=%d "
                "quatTurn=%.0f deg posTurn=%.0f deg maxRate=%.0f deg/s "
                "(ended: %s)",
                dur, g_spin.frames, g_spin.accumQuat, g_spin.accumPos,
                g_spin.maxRate, why);
        }
    }
    ResetSpinWatch();
}

void EndEpisode(DWORD now, float endYaw) {
    acclog::Write("CameraSpinDiag",
        "edge-guard END: corrections=%d edgeFrames=%d duration=%ums "
        "maxRate=%.0f deg/s netYaw=%.0f->%.0f",
        g_ep.corrections, g_ep.edgeFrames, now - g_ep.startMs,
        g_ep.maxRate, g_ep.startYaw, endYaw);
    g_ep.active = false;
}

// True while something other than the player is entitled to move the camera:
// an engine cinematic / dialog shot, a blocking screen, or our own N-hotkey
// snap-turn. Both the tripwire and the spin watch are meaningless then — the
// camera is legitimately somewhere the player did not put it.
//
// IsForegroundUiBlocking already tests HasActiveDialogPanel first (it is the
// predicate's own rule 1), so the cinematic case needs no separate scan.
bool CameraDrivenByNonPlayer() {
    return acc::engine::IsForegroundUiBlocking() ||
           acc::camera_orient::IsActive();
}

}  // namespace

void Tick() {
    DWORD now = acc::tick::NowMs();

    // In-world gate: player position resolvable. Resets the rate sample
    // when we leave the world so we don't log a bogus jump on re-entry, and
    // closes out any open episode (e.g. an area transition mid-spin).
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        g_state.have    = false;
        g_state.havePos = false;
        g_anomalySinceMs = 0;
        EndSpinWatch(now, "left the world");
        if (g_ep.active) EndEpisode(now, g_ep.startYaw);
        return;
    }

    // --- Quaternion-derived yaw (the fix under test) ---
    float quatYawDeg = -1.0f;
    {
        float r = 0.0f;
        if (acc::engine::GetCameraYawRadians(r)) quatYawDeg = NormDeg(r * kRadToDeg);
    }

    // --- Position-derived yaw (independent cross-check) ---
    // Also an independent SPIN witness: an edge-turn orbits the camera around
    // the player, so this bearing sweeps too. Reading rotation from both means
    // a dead quaternion read cannot hide a spin from the watch below — which
    // matters, because the guard's own rotation test reads only the quaternion.
    float posYawDeg = -1.0f;
    float camDistM  = -1.0f;
    {
        Vector camPos;
        if (acc::engine::GetCameraPosition(camPos)) {
            float dx = playerPos.x - camPos.x;
            float dy = playerPos.y - camPos.y;
            camDistM = std::sqrt(dx * dx + dy * dy);
            if (camDistM >= 0.1f) {
                posYawDeg = NormDeg(std::atan2(dy, dx) * kRadToDeg);
            }
        }
    }

    // --- Rotation rate from the quaternion yaw ---
    //
    // A tick is one rendered frame, so this divides by an interval shorter
    // than GetTickCount can resolve (~15.6 ms against ~16.7 ms at 60 Hz vsync,
    // ~6.9 ms at 144 Hz). Two ticks in one quantum give dt == 0, and on the
    // old clock that silently reported 0 deg/s — below kGuardRateThresh, so
    // the guard skipped a correction on a frame the camera really was spinning.
    // Self-correcting on the next frame, but it is the same defect that made
    // camera_announce speak three false stop cues in one second.
    //
    // Timing now comes from the tick clock (QPC-backed, sampled once before
    // the fan-out), and a zero-length interval HOLDS the previous sample
    // rather than replacing it: a measurement we do not have yet is not a
    // measurement of zero, and keeping the old sample lets the next tick
    // measure across a real interval.
    float rateDegPerSec = 0.0f;
    bool  haveRate      = false;
    float quatStepDeg   = 0.0f;   // signed rotation since the last sample
    if (g_state.have && quatYawDeg >= 0.0f) {
        DWORD dt = now - g_state.prevMs;
        if (dt > 0) {
            quatStepDeg = AngularDelta(quatYawDeg, g_state.prevYaw);
            rateDegPerSec = quatStepDeg * 1000.0f / (float)dt;
            haveRate = true;
        }
    }
    if (quatYawDeg >= 0.0f && (haveRate || !g_state.have)) {
        g_state.prevYaw = quatYawDeg;
        g_state.prevMs  = now;
        g_state.have    = true;
    }
    // The bearing sample is tracked INDEPENDENTLY of the quaternion one, not
    // gated on haveRate: the whole point of the second witness is that it keeps
    // working when the first stops. Gating it on the first would make a dead
    // quaternion read freeze this sample too and hide exactly the case it
    // exists to catch.
    float posStepDeg  = 0.0f;
    float posRateDps  = 0.0f;
    bool  havePosStep = false;
    if (posYawDeg >= 0.0f) {
        DWORD dt = now - g_state.prevPosMs;
        if (g_state.havePos && dt > 0) {
            posStepDeg  = AngularDelta(posYawDeg, g_state.prevPosYaw);
            posRateDps  = posStepDeg * 1000.0f / (float)dt;
            havePosStep = true;
        }
        if (havePosStep || !g_state.havePos) {
            g_state.prevPosYaw = posYawDeg;
            g_state.prevPosMs  = now;
            g_state.havePos    = true;
        }
    }

    // --- Cursor / edge-band state (the suspected driver) ---
    void* gui = SafeRead<void*>(kAddrGuiManagerPtr, nullptr);
    uint32_t mouseX = SafeReadOff<uint32_t>(gui, kGuiMouseXOffset, 0);
    // Width source is per-game: K1 keeps it on the GuiManager, K2 on the
    // viewport object at [internal+0x274] (see the constant's witness note).
    void* vpObj = gui;
    if (acc::game::IsKotor2()) {
        vpObj = SafeReadOff<void*>(acc::engine::GetClientAppInternal(),
                                   kK2InternalViewportObjOffset, nullptr);
    }
    int16_t  vpW    = SafeReadOff<int16_t>(vpObj, kGuiViewportWidthOffset, 0);
    float pct       = SafeRead<float>(kAddrScreenFramePercent, 0.0f);
    // Floor the detection band well above what the config arithmetic gives. At
    // an 800px viewport with the default screenFramePercentage (0.001) that is
    // 0.8px, and the old 1px floor proved too tight: a cursor parked at x=2 spun
    // the camera for the rest of the session in patch-20260724-230031.log while
    // this guard never fired once. So the engine's real turn band is wider than
    // screenFramePercentage suggests, and under-detecting here costs the player
    // an unrecoverable spin. Over-detecting costs nothing — the guard below also
    // requires live rotation, foreground, and in-world, so a wider band cannot
    // misfire on a paused menu with the cursor resting near an edge.
    // Cursor parks must stay clear of this; see kDialogParkX in
    // menus_monitors.cpp.
    constexpr int kMinBandPx = 6;
    int bandPx = (int)((float)vpW * pct);
    if (bandPx < kMinBandPx) bandPx = kMinBandPx;
    bool leftEdge  = vpW > 0 && (int)mouseX <= bandPx;
    bool rightEdge = vpW > 0 && (int)mouseX >= (int)vpW - bandPx;
    const char* edge = leftEdge ? "LEFT" : rightEdge ? "RIGHT" : "none";

    bool edgeNow = leftEdge || rightEdge;

    // --- Cursor-edge guard (Option A, the fix) ---
    // The engine edge-turn (UpdateCamera @0x5f5e10) spins the camera while
    // the cursor sits in the edge band. Fire only on an actual spin:
    // in-world (gated above), foreground, cursor in band, and rotating.
    // Tying it to live rotation means it can't disturb a paused menu (no
    // rotation there). Write the engine cursor position straight to viewport
    // centre — the post-load pipeline ignores injected motion, but a direct
    // field write isn't subject to that.
    bool guardFired = false;
    if (edgeNow &&
        std::fabs(rateDegPerSec) >= kGuardRateThresh &&
        acc::hotkeys::IsForegroundGame() && gui && vpW > 0) {
        guardFired = SafeWriteOff<uint32_t>(
            gui, kGuiMouseXOffset, (uint32_t)(vpW / 2));
    }

    // --- Episode-based logging (START + END summary, not per frame) ---
    if (guardFired && !g_ep.active) {
        int rightClick = SafeRead<int>(kAddrRightClickHeld, 0);
        bool mouseLook = false;
        acc::engine::GetMouseLook(mouseLook);
        g_ep.active      = true;
        g_ep.startMs     = now;
        g_ep.corrections = 0;
        g_ep.edgeFrames  = 0;
        g_ep.maxRate     = 0.0f;
        g_ep.startYaw    = quatYawDeg >= 0.0f ? quatYawDeg : 0.0f;
        acclog::Write("CameraSpinDiag",
            "edge-guard START: mouseX=%u/%d edge=%s rate=%.0f deg/s "
            "rightClick=%d mouseLook=%d",
            mouseX, (int)vpW, edge, rateDegPerSec,
            rightClick, mouseLook ? 1 : 0);
    }
    if (g_ep.active) {
        if (guardFired) g_ep.corrections++;
        if (edgeNow) { g_ep.edgeFrames++; g_ep.lastEdgeMs = now; }
        if (std::fabs(rateDegPerSec) > g_ep.maxRate)
            g_ep.maxRate = std::fabs(rateDegPerSec);
        if (now - g_ep.lastEdgeMs >= kEpisodeQuietMs)
            EndEpisode(now, quatYawDeg >= 0.0f ? quatYawDeg : g_ep.startYaw);
    }

    // --- Uncommanded-rotation watch (cause-agnostic spin detector) ---
    //
    // The edge guard above knows one cause. This knows none: it watches for the
    // SYMPTOM — the camera turning while the player commands nothing — so a
    // spin from a stuck mouse axis, a latched mouse-look, or anything else we
    // have not thought of still leaves a diagnosable record instead of the
    // nothing we had for the field report that prompted this.
    //
    // Log-only by design. Correcting an unknown cause blind risks fighting the
    // engine over a camera that is doing something legitimate we failed to
    // gate; naming the cause comes first.
    //
    // Both queries below scan (panel stack / key state), so resolve once per
    // tick — the tripwire further down wants the same two answers.
    //
    // Caveat worth knowing when reading a report: "commanding" covers keys and
    // the pad, not a right-drag camera rotation, which holds no key. A mouse
    // user could therefore turn the camera and have it read as uncommanded —
    // hence the cursor position and right-click state in the report line. Not
    // gated on here because the only mouse state we can read reliably across
    // both games is the cursor, and a full uncommanded revolution is worth a
    // line even when a mouse is involved.
    bool playerCommanding = acc::engine_keymap::AnyMovementCommanded();
    bool cameraNotOurs    = CameraDrivenByNonPlayer();
    bool watchEligible = !cameraNotOurs && !playerCommanding &&
                         acc::hotkeys::IsForegroundGame();
    if (!watchEligible) {
        EndSpinWatch(now, playerCommanding ? "player input"
                                           : "camera handed over");
    } else {
        float absRate = std::fabs(rateDegPerSec);
        // Either witness may be the live one; take whichever sees motion, on
        // the same deg/s threshold so a dead quaternion read cannot make the
        // bearing witness stricter than intended.
        bool rotating = (haveRate && absRate >= kSpinRateThresh) ||
                        (havePosStep &&
                         std::fabs(posRateDps) >= kSpinRateThresh);
        if (!rotating) {
            EndSpinWatch(now, "rotation stopped");
        } else {
            if (!g_spin.active) {
                g_spin.active    = true;
                g_spin.startMs   = now;
            }
            ++g_spin.frames;
            if (haveRate)    g_spin.accumQuat += quatStepDeg;
            if (havePosStep) g_spin.accumPos  += posStepDeg;
            if (absRate > g_spin.maxRate) g_spin.maxRate = absRate;

            // Report per full turn. A player-controlled camera has no
            // legitimate reason to make a whole revolution on its own.
            float turns = std::fabs(g_spin.accumQuat);
            if (std::fabs(g_spin.accumPos) > turns) turns = std::fabs(g_spin.accumPos);
            int due = (int)(turns / kSpinReportDeg);
            if (due > g_spin.reports && g_spin.reports < kSpinMaxReports) {
                g_spin.reports = due;
                int  rightClick = SafeRead<int>(kAddrRightClickHeld, 0);
                bool mouseLook  = false;
                acc::engine::GetMouseLook(mouseLook);
                acclog::Write("CameraSpinDiag",
                    "UNCOMMANDED ROTATION x%d: quatTurn=%.0f deg posTurn=%.0f deg "
                    "rate=%.0f deg/s duration=%ums frames=%d | mouseX=%u/%d "
                    "band=%dpx edge=%s mouseLook=%d rightClick=%d "
                    "heldMoveVk=0x%02x camDist=%.1fm quatYaw=%.1f posYaw=%.1f",
                    due, g_spin.accumQuat, g_spin.accumPos, rateDegPerSec,
                    now - g_spin.startMs, g_spin.frames, mouseX, (int)vpW,
                    bandPx, edge, mouseLook ? 1 : 0, rightClick,
                    acc::engine_keymap::FirstMovementKeyHeld(),
                    camDistM, quatYawDeg, posYawDeg);
                if (due >= kSpinMaxReports) {
                    acclog::Write("CameraSpinDiag",
                        "uncommanded-rotation: report cap reached (%d turns) — "
                        "further turns this episode not logged",
                        kSpinMaxReports);
                }
            }
        }
    }

    // --- Camera-read sanity tripwire (fix #1 regression guard) ---
    //
    // Gated hard, because the two yaws are only comparable while the camera is
    // in its ordinary follow pose behind the player: no cinematic or blocking
    // screen driving it, no snap-turn of ours, no player turn input (the camera
    // legitimately lags a turn by tens of degrees), and a plausible follow
    // distance. Ungated this was pure noise — see kReadAnomalyDeg. On top of
    // the gates the divergence must PERSIST, which is what separates a real
    // regression from a transient the gates did not happen to catch.
    bool crossCheckMeaningful =
        quatYawDeg >= 0.0f && posYawDeg >= 0.0f &&
        camDistM >= kFollowDistMinM && camDistM <= kFollowDistMaxM &&
        !cameraNotOurs && !playerCommanding;
    if (!crossCheckMeaningful) {
        g_anomalySinceMs = 0;
    } else {
        float d = AngularDelta(quatYawDeg, posYawDeg);
        if (std::fabs(d) <= kReadAnomalyDeg) {
            if (g_anomalyOpen) {
                acclog::Write("CameraSpinDiag",
                    "read-anomaly cleared: quatYaw=%.1f posYaw=%.1f diff=%.1f",
                    quatYawDeg, posYawDeg, d);
                g_anomalyOpen = false;
            }
            g_anomalySinceMs = 0;
        } else {
            if (g_anomalySinceMs == 0) g_anomalySinceMs = now;
            DWORD held = now - g_anomalySinceMs;
            if (held >= kAnomalyPersistMs &&
                (!g_anomalyOpen || now - g_lastAnomalyMs >= kAnomalyRepeatMs)) {
                acclog::Write("CameraSpinDiag",
                    "READ ANOMALY (held %ums): quatYaw=%.1f posYaw=%.1f "
                    "diff=%.1f camDist=%.1fm mouseX=%u/%d — the quaternion yaw "
                    "read may have regressed; the edge guard's rotation test "
                    "depends on it",
                    held, quatYawDeg, posYawDeg, d, camDistM, mouseX, (int)vpW);
                g_lastAnomalyMs = now;
                g_anomalyOpen   = true;
            }
        }
    }
}

}  // namespace acc::camera_spin_guard
