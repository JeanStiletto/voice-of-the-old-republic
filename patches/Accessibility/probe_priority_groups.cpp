#include "probe_priority_groups.h"

#include <windows.h>

#include <cstdint>

#include "audio_bus.h"   // kAddrCExoSoundPtr
#include "log.h"

namespace acc::probe::priority_groups {

namespace {

// CExoSound facade layout (XML type DB, 2026-05-14): the struct is
// ONLY 4 bytes wide — no vtable — and `internal` lives at offset 0.
// This differs from CClientExoApp (8 bytes, vtable@0, internal@4).
// Reading from +0x4 walks off the end and faults — exactly what the
// first probe attempt did before the layout was checked.
constexpr size_t kCExoSoundInternalOffset       = 0x0;
// CExoSoundInternal layout (XML type DB, 2026-05-14):
//   +0x4c   CPriorityGroup* priority_groups   (heap-allocated array)
constexpr size_t kSoundInternalPriorityGroupsOff = 0x4c;

// CPriorityGroup layout (XML, size 0x18):
//   +0x00  ulong  interrupt
//   +0x04  byte   max_player
//   +0x05  byte   (unknown)
//   +0x06  byte   priority
//   +0x07  byte   volume
//   +0x08  float  min_volume_dist
//   +0x0C  float  max_volume_distance
//   +0x10  float  playback_variance
//   +0x14  ushort fade_time
//   +0x16  ushort (padding)
constexpr size_t kPriorityGroupStride           = 0x18;
constexpr size_t kPriorityGroupMaxPlayerOff     = 0x04;
constexpr size_t kPriorityGroupPriorityOff      = 0x06;
constexpr size_t kPriorityGroupVolumeOff        = 0x07;
constexpr size_t kPriorityGroupMinDistOff       = 0x08;
constexpr size_t kPriorityGroupMaxDistOff       = 0x0C;
constexpr size_t kPriorityGroupVarianceOff      = 0x10;
constexpr size_t kPriorityGroupFadeTimeOff      = 0x14;

// We don't know the actual count of entries. Empirical caller values
// observed in engine code top out at 0x18 (24). Walk a wide range
// (0..63) so we catch the upper end too; bail when we see an entry
// that's clearly garbage (volume > 127 = invalid byte AND priority >
// 127 = invalid).
constexpr int kMaxEntries = 64;

bool g_dumped = false;
DWORD g_firstObservedAt = 0;

void* GetSoundInternal() {
    __try {
        void* exoSound = *reinterpret_cast<void**>(kAddrCExoSoundPtr);
        if (!exoSound) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(exoSound) +
            kCExoSoundInternalOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void DumpOnce() {
    void* internal = GetSoundInternal();
    if (!internal) return;
    void* table = nullptr;
    __try {
        table = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) +
            kSoundInternalPriorityGroupsOff);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Probe.PriorityGroups", "FAULT reading table pointer");
        return;
    }
    if (!table) {
        acclog::Write("Probe.PriorityGroups",
                      "table pointer is null (sound subsystem not ready?)");
        return;
    }

    acclog::Write("Probe.PriorityGroups",
                  "table @%p — dumping up to %d entries",
                  table, kMaxEntries);

    int validCount = 0;
    int garbageRun = 0;
    for (int i = 0; i < kMaxEntries; ++i) {
        auto* entry = reinterpret_cast<unsigned char*>(table) +
                      static_cast<size_t>(i) * kPriorityGroupStride;
        unsigned char vol = 0, prio = 0, maxPlayer = 0;
        float minDist = 0.0f, maxDist = 0.0f, variance = 0.0f;
        unsigned short fadeMs = 0;
        bool faulted = false;
        __try {
            vol       = *(entry + kPriorityGroupVolumeOff);
            prio      = *(entry + kPriorityGroupPriorityOff);
            maxPlayer = *(entry + kPriorityGroupMaxPlayerOff);
            minDist   = *reinterpret_cast<float*>(
                entry + kPriorityGroupMinDistOff);
            maxDist   = *reinterpret_cast<float*>(
                entry + kPriorityGroupMaxDistOff);
            variance  = *reinterpret_cast<float*>(
                entry + kPriorityGroupVarianceOff);
            fadeMs    = *reinterpret_cast<unsigned short*>(
                entry + kPriorityGroupFadeTimeOff);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            faulted = true;
        }
        if (faulted) {
            acclog::Write("Probe.PriorityGroups",
                          "  [%02d] FAULT — stopping walk", i);
            break;
        }
        // Heuristic: bytes > 127 in BOTH vol and priority strongly suggest
        // we walked off the end into uninitialized memory. Tolerate a few
        // garbage entries (some priority groups might legitimately have
        // odd values) but bail after a run of clearly-invalid rows.
        bool looksValid = (vol <= 127) && (prio <= 127);
        if (looksValid) {
            ++validCount;
            garbageRun = 0;
            acclog::Write("Probe.PriorityGroups",
                          "  [%02d] vol=%3u priority=%3u max_player=%3u "
                          "min_dist=%.2f max_dist=%.2f variance=%.3f "
                          "fade_ms=%u",
                          i, (unsigned)vol, (unsigned)prio,
                          (unsigned)maxPlayer, minDist, maxDist, variance,
                          (unsigned)fadeMs);
        } else {
            ++garbageRun;
            acclog::Write("Probe.PriorityGroups",
                          "  [%02d] vol=%u priority=%u (suspect — past array end?)",
                          i, (unsigned)vol, (unsigned)prio);
            if (garbageRun >= 4) {
                acclog::Write("Probe.PriorityGroups",
                              "  ...stopping after %d consecutive invalid rows",
                              garbageRun);
                break;
            }
        }
    }
    acclog::Write("Probe.PriorityGroups",
                  "dump complete — %d valid entries", validCount);
    g_dumped = true;
}

}  // namespace

void Tick() {
    if (g_dumped) return;
    DWORD now = GetTickCount();
    if (g_firstObservedAt == 0) {
        g_firstObservedAt = now;
        return;  // wait one tick so audio subsystem has settled
    }
    // Hold off ~2s after first observation so the sound engine is
    // fully initialised before we read its tables. Same convention
    // other probes use to avoid racing engine init.
    if (now - g_firstObservedAt < 2000) return;
    DumpOnce();
}

}  // namespace acc::probe::priority_groups
