#include "probe_camera_state.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#pragma comment(lib, "user32.lib")

#include "camera_announce.h"  // TryGetCameraEngineYawDegrees
#include "engine_compass.h"
#include "engine_player.h"
#include "log.h"

namespace acc::probe_camera_state {

namespace {

constexpr int  kVK_F12                            = 0x7B;
constexpr size_t kClientInternalModuleOffset      = 0x18;
constexpr size_t kCSWCModuleCameraYawOffset       = 0x98;
constexpr size_t kCSWPlayerControlCameraOffset    = 0x08;  // CAurCamera*
// Candidate offsets inside the camera object (CSWCameraOnAStick is the
// gameplay orbital camera; field32_0x90 was referenced in
// Control_UpdateCameraDesiredOrientation as a yaw offset).
constexpr size_t kCameraYawOffsetA                = 0x90;
constexpr size_t kCameraYawOffsetB                = 0x94;
constexpr size_t kCameraYawOffsetC                = 0x40;

bool g_prevF12 = false;

bool IsForegroundOurs() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void* GetClientInternal() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* clientApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!clientApp) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientApp) +
            kClientExoAppInternalOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetCSWCModule(void* clientInternal) {
    if (!clientInternal) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientInternal) +
            kClientInternalModuleOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetPlayerControlCamera(void* clientInternal) {
    if (!clientInternal) return nullptr;
    __try {
        void* playerControl = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientInternal) +
            kClientAppPlayerControlOffset);
        if (!playerControl) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(playerControl) +
            kCSWPlayerControlCameraOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

template <typename T>
T SafeRead(void* base, size_t offset, T fallback) {
    if (!base) return fallback;
    __try {
        return *reinterpret_cast<T*>(
            reinterpret_cast<unsigned char*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

}  // namespace

void PollWin32() {
    if (!IsForegroundOurs()) {
        g_prevF12 = false;
        return;
    }
    bool now    = (GetAsyncKeyState(kVK_F12) & 0x8000) != 0;
    bool rising = now && !g_prevF12;
    g_prevF12 = now;
    if (!rising) return;

    void* clientInternal = GetClientInternal();
    void* module         = GetCSWCModule(clientInternal);
    void* pcCamera       = GetPlayerControlCamera(clientInternal);

    // CSWCModule.camera (Camera*) at +0x40 — this is the Module-owned
    // gameplay camera. Camera embeds a Gob at +0x04 which has its world
    // orientation quaternion at Gob+0x84 → Camera+0x88. Position at
    // Camera+0x7C.
    void* modCamera = SafeRead<void*>(module, 0x40, nullptr);

    // Quaternion at modCamera + 0x88 (x, y, z, w as floats).
    float qx = SafeRead<float>(modCamera, 0x88 + 0x0, 0.0f);
    float qy = SafeRead<float>(modCamera, 0x88 + 0x4, 0.0f);
    float qz = SafeRead<float>(modCamera, 0x88 + 0x8, 0.0f);
    float qw = SafeRead<float>(modCamera, 0x88 + 0xc, 0.0f);
    // Camera position at +0x7C
    float px = SafeRead<float>(modCamera, 0x7c + 0x0, 0.0f);
    float py = SafeRead<float>(modCamera, 0x7c + 0x4, 0.0f);
    float pz = SafeRead<float>(modCamera, 0x7c + 0x8, 0.0f);

    // Derived yaw from quaternion. KOTOR/Aurora world is Y-up... actually
    // Z-up per Engine yaw convention (rotation about Z axis, 0=+X). For
    // a pure-Z-rotation quaternion q = (0,0,sin(yaw/2),cos(yaw/2)):
    //   yaw = 2 * atan2(z, w)
    // If the quaternion also has X/Y components (camera pitched), this
    // approximation still extracts the Z-rotation portion reasonably for
    // the level-camera orbital case.
    float yawRad = 2.0f * std::atan2(qz, qw);
    float yawDeg = yawRad * 57.29577951308232f;
    while (yawDeg < 0.0f) yawDeg += 360.0f;
    while (yawDeg >= 360.0f) yawDeg -= 360.0f;
    float yawCompass = acc::engine::EngineYawToCompass(yawDeg);

    // Read the original candidates too for back-comparison.
    float cam_0x90 = SafeRead<float>(pcCamera, 0x90, 0.0f);
    float cam_0x94 = SafeRead<float>(pcCamera, 0x94, 0.0f);

    float playerEngineYaw = 0.0f;
    bool playerOk = acc::engine::GetPlayerYawDegrees(playerEngineYaw);
    float playerCompass = playerOk
        ? acc::engine::EngineYawToCompass(playerEngineYaw)
        : -1.0f;

    float reckonedCamEngineYaw = 0.0f;
    bool reckonOk = acc::camera_announce::TryGetCameraEngineYawDegrees(
        reckonedCamEngineYaw);
    float reckonedCamCompass = reckonOk
        ? acc::engine::EngineYawToCompass(reckonedCamEngineYaw)
        : -1.0f;

    constexpr float kRadToDeg = 57.29577951308232f;
    auto asDeg = [&](float v) {
        float d = std::fmod(v * kRadToDeg + 360.0f, 360.0f);
        if (d < 0.0f) d += 360.0f;
        return d;
    };

    acclog::Write("ProbeCamera",
        "F12: modCam=%p pcCam=%p | "
        "quat=(%.3f,%.3f,%.3f,%.3f) -> yawEng=%.2f yawCompass=%.2f | "
        "campos=(%.2f,%.2f,%.2f) | "
        "pcCam[0x90]=%.4f (rad->%.1f) pcCam[0x94]=%.4f (rad->%.1f) | "
        "player_eng=%.2f player_compass=%.2f | "
        "reckoned_eng=%.2f reckoned_compass=%.2f",
        modCamera, pcCamera,
        qx, qy, qz, qw, yawDeg, yawCompass,
        px, py, pz,
        cam_0x90, asDeg(cam_0x90),
        cam_0x94, asDeg(cam_0x94),
        playerEngineYaw, playerCompass,
        reckonedCamEngineYaw, reckonedCamCompass);
}

}  // namespace acc::probe_camera_state
