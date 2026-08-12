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
#include "engine_offsets_select.h"

namespace acc::engine {

// Declared in engine_player_internal.h (with the chain-walk rationale) —
// no longer file-static because the party and input-lock TUs read it too.
void* GetPlayerServerObject() {
    void* exoApp = GetClientApp();
    if (!exoApp) return nullptr;
    __try {
        auto getCreature = reinterpret_cast<PFN_GetPlayerCreature>(
            kAddrGetPlayerCreature);
        void* clientCreature = getCreature(exoApp);
        if (!clientCreature) return nullptr;

        // KOTOR 1 keeps its tested field read. KOTOR 2's server_object field
        // position is unestablished, so there we call the engine's own
        // CSWCCreature::GetServerCreature — which resolves through a virtual
        // and is therefore immune to the client-object layout shift.
        if (acc::game::IsKotor2()) {
            using PFN_GetServerCreature = void* (__thiscall*)(void*);
            return reinterpret_cast<PFN_GetServerCreature>(
                kAddrGetServerCreature)(clientCreature);
        }

        void* serverObject = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientCreature) +
            kClientObjectServerObjectOffset);
        return serverObject;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* ClientToServerCreature(void* clientCreature) {
    if (!clientCreature) return nullptr;
    __try {
        // Same dual path as GetPlayerServerObject above: KOTOR 2's client
        // layout shift makes the field read unsafe there, so use the
        // engine's own virtual-dispatching resolver; KOTOR 1 keeps the
        // tested field read.
        if (acc::game::IsKotor2()) {
            using PFN_GetServerCreature = void* (__thiscall*)(void*);
            return reinterpret_cast<PFN_GetServerCreature>(
                kAddrGetServerCreature)(clientCreature);
        }
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientCreature) +
            kClientObjectServerObjectOffset);
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
    // K1: Camera+0x04 embedded Gob + Gob position +0x78. K2 moved the Gob
    // position to +0xa4 (witnessed in Gob::GetPosition 0x00459710, which
    // returns this+0xa4; Camera::GetPosition 0x0047EF10 reads camera+0xa8,
    // confirming the Gob still sits at Camera+0x4).
    const size_t kCameraGobPositionOffset    = acc::off::Pick(0x7c, 0xa8);
    void* camera = GetCamera();
    if (!camera) return false;
    __try {
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(camera) +
            kCameraGobPositionOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetCameraYawRadians(float& outRad) {
    // K1: Camera+0x04 + Gob orientation +0x84. K2: Gob quaternion moved to
    // +0xb0 (Gob::GetOrientation 0x00459740 copies 16 bytes from this+0xb0;
    // Camera::GetOrientation 0x0047EF40 reads camera+0xb4). Same base-engine
    // Quaternion struct on both games, so the w,x,y,z layout below carries.
    const size_t kCameraOrientationOffset    = acc::off::Pick(0x88, 0xb4);
    void* camera = GetCamera();
    if (!camera) return false;
    __try {
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
