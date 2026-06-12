#include "camera_spin_diag.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "engine_options.h"  // GetMouseLook
#include "engine_player.h"   // GetCameraYawRadians / GetCameraPosition /
                             // GetPlayerPosition + chain constants
#include "hotkeys.h"         // IsForegroundGame — guard only when focused
#include "log.h"

#pragma comment(lib, "user32.lib")

namespace acc::camera_spin_diag {

namespace {

constexpr float kRadToDeg = 57.29577951308232f;

// Engine globals (decompiled, see UpdateCamera @0x5f5e10).
constexpr uintptr_t kAddrGuiManagerPtr        = 0x007A39F4;  // CSWGuiManager**
constexpr uintptr_t kAddrScreenFramePercent   = 0x007A2444;  // float
constexpr uintptr_t kAddrRightClickHeld       = 0x008338F0;  // int
constexpr size_t    kGuiMouseXOffset          = 0x00;        // ulong
constexpr size_t    kGuiViewportWidthOffset   = 0x6C;        // int16

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

// Sanity tripwire for the camera-yaw read (fix #1): quat- and position-
// derived yaw agreed to 0.0 in testing. A persistent divergence above this
// would mean GetCameraYawRadians regressed. Logged at most once a second.
constexpr float kReadAnomalyDeg = 2.0f;

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
    bool  have    = false;
    float prevYaw = 0.0f;   // quaternion-derived, deg
    DWORD prevMs  = 0;
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
DWORD g_lastAnomalyMs = 0;

void EndEpisode(DWORD now, float endYaw) {
    acclog::Write("CameraSpinDiag",
        "edge-guard END: corrections=%d edgeFrames=%d duration=%ums "
        "maxRate=%.0f deg/s netYaw=%.0f->%.0f",
        g_ep.corrections, g_ep.edgeFrames, now - g_ep.startMs,
        g_ep.maxRate, g_ep.startYaw, endYaw);
    g_ep.active = false;
}

}  // namespace

void Tick() {
    DWORD now = GetTickCount();

    // In-world gate: player position resolvable. Resets the rate sample
    // when we leave the world so we don't log a bogus jump on re-entry, and
    // closes out any open episode (e.g. an area transition mid-spin).
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        g_state.have = false;
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
    float posYawDeg = -1.0f;
    {
        Vector camPos;
        if (acc::engine::GetCameraPosition(camPos)) {
            float dx = playerPos.x - camPos.x;
            float dy = playerPos.y - camPos.y;
            if (std::sqrt(dx * dx + dy * dy) >= 0.1f) {
                posYawDeg = NormDeg(std::atan2(dy, dx) * kRadToDeg);
            }
        }
    }

    // --- Rotation rate from the quaternion yaw ---
    float rateDegPerSec = 0.0f;
    if (g_state.have && quatYawDeg >= 0.0f) {
        DWORD dt = now - g_state.prevMs;
        if (dt > 0) {
            rateDegPerSec =
                AngularDelta(quatYawDeg, g_state.prevYaw) * 1000.0f / (float)dt;
        }
    }
    if (quatYawDeg >= 0.0f) {
        g_state.prevYaw = quatYawDeg;
        g_state.prevMs  = now;
        g_state.have    = true;
    }

    // --- Cursor / edge-band state (the suspected driver) ---
    void* gui = SafeRead<void*>(kAddrGuiManagerPtr, nullptr);
    uint32_t mouseX = SafeReadOff<uint32_t>(gui, kGuiMouseXOffset, 0);
    int16_t  vpW    = SafeReadOff<int16_t>(gui, kGuiViewportWidthOffset, 0);
    float pct       = SafeRead<float>(kAddrScreenFramePercent, 0.0f);
    int bandPx = (int)((float)vpW * pct);
    if (bandPx < 1) bandPx = 1;
    bool leftEdge  = vpW > 0 && (int)mouseX <= bandPx;
    bool rightEdge = vpW > 0 && (int)mouseX >= (int)vpW - bandPx;
    const char* edge = leftEdge ? "LEFT" : rightEdge ? "RIGHT" : "none";

    bool edgeNow = leftEdge || rightEdge;

    // --- Cursor-edge guard (Option A, the fix) ---
    // The engine edge-turn (UpdateCamera @0x5f5e10) spins the camera while
    // the cursor sits in the ~1px edge band. Fire only on an actual spin:
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

    // --- Camera-read sanity tripwire (fix #1 regression guard) ---
    if (quatYawDeg >= 0.0f && posYawDeg >= 0.0f) {
        float d = AngularDelta(quatYawDeg, posYawDeg);
        if (std::fabs(d) > kReadAnomalyDeg && now - g_lastAnomalyMs >= 1000) {
            acclog::Write("CameraSpinDiag",
                "READ ANOMALY: quatYaw=%.1f posYaw=%.1f diff=%.1f mouseX=%u/%d",
                quatYawDeg, posYawDeg, d, mouseX, (int)vpW);
            g_lastAnomalyMs = now;
        }
    }
}

}  // namespace acc::camera_spin_diag
