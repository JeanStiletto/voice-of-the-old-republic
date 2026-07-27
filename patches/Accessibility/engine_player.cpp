// core player reads: position, facing, yaw, area and camera.
//
// Party/leader resolution and the input-disable state machine were split
// out by the Phase-1 structure pass (refactoring candidate 5) into
// engine_player_party.cpp and engine_player_inputlock.cpp. This file also
// owns GetPlayerServerObject, the app-manager chain walk all three TUs
// read through - see engine_player_internal.h.

#include "engine_player.h"
#include "engine_player_internal.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "engine_area.h"   // GetObjectHandle / GetObjectDisplayNameByHandle /
                           // kCreatureStatsPtrOffset etc. — used by GetActiveLeaderName
#include "engine_reads.h"  // ReadCExoString, ExtractTextOrStrRef
#include "log.h"           // acclog::Write — diagnostics on the
#include "engine_rebase.h"
                           // SetPlayerInputEnabled toggle / auto-restore tick

namespace acc::engine {

// Declared in engine_player_internal.h (with the chain-walk rationale) —
// no longer file-static because the party and input-lock TUs read it too.
void* GetPlayerServerObject() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;

        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return nullptr;

        auto getCreature = reinterpret_cast<PFN_GetPlayerCreature>(
            kAddrGetPlayerCreature);
        void* clientCreature = getCreature(exoApp);
        if (!clientCreature) return nullptr;

        void* serverObject = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientCreature) +
            kClientObjectServerObjectOffset);
        return serverObject;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool GetPlayerPosition(Vector& out) {
    void* obj = GetPlayerServerObject();
    if (!obj) return false;
    __try {
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(obj) +
            kServerObjectPositionOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetPlayerFacing(Vector& out) {
    void* obj = GetPlayerServerObject();
    if (!obj) return false;
    __try {
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(obj) +
            kServerObjectOrientationOffset);
        out.z = 0.0f;  // engine zeros z for object facing — see Q1
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetPlayerYawDegrees(float& out) {
    Vector facing;
    if (!GetPlayerFacing(facing)) return false;
    if (facing.x == 0.0f && facing.y == 0.0f) return false;
    constexpr float kRadToDeg = 57.29577951308232f;  // 180 / π
    float deg = std::atan2(facing.y, facing.x) * kRadToDeg;
    if (deg < 0.0f) deg += 360.0f;
    out = deg;
    return true;
}

void* GetPlayerArea() {
    void* obj = GetPlayerServerObject();
    if (!obj) return nullptr;
    __try {
        auto getArea = reinterpret_cast<PFN_CSWSObjectGetArea>(
            kAddrCSWSObjectGetArea);
        return getArea(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool GetCameraPosition(Vector& out) {
    constexpr size_t kClientInternalModuleOffset = 0x18;
    constexpr size_t kCSWCModuleCameraOffset     = 0x40;
    constexpr size_t kCameraGobPositionOffset    = 0x7c;  // Camera+0x04 + Gob+0x78
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return false;
        void* clientApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!clientApp) return false;
        void* clientInternal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientApp) +
            kClientExoAppInternalOffset);
        if (!clientInternal) return false;
        void* module = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientInternal) +
            kClientInternalModuleOffset);
        if (!module) return false;
        void* camera = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(module) +
            kCSWCModuleCameraOffset);
        if (!camera) return false;
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(camera) +
            kCameraGobPositionOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetCameraYawRadians(float& outRad) {
    constexpr size_t kClientInternalModuleOffset = 0x18;
    constexpr size_t kCSWCModuleCameraOffset     = 0x40;
    constexpr size_t kCameraOrientationOffset    = 0x88;  // Camera+0x04 + Gob+0x84
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return false;
        void* clientApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!clientApp) return false;
        void* clientInternal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientApp) +
            kClientExoAppInternalOffset);
        if (!clientInternal) return false;
        void* module = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientInternal) +
            kClientInternalModuleOffset);
        if (!module) return false;
        void* camera = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(module) +
            kCSWCModuleCameraOffset);
        if (!camera) return false;
        // Quaternion layout w,x,y,z (w first) — verified against engine
        // Yaw() @0x4a9f40 and the struct header (struct Quaternion).
        const float* q = reinterpret_cast<float*>(
            reinterpret_cast<unsigned char*>(camera) + kCameraOrientationOffset);
        float qw = q[0], qx = q[1], qy = q[2], qz = q[3];
        float fwdX = 2.0f * (qx * qy - qz * qw);
        float fwdY = 1.0f - 2.0f * (qx * qx + qz * qz);
        if (fwdX == 0.0f && fwdY == 0.0f) return false;
        outRad = std::atan2(fwdY, fwdX);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace acc::engine
