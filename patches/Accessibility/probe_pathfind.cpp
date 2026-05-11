#include "probe_pathfind.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "user32.lib")

#include "engine_area.h"
#include "engine_offsets.h"
#include "engine_player.h"
#include "guidance_autowalk.h"
#include "hotkeys.h"
#include "log.h"

namespace acc::probe_pathfind {

namespace {

constexpr int kVK_F9 = 0x78;

// CSWSCreature.path_find_info: at offset +0x340 is a CPathfindInformation*
// POINTER (per swkotor.exe.h:9154). Our previous probe pass dumped 256 bytes
// at creature+0x340 thinking it was the struct itself; it isn't — that
// window is the pointer slot + the next ~256 bytes of OTHER CSWSCreature
// fields. The actual struct lives behind the dereference.
constexpr size_t kCreaturePathFindInfoPtrOffset = 0x340;

// CPathfindInformation is 0x278 bytes total per the swkotor.exe.h:9621
// definition (last named field at +0x274). We dump a bit beyond to catch
// any tail data.
constexpr size_t kPfiDumpLen = 0x280;

// CPathfindInformation field offsets — picked from swkotor.exe.h:9621. The
// `?`-suffixed names are Ghidra's guess-typed fields; we log them and let
// the data confirm or refute. We're especially interested in:
//   +0x8c path_count?   ulong    — plausible "remaining waypoint count"
//   +0x90 paths?        undefined4* — plausible "waypoint list pointer"
//   +0x60 start_point   Vector
//   +0x74 end_point     Vector
//   +0x2c creature_object_id ulong
constexpr size_t kPfiStartPointOffset       = 0x60;
constexpr size_t kPfiEndPointOffset         = 0x74;
constexpr size_t kPfiPathCountOffset        = 0x8c;
constexpr size_t kPfiPathsPtrOffset         = 0x90;
constexpr size_t kPfiCreatureObjIdOffset    = 0x2c;

// CSWSArea per-area nav graph — paired ulong count + pointer (NOT a
// CExoArrayList; see CSWSArea decomp at swkotor.exe.h:9123).
//
//   +0x238 path_points_count       ulong
//   +0x23c path_points             PathPoint*
//   +0x240 path_connections_count  ulong
//   +0x244 path_connections        ulong*
constexpr size_t kAreaPathPointsCountOffset      = 0x238;
constexpr size_t kAreaPathPointsPtrOffset        = 0x23c;
constexpr size_t kAreaPathConnectionsCountOffset = 0x240;
constexpr size_t kAreaPathConnectionsPtrOffset   = 0x244;

struct ProbeState {
    bool   active           = false;
    DWORD  dispatchTick     = 0;
    void*  creatureAtPress  = nullptr;
    void*  pfiAtPress       = nullptr;  // snapshot of the deref'd
                                        // CPathfindInformation*. If the
                                        // engine swaps it mid-cascade
                                        // we log it.
    bool   fired100         = false;
    bool   fired500         = false;
    bool   fired1500        = false;
    bool   fired3500        = false;
};
ProbeState g_state;

bool g_prevF9 = false;

bool IsForegroundOurs() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

// Read a uint32 at base+offset, SEH-guarded. Returns 0 + sets ok=false on
// fault.
uint32_t SafeReadU32(void* base, size_t offset, bool& ok) {
    ok = true;
    uint32_t v = 0;
    __try {
        v = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return v;
}

// Read a Vector at base+offset, SEH-guarded. Returns zero vector + sets
// ok=false on fault.
Vector SafeReadVector(void* base, size_t offset, bool& ok) {
    ok = true;
    Vector v{0, 0, 0};
    __try {
        v = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return v;
}

// SEH-wrapped read of one byte. Falls back to byte-by-byte copy on bulk
// fault. Returns number of bytes successfully read.
size_t SafeBulkRead(void* src, void* dst, size_t len) {
    __try {
        memcpy(dst, src, len);
        return len;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // bulk faulted — fall through to byte-by-byte
    }
    auto* s = static_cast<unsigned char*>(src);
    auto* d = static_cast<unsigned char*>(dst);
    for (size_t i = 0; i < len; ++i) {
        __try {
            d[i] = s[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return i;
        }
    }
    return len;
}

// Dump the CPathfindInformation struct (0x280 bytes) and decode the named
// fields. Tag prefixes the log lines so PRE / t+100ms / etc. are easy to
// diff in the log.
void DumpPathfindInformation(const char* tag, void* pfi) {
    if (!pfi) {
        acclog::Write(tag, "pfi=null — no struct to dump");
        return;
    }

    // Bulk dump of the struct body.
    unsigned char buf[kPfiDumpLen];
    size_t copied = SafeBulkRead(pfi, buf, kPfiDumpLen);
    char label[64];
    snprintf(label, sizeof(label), "CPathfindInformation @%p (%zu bytes)", pfi, copied);
    acclog::WriteHex(tag, label, buf, copied);

    // Named-field decode.
    bool ok = true;
    uint32_t creatureObjId = SafeReadU32(pfi, kPfiCreatureObjIdOffset, ok);
    Vector startPt = SafeReadVector(pfi, kPfiStartPointOffset, ok);
    Vector endPt   = SafeReadVector(pfi, kPfiEndPointOffset,   ok);
    uint32_t pathCount    = SafeReadU32(pfi, kPfiPathCountOffset, ok);
    uint32_t pathsPtr     = SafeReadU32(pfi, kPfiPathsPtrOffset,  ok);

    acclog::Write(tag, "pfi.creature_obj_id @+0x%x = 0x%08x",
                  (unsigned)kPfiCreatureObjIdOffset, creatureObjId);
    acclog::Write(tag, "pfi.start_point @+0x%x = (%.2f, %.2f, %.2f)",
                  (unsigned)kPfiStartPointOffset, startPt.x, startPt.y, startPt.z);
    acclog::Write(tag, "pfi.end_point @+0x%x = (%.2f, %.2f, %.2f)",
                  (unsigned)kPfiEndPointOffset, endPt.x, endPt.y, endPt.z);
    acclog::Write(tag, "pfi.path_count? @+0x%x = %u  pfi.paths? @+0x%x = 0x%08x",
                  (unsigned)kPfiPathCountOffset, pathCount,
                  (unsigned)kPfiPathsPtrOffset, pathsPtr);

    // If paths? looks like a plausible heap pointer AND path_count? looks
    // like a small count, dereference and dump the pointed memory. The
    // engine's waypoint list — if that's what these fields are — should
    // be a small array of Vectors (or Vector + flag entries).
    if (pathsPtr >= 0x00100000u && pathsPtr < 0x80000000u &&
        pathCount > 0 && pathCount < 256) {
        const size_t kMaxBytes = 256;
        size_t want = pathCount * 16;  // try 16-byte stride first
        if (want > kMaxBytes) want = kMaxBytes;

        unsigned char pbuf[kMaxBytes];
        size_t pcopied = SafeBulkRead(
            reinterpret_cast<void*>(static_cast<uintptr_t>(pathsPtr)),
            pbuf, want);

        char pLabel[80];
        snprintf(pLabel, sizeof(pLabel),
                 "*pfi.paths? @0x%08x (count=%u, %zu bytes)",
                 pathsPtr, pathCount, pcopied);
        acclog::WriteHex(tag, pLabel, pbuf, pcopied);

        // Try interpreting as Vector entries (12-byte stride) for the
        // first few.
        for (size_t i = 0; i + 12 <= pcopied && i < 4*16; i += 16) {
            float x = *reinterpret_cast<const float*>(pbuf + i);
            float y = *reinterpret_cast<const float*>(pbuf + i + 4);
            float z = *reinterpret_cast<const float*>(pbuf + i + 8);
            uint32_t tail = (i + 16 <= pcopied)
                          ? *reinterpret_cast<const uint32_t*>(pbuf + i + 12)
                          : 0;
            acclog::Write(tag, "  paths[%zu]: V=(%.2f, %.2f, %.2f) tail=0x%08x",
                          i / 16, x, y, z, tail);
        }
    } else {
        acclog::Write(tag, "  *pfi.paths? not dereferenceable "
                      "(ptr=0x%08x, count=%u) — skipping deref",
                      pathsPtr, pathCount);
    }

    // Triple-scan for any other CExoArrayList-shaped triples inside the
    // PFI struct. May surface the waypoint list at a different offset
    // than path_count?/paths?.
    acclog::Write(tag, "  PFI triple-scan: {ptr, size in [1..99], "
                  "alloc in [size..499]}");
    for (size_t i = 0; i + 12 <= copied; i += 4) {
        uint32_t ptr   = *reinterpret_cast<const uint32_t*>(buf + i);
        int32_t  size  = *reinterpret_cast<const int32_t *>(buf + i + 4);
        int32_t  alloc = *reinterpret_cast<const int32_t *>(buf + i + 8);
        if (ptr == 0) continue;
        if (size < 1 || size > 99) continue;
        if (alloc < size || alloc > 499) continue;
        if (ptr < 0x00100000u || ptr >= 0x80000000u) continue;
        acclog::Write(tag, "  triple @+0x%zx: ptr=0x%08x size=%d alloc=%d",
                      i, ptr, size, alloc);
    }
}

void DumpAreaNavGraph(const char* tag, void* area) {
    if (!area) {
        acclog::Write(tag, "area=null — skipping nav-graph dump");
        return;
    }
    bool ok = true;
    uint32_t pointsCount = SafeReadU32(area, kAreaPathPointsCountOffset,      ok);
    uint32_t pointsPtr   = SafeReadU32(area, kAreaPathPointsPtrOffset,        ok);
    uint32_t connsCount  = SafeReadU32(area, kAreaPathConnectionsCountOffset, ok);
    uint32_t connsPtr    = SafeReadU32(area, kAreaPathConnectionsPtrOffset,   ok);
    acclog::Write(tag, "area.path_points count=%u ptr=0x%08x | "
                  "area.path_connections count=%u ptr=0x%08x",
                  pointsCount, pointsPtr, connsCount, connsPtr);

    // Sample the first ~8 path points (assume 16-byte stride = Vector +
    // uint32, matches previous probe observation).
    if (pointsPtr >= 0x00100000u && pointsPtr < 0x80000000u && pointsCount > 0) {
        const size_t kMaxSample = 16 * 8;
        size_t want = (pointsCount * 16 < kMaxSample)
                    ? pointsCount * 16 : kMaxSample;
        unsigned char buf[kMaxSample];
        size_t copied = SafeBulkRead(
            reinterpret_cast<void*>(static_cast<uintptr_t>(pointsPtr)),
            buf, want);
        char label[64];
        snprintf(label, sizeof(label), "path_points sample (count=%u, %zu bytes)",
                 pointsCount, copied);
        acclog::WriteHex(tag, label, buf, copied);
    }

    // Sample first ~16 connection entries (engine uses ulong*, 4-byte
    // stride? or pairs?). We dump 64 bytes flat.
    if (connsPtr >= 0x00100000u && connsPtr < 0x80000000u && connsCount > 0) {
        const size_t kMaxSample = 64;
        size_t want = (connsCount * 4 < kMaxSample)
                    ? connsCount * 4 : kMaxSample;
        unsigned char buf[kMaxSample];
        size_t copied = SafeBulkRead(
            reinterpret_cast<void*>(static_cast<uintptr_t>(connsPtr)),
            buf, want);
        char label[64];
        snprintf(label, sizeof(label), "path_connections sample (count=%u, %zu bytes)",
                 connsCount, copied);
        acclog::WriteHex(tag, label, buf, copied);
    }
}

void* DerefPathfindInfo(void* creature) {
    if (!creature) return nullptr;
    bool ok = true;
    uint32_t p = SafeReadU32(creature, kCreaturePathFindInfoPtrOffset, ok);
    if (!ok || p == 0) return nullptr;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(p));
}

void DumpCheckpoint(const char* checkpointTag) {
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) {
        acclog::Write("Probe.PathFind", "%s creature=null — disarming",
                      checkpointTag);
        g_state.active = false;
        return;
    }
    if (creature != g_state.creatureAtPress) {
        acclog::Write("Probe.PathFind",
            "%s creature changed during cascade (was=%p now=%p)",
            checkpointTag, g_state.creatureAtPress, creature);
    }
    void* pfi = DerefPathfindInfo(creature);
    if (pfi != g_state.pfiAtPress) {
        acclog::Write("Probe.PathFind",
            "%s pfi pointer changed (was=%p now=%p)",
            checkpointTag, g_state.pfiAtPress, pfi);
    }
    char tag[64];
    snprintf(tag, sizeof(tag), "Probe.PathFind.%s", checkpointTag);
    DumpPathfindInformation(tag, pfi);
}

}  // namespace

void PollWin32() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::ProbePathfind)) return;

    Vector playerPos{0.0f, 0.0f, 0.0f};
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        acclog::Write("Probe.PathFind", "F9 ignored — no player loaded");
        return;
    }
    float yawDeg = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(yawDeg)) {
        acclog::Write("Probe.PathFind", "F9 ignored — no player yaw");
        return;
    }
    void* area = acc::engine::GetCurrentArea();
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) {
        acclog::Write("Probe.PathFind", "F9 ignored — no player creature");
        return;
    }
    void* pfi = DerefPathfindInfo(creature);

    acclog::Write("Probe.PathFind",
        "=== F9 PRESS === player=(%.2f, %.2f, %.2f) yaw=%.1f deg "
        "area=%p creature=%p pfi=%p",
        playerPos.x, playerPos.y, playerPos.z, yawDeg, area, creature, pfi);

    // --- Pre-dispatch dumps ---
    DumpAreaNavGraph("Probe.PathFind.PRE", area);
    DumpPathfindInformation("Probe.PathFind.PRE", pfi);

    // --- Dispatch WalkTo to a target 10m ahead along player heading ---
    constexpr float kProbeDistance = 10.0f;
    float yawRad = yawDeg * 3.14159265358979323846f / 180.0f;
    Vector target{
        playerPos.x + std::cos(yawRad) * kProbeDistance,
        playerPos.y + std::sin(yawRad) * kProbeDistance,
        playerPos.z};

    acclog::Write("Probe.PathFind",
        "dispatching WalkTo target=(%.2f, %.2f, %.2f) (%.1fm ahead)",
        target.x, target.y, target.z, kProbeDistance);

    bool dispatched = acc::guidance::WalkTo(target);
    acclog::Write("Probe.PathFind", "WalkTo returned %s — arming cascade",
                  dispatched ? "true" : "false");

    g_state.active = true;
    g_state.dispatchTick = GetTickCount();
    g_state.creatureAtPress = creature;
    g_state.pfiAtPress = pfi;
    g_state.fired100 = false;
    g_state.fired500 = false;
    g_state.fired1500 = false;
    g_state.fired3500 = false;
}

void Tick() {
    if (!g_state.active) return;
    DWORD elapsed = GetTickCount() - g_state.dispatchTick;

    if (!g_state.fired100 && elapsed >= 100) {
        g_state.fired100 = true;
        DumpCheckpoint("t+100ms");
    }
    if (!g_state.fired500 && elapsed >= 500) {
        g_state.fired500 = true;
        DumpCheckpoint("t+500ms");
    }
    if (!g_state.fired1500 && elapsed >= 1500) {
        g_state.fired1500 = true;
        DumpCheckpoint("t+1500ms");
    }
    if (!g_state.fired3500 && elapsed >= 3500) {
        g_state.fired3500 = true;
        DumpCheckpoint("t+3500ms");
        acclog::Write("Probe.PathFind", "=== cascade complete; disarming ===");
        g_state.active = false;
    }
}

}  // namespace acc::probe_pathfind
