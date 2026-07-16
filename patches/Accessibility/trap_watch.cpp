// Trap detection watcher — see trap_watch.h for the design and
// docs/llm-docs/mine-trap-model.md for the engine model.

#include "trap_watch.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine_area.h"     // AreaObjectIterator, GetObjectKind, TrapDetectedByAnyOf, kTriggerIsTrapOffset, GetObjectName, GetObjectPosition, GetObjectHandle, GetCurrentArea
#include "engine_compass.h"  // ClockPosition
#include "engine_player.h"   // GetPlayerServerCreature, GetPartyMembers, GetPlayerPosition, GetPlayerYawDegrees
#include "log.h"
#include "prism.h"
#include "strfmt.h"
#include "strings.h"

namespace acc::trap_watch {

namespace {

// Scan cadence. Detection transitions are driven by the engine's own
// 100ms/3s UpdateMineCheck cadence, so 250ms adds at most a quarter
// second of latency; the proximity warning tolerates ~1m of drift at
// run speed, well inside the enter radius.
constexpr DWORD kScanIntervalMs = 250;

// Proximity warning hysteresis. Small on purpose: one warning close to
// the mine, no repetition while the player disarms it or deliberately
// steps over it; walking away past the exit radius re-arms so the way
// back through a minefield warns again.
constexpr float kMineWarnEnterM = 4.0f;
constexpr float kMineWarnExitM  = 8.0f;

// Fresh-detection entries (for the combat-line enrichment) expire after
// this window — if the engine feedback line never arrives (feedback
// disabled in the game options), the entry just ages out.
constexpr DWORD kFreshMineExpireMs = 4000;

struct TrackedTrap {
    uint32_t handle    = 0;
    bool     detected  = false;
    bool     warnArmed = true;
    bool     isMine    = false;   // trigger kind (ground mine)
    Vector   pos       = {0.0f, 0.0f, 0.0f};
};

constexpr int kMaxTracked = 96;
TrackedTrap g_tracked[kMaxTracked];
int         g_tracked_count = 0;

struct FreshMine {
    bool   valid = false;
    char   name[96] = {0};
    Vector pos = {0.0f, 0.0f, 0.0f};
    DWORD  tick = 0;
};
// Two slots suffice — multiple simultaneous detections happen when
// walking into a mine cluster; the combat funnel delivers their lines
// back-to-back within one window.
FreshMine g_fresh[2];

void*  g_area      = nullptr;
DWORD  g_last_scan = 0;

TrackedTrap* FindOrAddTracked(uint32_t handle) {
    for (int i = 0; i < g_tracked_count; ++i) {
        if (g_tracked[i].handle == handle) return &g_tracked[i];
    }
    if (g_tracked_count >= kMaxTracked) return nullptr;
    TrackedTrap& t = g_tracked[g_tracked_count++];
    t = TrackedTrap{};
    t.handle = handle;
    return &t;
}

void PushFresh(const char* name, const Vector& pos, DWORD now) {
    // Oldest slot loses.
    FreshMine* slot = &g_fresh[0];
    for (auto& f : g_fresh) {
        if (!f.valid) { slot = &f; break; }
        if (f.tick < slot->tick) slot = &f;
    }
    slot->valid = true;
    std::snprintf(slot->name, sizeof(slot->name), "%s", name ? name : "");
    slot->pos  = pos;
    slot->tick = now;
}

void ExpireFresh(DWORD now) {
    for (auto& f : g_fresh) {
        if (f.valid && (now - f.tick) > kFreshMineExpireMs) f.valid = false;
    }
}

// SEH-guarded is-mine read (CSWSTrigger.is_trap). False on fault.
bool TriggerIsTrap(void* trigger) {
    __try {
        return *reinterpret_cast<int*>(
                   reinterpret_cast<unsigned char*>(trigger) +
                   kTriggerIsTrapOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Localized object name with per-kind fallback (mines placed by scripts
// occasionally carry no authored name — GetObjectName would fall back to
// the raw tag, which is modder machinery, not speech).
void TrapSpokenName(void* obj, bool isMine, char* out, size_t size) {
    out[0] = '\0';
    if (!acc::engine::GetObjectName(obj, out, size) || !out[0] ||
        (isMine && std::strchr(out, '_') != nullptr)) {
        std::snprintf(out, size, "%s",
                      acc::strings::Get(acc::strings::Id::MineNoun));
    }
}

// "<label>, auf X Uhr, Y Meter" via the shared announce format; falls
// back to the bare label when player pos/yaw is degenerate.
std::string WithClock(const char* label, const Vector& target) {
    Vector p{};
    float  yaw = 0.0f;
    if (acc::engine::GetPlayerPosition(p) &&
        acc::engine::GetPlayerYawDegrees(yaw)) {
        int clock = acc::engine::ClockPosition(yaw, target.x - p.x,
                                               target.y - p.y);
        float dx = target.x - p.x, dy = target.y - p.y;
        int m = static_cast<int>(std::sqrt(dx * dx + dy * dy) + 0.5f);
        if (m < 1) m = 1;
        const char* fmt =
            acc::strings::Get(acc::strings::Id::FmtAnnounceWithClock);
        if (fmt && fmt[0]) return acc::strfmt::Format(fmt, label, clock, m);
    }
    return std::string(label);
}

}  // namespace

void ScanInternal(DWORD now);

void Tick() {
    DWORD now = GetTickCount();
    if ((now - g_last_scan) < kScanIntervalMs) return;
    ScanInternal(now);
}

// Forced rescan for the message-enrichment race: the engine appends the
// mine-detect feedback line in the same tick it updates the detected-by
// list, which can beat the next 250ms scan. RuleMineDetect calls this
// before giving up. Rate-limited so a combat message burst can't turn
// every line into an area scan.
void ScanNow() {
    DWORD now = GetTickCount();
    if ((now - g_last_scan) < 100) return;
    ScanInternal(now);
}

void ScanInternal(DWORD now) {
    g_last_scan = now;

    void* area = acc::engine::GetCurrentArea();
    if (area != g_area) {
        g_area = area;
        g_tracked_count = 0;
        for (auto& f : g_fresh) f.valid = false;
    }
    if (!area) return;
    ExpireFresh(now);

    // Party server ids: the PC plus active followers. The engine writes
    // all of them into a trap's detected-by list on a party detection, so
    // matching any is enough.
    uint32_t partyIds[12];
    int      partyCount = 0;
    void* pc = acc::engine::GetPlayerServerCreature();
    if (!pc) return;  // no world (menus / load) — nothing to scan
    partyIds[partyCount++] = acc::engine::GetObjectHandle(pc);
    partyCount += acc::engine::GetPartyMembers(
        partyIds + partyCount,
        static_cast<int>(sizeof(partyIds) / sizeof(partyIds[0])) - partyCount);

    Vector playerPos{};
    bool havePlayerPos = acc::engine::GetPlayerPosition(playerPos);

    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    while ((obj = iter.Next()) != nullptr) {
        int kind = acc::engine::GetObjectKind(obj);
        bool isMine = false;
        if (kind == static_cast<int>(acc::engine::GameObjectKind::Trigger)) {
            if (!TriggerIsTrap(obj)) continue;
            isMine = true;
        } else if (kind != static_cast<int>(acc::engine::GameObjectKind::Door) &&
                   kind != static_cast<int>(
                       acc::engine::GameObjectKind::Placeable)) {
            continue;
        }

        bool detected =
            acc::engine::TrapDetectedByAnyOf(obj, partyIds, partyCount);
        // Doors/placeables without any trap resolve to false forever and
        // cost one list read per scan — acceptable; tracking them would
        // cost the same.
        if (!detected && !isMine) continue;

        uint32_t handle = acc::engine::GetObjectHandle(obj);
        TrackedTrap* t = FindOrAddTracked(handle);
        if (!t) continue;
        t->isMine = isMine;
        acc::engine::GetObjectPosition(obj, t->pos);

        if (detected && !t->detected) {
            t->detected = true;
            char name[96];
            TrapSpokenName(obj, isMine, name, sizeof(name));
            if (isMine) {
                // Engine emits the feedback line; stash for RuleMineDetect
                // to enrich. No direct speech here.
                PushFresh(name, t->pos, now);
                acclog::Write("TrapWatch",
                              "mine detected: handle=%08x '%s' pos=(%.1f,%.1f)"
                              " — queued for message enrichment",
                              handle, name, t->pos.x, t->pos.y);
            } else {
                // No engine feedback line exists for trapped doors /
                // placeables — announce ourselves.
                const char* fmt =
                    acc::strings::Get(acc::strings::Id::FmtTrapDetected);
                std::string label = acc::strfmt::Format(
                    (fmt && fmt[0]) ? fmt : "Falle entdeckt: %s", name);
                std::string line = WithClock(label.c_str(), t->pos);
                prism::Speak(line.c_str(), /*interrupt=*/false);
                acclog::Write("TrapWatch",
                              "trap detected (kind=%d): handle=%08x -> [%s]",
                              kind, handle, line.c_str());
            }
        }

        // Proximity warning — detected ground mines only.
        if (t->detected && t->isMine && havePlayerPos) {
            float dx = playerPos.x - t->pos.x;
            float dy = playerPos.y - t->pos.y;
            float d2 = dx * dx + dy * dy;
            if (t->warnArmed && d2 < kMineWarnEnterM * kMineWarnEnterM) {
                t->warnArmed = false;
                char name[96];
                TrapSpokenName(obj, /*isMine=*/true, name, sizeof(name));
                std::string line = WithClock(name, t->pos);
                prism::SpeakUrgent(line.c_str());
                acclog::Write("TrapWatch",
                              "proximity warn: handle=%08x dist=%.1fm -> [%s]",
                              handle, std::sqrt(d2), line.c_str());
            } else if (!t->warnArmed &&
                       d2 > kMineWarnExitM * kMineWarnExitM) {
                t->warnArmed = true;
            }
        }
    }
}

bool PeekFreshMine(char* nameOut, size_t nameSize, Vector& posOut) {
    ExpireFresh(GetTickCount());
    // Newest valid entry wins.
    const FreshMine* best = nullptr;
    for (const auto& f : g_fresh) {
        if (f.valid && (!best || f.tick > best->tick)) best = &f;
    }
    if (!best) return false;
    std::snprintf(nameOut, nameSize, "%s", best->name);
    posOut = best->pos;
    return true;
}

void ConsumeFreshMine() {
    FreshMine* best = nullptr;
    for (auto& f : g_fresh) {
        if (f.valid && (!best || f.tick > best->tick)) best = &f;
    }
    if (best) best->valid = false;
}

}  // namespace acc::trap_watch
