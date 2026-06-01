#include "engine_player.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "engine_area.h"   // GetObjectHandle / GetObjectDisplayNameByHandle /
                           // kCreatureStatsPtrOffset etc. — used by GetActiveLeaderName
#include "engine_reads.h"  // ReadCExoString, ExtractTextOrStrRef
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

    // Resolve the *currently controlled* leader's name. Tab cycles which
    // party member the engine considers leader; GetClientLeader walks
    // CClientExoApp::GetPlayerCreature which the engine re-wires on each
    // Tab to the new leader (confirmed live via the DiagSelect Tab probe).
    //
    // Read first_name directly from CSWSCreatureStats via a pure memory
    // path (ExtractTextOrStrRef — inline CExoString, falling back to TLK
    // strref). We intentionally do NOT route through the engine's
    // CClientExoApp::GetObjectName accessor: that accessor writes through
    // a stack CExoString, and on the PC handle during the chargen→world
    // transient it overruns the caller's stack frame and trips the /GS
    // canary → uncatchable __fastfail (bisected 2026-05-19). The
    // direct-read path here doesn't invoke the engine accessor at all and
    // is safe in every state.
    //
    // The PC's stats.first_name is empty in vanilla saves (chargen writes
    // the chosen name to CClientExoAppInternal::player_character_name
    // instead — see project_pc_name_lives_in_client_exoapp). So when the
    // direct-read path yields an empty string, the leader is the PC and
    // we fall back to GetPlayerCharacterName.
    void* clientLeader = GetClientLeader();
    void* serverCreature = nullptr;
    void* stats = nullptr;
    uint32_t leaderHandle = 0;
    if (clientLeader) {
        __try {
            serverCreature = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(clientLeader) +
                kClientObjectServerObjectOffset);
            if (serverCreature) {
                stats = *reinterpret_cast<void**>(
                    reinterpret_cast<unsigned char*>(serverCreature) +
                    kCreatureStatsPtrOffset);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            serverCreature = nullptr;
            stats = nullptr;
        }
        leaderHandle = GetObjectHandle(clientLeader);
    }

    // Path 1: engine's universal display-name accessor on the leader's
    // handle. This is the same accessor sighted UI uses; it gives
    // localized names for companions (Trask, Carth, ...) and the PC's
    // chargen name where appropriate. The crash window we hit on
    // 2026-05-19 was the chargen→world *transient* (PC handle not yet
    // fully registered) — the Tab caller already gates that with
    // GetPlayerPosition.
    if (leaderHandle != 0u &&
        GetObjectDisplayNameByHandle(leaderHandle, outBuf, bufSize) &&
        outBuf[0] != '\0') {
        // Trace, not Write: this accessor is called dozens of times per
        // tick and the resolved line is identical every call (one distinct
        // value across a whole session). Trace dedups consecutive identical
        // content and emits a "(repeated Nx more)" summary on change, so the
        // log keeps the first line + every leader change + the repeat count
        // with none of the per-tick spam. (Was Write — 58% of one tester
        // session's 42k-line log; the volume served no diagnostic purpose
        // and each line cost an OutputDebugStringA + fflush on the game tick.)
        acclog::Trace("PartyLeader",
                      "leader=handle — client=%p server=%p stats=%p "
                      "handle=0x%08x name=[%s]",
                      clientLeader, serverCreature, stats,
                      leaderHandle, outBuf);
        return true;
    }

    // Path 2: direct stats first_name read. Bypasses the engine accessor
    // entirely (pure memory read). Companions usually populate this; the
    // PC's stats slot is empty in vanilla saves.
    outBuf[0] = '\0';
    bool statsReadOk = false;
    if (stats) {
        statsReadOk = ExtractTextOrStrRef(
            stats,
            kCreatureStatsFirstNameOffset,
            kCreatureStatsFirstNameOffset + 4,
            outBuf, bufSize);
        if (statsReadOk && outBuf[0] != '\0') {
            acclog::Trace("PartyLeader",
                          "leader=stats — client=%p server=%p stats=%p "
                          "name=[%s]",
                          clientLeader, serverCreature, stats, outBuf);
            return true;
        }
    }

    // Path 3: PC chargen-name slot in CClientExoAppInternal — last resort
    // when the leader is the PC and the engine accessor + stats are both
    // empty (the canonical case for a freshly created PC).
    outBuf[0] = '\0';
    bool pcOk = GetPlayerCharacterName(outBuf, bufSize);

    // Diagnostic: also dump the raw 8 bytes at stats+0x14 (CExoString
    // c_string + length) so we can see if first_name is a strref-only
    // slot we should be resolving through TLK.
    const char* firstName_cstr = nullptr;
    uint32_t    firstName_len  = 0xFFFFFFFFu;
    uint32_t    firstName_strref = 0xFFFFFFFFu;
    if (stats) {
        __try {
            auto* p = reinterpret_cast<unsigned char*>(stats) +
                      kCreatureStatsFirstNameOffset;
            firstName_cstr   = *reinterpret_cast<const char**>(p);
            firstName_len    = *reinterpret_cast<uint32_t*>(p + 4);
            firstName_strref = *reinterpret_cast<uint32_t*>(p + 8);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    acclog::Trace("PartyLeader",
                  "all paths empty — client=%p server=%p stats=%p "
                  "handle=0x%08x first_name(cstr=%p len=%u strref=0x%x) "
                  "stats_read_ok=%d pcOk=%d name=[%s]",
                  clientLeader, serverCreature, stats,
                  leaderHandle, firstName_cstr, firstName_len,
                  firstName_strref, statsReadOk ? 1 : 0, pcOk ? 1 : 0,
                  outBuf);
    if (pcOk && outBuf[0] != '\0') return true;
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

void* GetServerPartyTable() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* serverApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerServerOffsetPlayer);
        if (!serverApp) return nullptr;
        // CServerExoApp facade → CServerExoAppInternal at +0x4 (mirrors
        // the CClientExoApp / *Internal split). The party_table is
        // embedded inside the internal at +0x1b770.
        void* serverInternal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverApp) +
            kServerExoAppInternalOffset);
        if (!serverInternal) return nullptr;
        return reinterpret_cast<unsigned char*>(serverInternal) +
               kServerInternalPartyTableOffset;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

