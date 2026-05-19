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

    // Try the CClientExoApp accessor FIRST. That's the PC's authoritative
    // name slot (per memory project_pc_name_lives_in_client_exoapp) and it
    // covers the only solo-leader scenario the engine has — every save and
    // every fresh chargen ends up with a PC whose name lives there.
    //
    // We intentionally do NOT fall through to GetObjectName on the player
    // server creature. GetObjectName routes through
    // GetObjectDisplayNameByHandle → CClientExoApp::GetObjectName, which
    // writes through a stack CExoString. During the chargen→world transient
    // (player creature spawned with empty first_name + tag, client handle
    // not yet fully registered), that engine accessor takes a path that
    // overruns the caller's stack frame and trips the /GS canary →
    // __fastfail (uncatchable by SEH). Reliably reproduced on every
    // chargen→world today; bisected to this code path on 2026-05-19.
    //
    // TODO: when adding companion-leader support, fetch the actual active
    // leader (not GetPlayerServerObject, which is always the PC), and for
    // a companion call GetObjectName on THEIR server creature — companions
    // have populated first_name/tag and the unsafe engine accessor never
    // gets reached.
    if (GetPlayerCharacterName(outBuf, bufSize) && outBuf[0] != '\0') {
        return true;
    }
    return false;
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

bool SetPlayerInputEnabled(bool enabled, bool armAutoRestore) {
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
        // Re-enable always clears the timer regardless of the flag.
        g_disableActive = false;
        g_disableExpiresAt = 0;
    } else if (armAutoRestore) {
        g_disableActive = true;
        g_disableExpiresAt = GetTickCount() + kAutoRestoreMs;
    } else {
        // Sustained disable — caller manages the lifecycle (view mode).
        g_disableActive = false;
        g_disableExpiresAt = 0;
    }
    acclog::Write("PlayerInput", "SetEnabled(%s, armAutoRestore=%d) — was disabled=%d, "
        "now disabled=%d, expires=%lu",
        enabled ? "true" : "false", armAutoRestore ? 1 : 0,
        wasActive ? 1 : 0, g_disableActive ? 1 : 0,
        static_cast<unsigned long>(g_disableExpiresAt));
    return true;
}

int GetPartyMembers(uint32_t* outHandles, int maxCount) {
    if (!outHandles || maxCount <= 0) return 0;
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return 0;
        void* serverApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerServerOffsetPlayer);
        if (!serverApp) return 0;
        auto* partyTable = reinterpret_cast<unsigned char*>(serverApp) +
                           kServerExoAppPartyTableOffset;
        uint32_t numMembers = *reinterpret_cast<uint32_t*>(
            partyTable + kPartyTableNumMembersOffset);
        if (numMembers == 0 ||
            numMembers > static_cast<uint32_t>(kPartyTableMaxMembers)) {
            return 0;
        }
        int take = static_cast<int>(numMembers);
        if (take > maxCount) take = maxCount;
        auto* ids = reinterpret_cast<int32_t*>(
            partyTable + kPartyTableMemberIdsOffset);
        for (int i = 0; i < take; ++i) {
            outHandles[i] = static_cast<uint32_t>(ids[i]);
        }
        return take;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void TickPlayerInputRestore() {
    if (!g_disableActive) return;
    if (GetTickCount() < g_disableExpiresAt) return;
    acclog::Write("PlayerInput", "TickPlayerInputRestore — auto-restoring (now=%lu, "
        "expired_at=%lu)",
        static_cast<unsigned long>(GetTickCount()),
        static_cast<unsigned long>(g_disableExpiresAt));
    SetPlayerInputEnabled(true);  // flips g_disableActive false on success
}

}  // namespace acc::engine
