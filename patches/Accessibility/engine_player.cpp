#include "engine_player.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "engine_area.h"   // GetObjectName — used by GetActiveLeaderName
                           // to read first_name+tag from the server creature
#include "engine_reads.h"  // ReadCExoString
#include "log.h"           // acclog::Write — diagnostics on the
                           // SetPlayerInputEnabled toggle / auto-restore tick

namespace acc::engine {

namespace {

typedef void* (__thiscall* PFN_GetPlayerCreature)(void* this_);
typedef void* (__thiscall* PFN_CSWSObjectGetArea)(void* this_);
typedef void* (__thiscall* PFN_GetPlayerCharacterName)(void* this_);

// Walk *kAddrAppManagerPtr → CClientExoApp → GetPlayerCreature() →
// server_object (+0xf8) and return the CSWSObject*. nullptr at any failure
// point along the chain or on any raised exception.
//
// Centralising the chain walk here means the per-field readers below each
// pay one SEH frame instead of three; the cost is one extra function call
// per read which is negligible at the rates these are invoked from
// per-tick consumers.
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

}  // namespace

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

void* GetPlayerServerCreature() {
    return GetPlayerServerObject();
}

void* GetClientLeader() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return nullptr;
        auto fn = reinterpret_cast<PFN_GetPlayerCreature>(
            kAddrGetPlayerCreature);
        return fn(exoApp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool GetActiveLeaderName(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';

    // Try the server creature's first_name + tag first (companions only —
    // Trask/Carth/etc. have these populated). Same path DiagSelect's Tab
    // probe uses; we lean on engine_area::GetObjectName for the read.
    void* server = GetPlayerServerObject();
    if (server) {
        GetObjectName(server, outBuf, bufSize);
        if (outBuf[0] != '\0') return true;
    }

    // Empty name → the PC chargen creature, which carries its name on
    // CClientExoApp instead of the creature stats. Same fallback chain
    // DiagSelect uses.
    return GetPlayerCharacterName(outBuf, bufSize);
}

bool GetPlayerCharacterName(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return false;
        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return false;
        auto fn = reinterpret_cast<PFN_GetPlayerCharacterName>(
            kAddrCClientExoAppGetPlayerCharacterName);
        void* exoStr = fn(exoApp);
        if (!exoStr) return false;
        // CExoString layout (c_string @+0, length @+4) — same shape every
        // other reader hits via ReadCExoString.
        return ReadCExoString(exoStr, /*offset=*/0, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

namespace {

// Auto-restore session. Set when SetPlayerInputEnabled(false) succeeds;
// TickPlayerInputRestore flips back to enabled=true once the deadline
// passes. 3-second window matches the autowalk progress watchdog's
// t+3s disengage so log lines and lifecycle stay aligned.
constexpr DWORD kAutoRestoreMs = 3000;
bool  g_disableActive    = false;
DWORD g_disableExpiresAt = 0;

typedef void (__thiscall* PFN_CSWPlayerControlSetEnabled)(void* this_, int enabled);

// Walk *kAddrAppManagerPtr → CClientExoApp → CClientExoAppInternal →
// CSWPlayerControl. Distinct from GetPlayerServerObject's chain — different
// destination. Returns nullptr on any failure.
void* GetPlayerControl() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;

        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return nullptr;

        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(exoApp) +
            kClientExoAppInternalOffset);
        if (!internal) return nullptr;

        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) +
            kClientAppPlayerControlOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

bool SetPlayerInputEnabled(bool enabled) {
    void* playerControl = GetPlayerControl();
    if (!playerControl) return false;

    __try {
        auto fn = reinterpret_cast<PFN_CSWPlayerControlSetEnabled>(
            kAddrCSWPlayerControlSetEnabled);
        fn(playerControl, enabled ? 1 : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    bool wasActive = g_disableActive;
    if (enabled) {
        g_disableActive = false;
        g_disableExpiresAt = 0;
    } else {
        g_disableActive = true;
        g_disableExpiresAt = GetTickCount() + kAutoRestoreMs;
    }
    acclog::Write(
        "PlayerInput: SetEnabled(%s) — was disabled=%d, now disabled=%d, "
        "expires=%lu",
        enabled ? "true" : "false", wasActive ? 1 : 0,
        g_disableActive ? 1 : 0,
        static_cast<unsigned long>(g_disableExpiresAt));
    return true;
}

void TickPlayerInputRestore() {
    if (!g_disableActive) return;
    if (GetTickCount() < g_disableExpiresAt) return;
    acclog::Write(
        "PlayerInput: TickPlayerInputRestore — auto-restoring (now=%lu, "
        "expired_at=%lu)",
        static_cast<unsigned long>(GetTickCount()),
        static_cast<unsigned long>(g_disableExpiresAt));
    SetPlayerInputEnabled(true);  // flips g_disableActive false on success
}

}  // namespace acc::engine