namespace {

typedef int  (__thiscall* PFN_PartyTableGetIsNPCAvailable)(void*, int);
typedef char (__thiscall* PFN_PartyTableGetNPCSelectability)(void*, int);
typedef int  (__thiscall* PFN_PartyTableGetNPCObject)(void*, int, int, int);

}  // namespace

bool PartyTableIsNPCAvailable(int npcSlot) {
    if (npcSlot < 0 || npcSlot >= kPartyRosterSlotCount) return false;
    void* table = GetServerPartyTable();
    if (!table) return false;
    __try {
        auto fn = reinterpret_cast<PFN_PartyTableGetIsNPCAvailable>(
            kAddrCSWPartyTableGetIsNPCAvailable);
        return fn(table, npcSlot) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PartyTableIsNPCSelectable(int npcSlot) {
    if (npcSlot < 0 || npcSlot >= kPartyRosterSlotCount) return false;
    void* table = GetServerPartyTable();
    if (!table) return false;
    __try {
        auto fn = reinterpret_cast<PFN_PartyTableGetNPCSelectability>(
            kAddrCSWPartyTableGetNPCSelectability);
        return fn(table, npcSlot) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Roster name fallback. KOTOR 1's NPC slot index is fixed and these are
// proper nouns ("Bastila", "Carth Onasi", …) that don't change between
// English and German installs, so a hardcoded table is the simplest
// reliable path when GetNPCObject can't resolve a live creature (i.e.
// the companion is recruited but not in the current module — the open-
// world PartySelection case). Order matches the engine's roster index:
//   0 Bastila, 1 Canderous, 2 Carth, 3 HK-47, 4 Jolee, 5 Juhani,
//   6 Mission, 7 T3-M4, 8 Zaalbar.
static const char* const kCompanionNamesBySlot[kPartyRosterSlotCount] = {
    "Bastila Shan",
    "Canderous Ordo",
    "Carth Onasi",
    "HK-47",
    "Jolee Bindo",
    "Juhani",
    "Mission Vao",
    "T3-M4",
    "Zaalbar",
};

bool GetPartyNpcNameForSlot(int npcSlot, char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    if (npcSlot < 0 || npcSlot >= kPartyRosterSlotCount) return false;
    void* table = GetServerPartyTable();
    if (table) {
        uint32_t handle = 0;
        __try {
            auto fn = reinterpret_cast<PFN_PartyTableGetNPCObject>(
                kAddrCSWPartyTableGetNPCObject);
            // OnPanelAdded calls GetNPCObject(slot, 0, 1) first; if that's
            // 0 (creature not in the active module) it tries (slot, 1, 1).
            // Mirror that fallback so a name resolves even when the engine
            // had to fall through to the second-instance pool.
            int id0 = fn(table, npcSlot, 0, 1);
            if (id0 != 0) {
                handle = static_cast<uint32_t>(id0);
            } else {
                int id1 = fn(table, npcSlot, 1, 1);
                if (id1 != 0) handle = static_cast<uint32_t>(id1);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            handle = 0;
        }
        if (handle != 0 &&
            GetObjectDisplayNameByHandle(handle, outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
        outBuf[0] = '\0';
    }
    // Engine path didn't resolve — most often because the companion is on
    // the roster but not in the current module (open-world PartySelection
    // screen, away from base). Fall back to the fixed-roster table.
    const char* fixed = kCompanionNamesBySlot[npcSlot];
    if (!fixed || !fixed[0]) return false;
    size_t nlen = strlen(fixed);
    if (nlen + 1 > bufSize) return false;
    memcpy(outBuf, fixed, nlen + 1);
    return true;
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
