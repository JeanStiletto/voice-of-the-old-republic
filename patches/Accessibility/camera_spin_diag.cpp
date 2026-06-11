#include "camera_spin_diag.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "camera_orient.h"   // IsActive — are WE driving the rotation?
#include "engine_compass.h"  // EngineYawToCompass
#include "engine_options.h"  // GetMouseLook
#include "engine_player.h"   // GetCameraYawRadians / GetCameraPosition /
                             // GetPlayerPosition + chain constants
#include "log.h"

namespace acc::camera_spin_diag {

namespace {

constexpr float kRadToDeg = 57.29577951308232f;

// Engine globals (decompiled, see UpdateCamera @0x5f5e10).
constexpr uintptr_t kAddrGuiManagerPtr        = 0x007A39F4;  // CSWGuiManager**
constexpr uintptr_t kAddrScreenFramePercent   = 0x007A2444;  // float
constexpr uintptr_t kAddrRightClickHeld       = 0x008338F0;  // int
constexpr size_t    kGuiMouseXOffset          = 0x00;        // ulong
constexpr size_t    kGuiViewportWidthOffset   = 0x6C;        // int16
// Accumulated camera turn yaw cached by AcclTurnCamera/TurnCamera —
// CSWCModule + 0x98 (field31_0x98). Grows unbounded during a spin.
constexpr size_t    kClientInternalModuleOffset = 0x18;
constexpr size_t    kCSWCModuleCachedYawOffset  = 0x98;

// Only log when the camera is actually turning, or the cursor is sitting
// in the edge band (early-warning). Below this rate a frame is "quiet".
constexpr float kRotateThreshDegPerSec = 8.0f;

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

// Walk to CSWCModule for the cached-yaw read.
void* GetModule() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* clientApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!clientApp) return nullptr;
        void* clientInternal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientApp) +
            kClientExoAppInternalOffset);
        if (!clientInternal) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientInternal) +
            kClientInternalModuleOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
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

}  // namespace

void Tick() {
    // In-world gate: player position resolvable. Resets the rate sample
    // when we leave the world so we don't log a bogus jump on re-entry.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        g_state.have = false;
        return;
    }

    DWORD now = GetTickCount();

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

    int rightClick = SafeRead<int>(kAddrRightClickHeld, 0);
    bool mouseLook = false;
    acc::engine::GetMouseLook(mouseLook);

    // Engine cached turn accumulator (grows during a spin).
    float cachedYaw = SafeReadOff<float>(GetModule(), kCSWCModuleCachedYawOffset, 0.0f);

    bool weAreDriving = acc::camera_orient::IsActive();

    // Log only when meaningfully rotating OR cursor in the edge band — so
    // the log isolates spin events instead of every quiet frame.
    bool rotating = std::fabs(rateDegPerSec) >= kRotateThreshDegPerSec;
    if (!rotating && edge[0] == 'n') return;

    // Suspect classification: cursor in band → edge-turn driver; rotating
    // with cursor centred and not us → keyboard/axis (our A/D or stuck).
    const char* suspect =
        (leftEdge || rightEdge) ? "EDGE-CURSOR"
        : weAreDriving           ? "camera_orient(us)"
        : rotating               ? "axis/keyboard"
                                 : "-";

    acclog::Write("CameraSpinDiag",
        "rate=%.1f deg/s suspect=%s | quatYaw=%.1f posYaw=%.1f (diff=%.1f) "
        "cachedYaw=%.3f | mouseX=%u/%d band=%dpx pct=%.4f edge=%s | "
        "rightClick=%d mouseLook=%d weDrive=%d",
        rateDegPerSec, suspect,
        quatYawDeg, posYawDeg,
        (quatYawDeg >= 0.0f && posYawDeg >= 0.0f)
            ? AngularDelta(quatYawDeg, posYawDeg) : -999.0f,
        cachedYaw,
        mouseX, (int)vpW, bandPx, pct, edge,
        rightClick, mouseLook ? 1 : 0, weAreDriving ? 1 : 0);
}

}  // namespace acc::camera_spin_diag
