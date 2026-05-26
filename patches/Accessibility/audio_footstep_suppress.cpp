#include "audio_footstep_suppress.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "engine_area.h"             // AreaObjectIterator, GameObjectKind, SegmentCrossesWalkmesh, WallEdge
#include "engine_offsets.h"          // Vector
#include "engine_player.h"           // GetPlayerPosition, GetClientLeader
#include "log.h"
#include "prism.h"
#include "spatial_change_detector.h" // GetCachedWalls
#include "strings.h"

namespace acc::audio::footstep_suppress {

namespace {

// Velocity-based stuck threshold, frame-rate independent. KOTOR's
// CSWGuiManager::Update fires per render frame, so per-tick displacement
// is fps-coupled — the design doc's 1cm/tick assumed a 30Hz tick that
// the engine doesn't actually have. We measure observed speed
// (displacement / elapsed milliseconds) instead.
//
// 0.3 m/s threshold:
//   * KOTOR walk speed is ~2 m/s, run ~5 m/s → both well above.
//   * Wall-sliding while engine-physics-stuck typically produces
//     <0.2 m/s effective forward velocity (sub-cm per frame at 60fps,
//     accumulated over the dt window).
//   * Standing still: 0 m/s, comfortably below.
//
// The comparison is done in m²/s² to avoid sqrt: speedSq < 0.09.
constexpr float kStuckSpeedMetersPerSec = 0.3f;
constexpr float kStuckSpeedSq =
    kStuckSpeedMetersPerSec * kStuckSpeedMetersPerSec;

// Minimum dt before we trust a speed sample. Engine ticks faster than
// a millisecond on idle frames (cache hit, no work), so very small dt
// can produce noisy speed estimates. 8ms ~= 120fps frame budget; below
// that we just preserve the previous stuck state.
constexpr uint64_t kMinDeltaMs = 8;

bool      g_have_last_sample = false;
Vector    g_last_pos          = {0.0f, 0.0f, 0.0f};
uint64_t  g_last_tick_ms      = 0;
bool      g_was_stuck         = false;

// ---------- Stuck-direction probe state ----------------------------------
//
// `g_last_leader_footstep_ms` is stamped by OnPlayFootstep whenever the
// engine ticks an animation footstep on the leader. Walking ⇔ this is
// fresh (<200ms old); standing still ⇔ this goes stale. Standing still
// must not arm the probe — silence-when-stuck only signals "input
// without progress", not "I chose to stop."
//
// `g_progress_checkpoint_{pos,ms}` is the position + timestamp of the
// current 2-second progress-evaluation window. Re-seeded whenever the
// player covers >= kAnnounceDisplacementThreshold during a window or
// when walking-vs-not-walking flips.
//
// `g_stuck_announced` latches after we speak so the probe only fires
// once per stuck episode (re-armed by meaningful displacement).
constexpr uint64_t kFootstepFreshnessMs           = 200;     // walk-anim alive
constexpr uint64_t kStuckWindowMs                 = 2000;    // probe gate
constexpr float    kAnnounceDisplacementMeters    = 0.5f;    // window threshold
constexpr float    kAnnounceDisplacementMetersSq  =
    kAnnounceDisplacementMeters * kAnnounceDisplacementMeters;

// 8-cardinal probe parameters. 1.5 m is roughly two character-lengths
// — enough to clear a follower's collision capsule plus a step of
// real walking space. 0.5 m body clearance covers KOTOR's creature
// capsules (~0.4 m radius observed) with a small margin for ones
// standing just off the probe axis.
constexpr float    kProbeDistanceMeters           = 1.5f;
constexpr float    kBodyClearanceMeters           = 0.5f;
constexpr float    kBodyClearanceMetersSq         =
    kBodyClearanceMeters * kBodyClearanceMeters;
constexpr float    kSelfPositionEpsilonSq         = 0.04f;   // 0.2 m
constexpr int      kMaxNearbyBodies               = 32;

uint64_t g_last_leader_footstep_ms = 0;
Vector   g_progress_checkpoint_pos = {0.0f, 0.0f, 0.0f};
uint64_t g_progress_checkpoint_ms  = 0;
bool     g_stuck_announced         = false;

// World-space unit vectors for the 8 cardinals (+X = East, +Y = North,
// per all observed KOTOR positions: see CuePlayer "Wall pos=" relative
// to player). Order: N, NE, E, SE, S, SW, W, NW.
constexpr float kSqrt2Over2 = 0.70710678f;
struct ProbeDir { float x, y; acc::strings::Id strId; const char* tag; };
const ProbeDir kProbeDirs[8] = {
    { 0.0f,         1.0f,         acc::strings::Id::DirNorth,     "N"  },
    { kSqrt2Over2,  kSqrt2Over2,  acc::strings::Id::DirNortheast, "NE" },
    { 1.0f,         0.0f,         acc::strings::Id::DirEast,      "E"  },
    { kSqrt2Over2, -kSqrt2Over2,  acc::strings::Id::DirSoutheast, "SE" },
    { 0.0f,        -1.0f,         acc::strings::Id::DirSouth,     "S"  },
    {-kSqrt2Over2, -kSqrt2Over2,  acc::strings::Id::DirSouthwest, "SW" },
    {-1.0f,         0.0f,         acc::strings::Id::DirWest,      "W"  },
    {-kSqrt2Over2,  kSqrt2Over2,  acc::strings::Id::DirNorthwest, "NW" },
};

void RunStuckProbe(const Vector& pos) {
    // Walls — borrowed pointer into the change-detector's per-area cache.
    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(walls, wallCount)) {
        walls = nullptr;
        wallCount = 0;
    }

    // Collect nearby blocker bodies (creatures + placeables) once. The
    // player creature is included in the area object list; skip via the
    // self-position epsilon — proper handle comparison would also work
    // but distance is simpler and equally reliable here.
    Vector bodies[kMaxNearbyBodies];
    int    bodyCount = 0;
    const float bodyMaxReach = kProbeDistanceMeters + kBodyClearanceMeters;
    const float bodyMaxReachSq = bodyMaxReach * bodyMaxReach;
    if (void* area = acc::engine::GetCurrentArea()) {
        acc::engine::AreaObjectIterator it(area);
        for (void* obj = it.Next(); obj; obj = it.Next()) {
            if (bodyCount >= kMaxNearbyBodies) break;
            const int kind = acc::engine::GetObjectKind(obj);
            if (kind != static_cast<int>(acc::engine::GameObjectKind::Creature) &&
                kind != static_cast<int>(acc::engine::GameObjectKind::Placeable)) {
                continue;
            }
            Vector p;
            if (!acc::engine::GetObjectPosition(obj, p)) continue;
            const float ddx = p.x - pos.x;
            const float ddy = p.y - pos.y;
            const float dsq = ddx * ddx + ddy * ddy;
            if (dsq < kSelfPositionEpsilonSq) continue;  // skip self
            if (dsq > bodyMaxReachSq) continue;          // out of probe reach
            bodies[bodyCount++] = p;
        }
    }

    bool        freeDir[8] = { false, false, false, false, false, false, false, false };
    const char* blockBy[8] = { "",    "",    "",    "",    "",    "",    "",    ""    };

    for (int i = 0; i < 8; ++i) {
        Vector b = pos;
        b.x += kProbeDirs[i].x * kProbeDistanceMeters;
        b.y += kProbeDirs[i].y * kProbeDistanceMeters;
        // Z stays at player z — XY-planar probe matches engine walkmesh
        // semantics + SegmentCrossesWalkmesh's planar test.

        Vector hit;
        if (walls && acc::engine::SegmentCrossesWalkmesh(
                walls, wallCount, pos, b, hit)) {
            blockBy[i] = "wall";
            continue;
        }

        bool bodyHit = false;
        const float sdx = b.x - pos.x;
        const float sdy = b.y - pos.y;
        const float lenSq = sdx * sdx + sdy * sdy;
        if (lenSq > 1e-6f) {
            for (int j = 0; j < bodyCount; ++j) {
                // Closest point on segment to body.
                float t = ((bodies[j].x - pos.x) * sdx +
                           (bodies[j].y - pos.y) * sdy) / lenSq;
                if (t < 0.0f) t = 0.0f;
                else if (t > 1.0f) t = 1.0f;
                const float cx = pos.x + sdx * t;
                const float cy = pos.y + sdy * t;
                const float ddx2 = bodies[j].x - cx;
                const float ddy2 = bodies[j].y - cy;
                if (ddx2 * ddx2 + ddy2 * ddy2 < kBodyClearanceMetersSq) {
                    bodyHit = true;
                    break;
                }
            }
        }
        if (bodyHit) {
            blockBy[i] = "body";
            continue;
        }
        freeDir[i] = true;
    }

    char buf[256];
    int  written = 0;
    int  freeCount = 0;
    for (int i = 0; i < 8; ++i) {
        if (!freeDir[i]) continue;
        const char* word = acc::strings::Get(kProbeDirs[i].strId);
        if (freeCount == 0) {
            written = snprintf(buf, sizeof(buf), "%s: %s",
                               acc::strings::Get(
                                   acc::strings::Id::StuckFreeDirsPrefix),
                               word);
        } else if (written > 0 && written < static_cast<int>(sizeof(buf))) {
            const int rem = static_cast<int>(sizeof(buf)) - written;
            const int add = snprintf(buf + written, rem, ", %s", word);
            if (add > 0) written += add;
        }
        ++freeCount;
    }
    if (freeCount == 0) {
        snprintf(buf, sizeof(buf), "%s",
                 acc::strings::Get(acc::strings::Id::StuckAllBlocked));
    }

    acclog::Write("StuckProbe",
        "fire pos=(%.2f,%.2f) walls=%d bodies=%d "
        "N=%d/%s NE=%d/%s E=%d/%s SE=%d/%s S=%d/%s SW=%d/%s W=%d/%s NW=%d/%s "
        "-> [%s]",
        pos.x, pos.y, wallCount, bodyCount,
        freeDir[0] ? 1 : 0, blockBy[0],
        freeDir[1] ? 1 : 0, blockBy[1],
        freeDir[2] ? 1 : 0, blockBy[2],
        freeDir[3] ? 1 : 0, blockBy[3],
        freeDir[4] ? 1 : 0, blockBy[4],
        freeDir[5] ? 1 : 0, blockBy[5],
        freeDir[6] ? 1 : 0, blockBy[6],
        freeDir[7] ? 1 : 0, blockBy[7],
        buf);

    prism::SpeakUrgent(buf);
}

// Called every Tick() after the per-frame stuck flag is updated. Gates on
// the leader's walk animation being live (PlayFootstep firing for the
// leader); standing still does not arm the probe.
void TickStuckAnnounce(const Vector& pos, uint64_t now_ms) {
    const bool walking =
        (g_last_leader_footstep_ms != 0) &&
        ((now_ms - g_last_leader_footstep_ms) < kFootstepFreshnessMs);
    if (!walking) {
        // Reset everything — fresh tracking when walking resumes.
        g_progress_checkpoint_ms = 0;
        g_stuck_announced = false;
        return;
    }

    if (g_progress_checkpoint_ms == 0) {
        g_progress_checkpoint_pos = pos;
        g_progress_checkpoint_ms  = now_ms;
        return;
    }

    if ((now_ms - g_progress_checkpoint_ms) < kStuckWindowMs) {
        return;
    }

    const float dx = pos.x - g_progress_checkpoint_pos.x;
    const float dy = pos.y - g_progress_checkpoint_pos.y;
    const float distSq = dx * dx + dy * dy;

    if (distSq >= kAnnounceDisplacementMetersSq) {
        // Made real progress — re-seed checkpoint, re-arm announce.
        g_progress_checkpoint_pos = pos;
        g_progress_checkpoint_ms  = now_ms;
        g_stuck_announced = false;
        return;
    }

    // Stuck for >=2s while animating walk. Announce once per episode;
    // leave the checkpoint in place so we don't re-evaluate every tick
    // (next re-arm happens via the displacement-progress branch above).
    if (!g_stuck_announced) {
        RunStuckProbe(pos);
        g_stuck_announced = true;
    }
}

}  // namespace

bool WasStuckLastTick() { return g_was_stuck; }

void NoteLeaderFootstep() {
    g_last_leader_footstep_ms = GetTickCount64();
}

void Tick() {
    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) {
        // No player loaded (main menu, area-load). Reset state so the
        // next observation seeds fresh rather than comparing against
        // stale data from a different area.
        g_have_last_sample = false;
        g_was_stuck = false;
        g_progress_checkpoint_ms = 0;
        g_stuck_announced = false;
        g_last_leader_footstep_ms = 0;
        return;
    }

    const uint64_t now_ms = GetTickCount64();

    if (!g_have_last_sample) {
        g_last_pos = pos;
        g_last_tick_ms = now_ms;
        g_have_last_sample = true;
        g_was_stuck = false;
        acclog::Write("FootstepSup", "Tick seed pos=(%.3f,%.3f,%.3f)",
                      pos.x, pos.y, pos.z);
        return;
    }

    const uint64_t dt_ms = now_ms - g_last_tick_ms;
    if (dt_ms < kMinDeltaMs) {
        // Too soon since last sample to trust the speed estimate.
        // Preserve previous stuck verdict until enough time elapses.
        return;
    }

    const float dx = pos.x - g_last_pos.x;
    const float dy = pos.y - g_last_pos.y;
    // Z deliberately excluded — vertical jitter on uneven walkmesh
    // shouldn't count as motion; "moving" is horizontal-plane only.
    const float distSq = dx * dx + dy * dy;
    const float dt_s = static_cast<float>(dt_ms) * 0.001f;
    const float speedSq = distSq / (dt_s * dt_s);
    const bool prev_stuck = g_was_stuck;
    g_was_stuck = (speedSq < kStuckSpeedSq);

    // Edge fires only on the stuck-flag transition; the framework reports
    // how many ticks the previous state held so full per-tick fidelity is
    // preserved without per-tick spam. Position/speed go in the line so
    // the value at transition time is recorded; held ticks comes from the
    // edge framework on the next flip. Per feedback_log_no_rate_limits.md.
    (void)prev_stuck;
    acclog::Edge("FootstepSup.stuck", g_was_stuck ? 1 : 0,
        "Tick pos=(%.3f,%.3f) d=(%.4f,%.4f) dt_ms=%llu speed=%.3f m/s stuck=%d",
        pos.x, pos.y, dx, dy,
        static_cast<unsigned long long>(dt_ms),
        sqrtf(speedSq),
        g_was_stuck ? 1 : 0);

    g_last_pos = pos;
    g_last_tick_ms = now_ms;

    TickStuckAnnounce(pos, now_ms);
}

}  // namespace acc::audio::footstep_suppress

// CSWCCreature::PlayFootstep — detour @0x0061a30c. Hooked AFTER both
// CExoString constructors (LAB sequence runs CExoString::CExoString twice
// in the prologue at 0x0061a2f8 and 0x0061a307). Cut covers the engine's
// own MOV EDI,[ESI+0x20] + CMP EDI,EBX (5 bytes, register-only).
//
// Suppression contract: handler returns 1 → framework wrapper jumps to the
// engine's natural early-return path at 0x0061a632 (LEA ECX,[ESP+0x34] →
// destructor cascade → SEH unwind → return). This is the SAME exit the
// engine takes when its own field6_0x20==0 check fails, so the destructor
// state is well-defined (both CExoStrings constructed, local_c flags set
// per the prologue path the engine just executed).
//
// Handler returns 0 → fall-through; wrapper runs the relocated cut bytes
// (re-establishing EDI=[ESI+0x20] and the CMP flags), then resumes the
// engine's flow at 0x0061a311 with the JZ that consumes the CMP result.
//
// Parameter: ESI holds `this` (set at function entry by `MOV ESI, ECX` at
// 0x0061a2f1). At our cut point, ESI is still valid (callee-saved across
// the constructor calls).
// Hook REPLACES the engine's own JZ at 0x0061a31a (skip_original_bytes=true),
// so the handler is responsible for BOTH the engine's natural early-out
// (field6_0x20==0) AND our stuck-suppression. Returning 1 mimics the JZ
// taken (consume → 0x0061a632 destructor cascade); returning 0 mimics the
// JZ not taken (resume at 0x0061a320, audio plays).
//
// The two-bug saga that led here is documented in hooks.toml above the hook
// entry — the short version: every "obvious" cut location either clobbered
// EFLAGS (breaking the engine's downstream JZ) or clobbered EAX (breaking
// the wrapper's TEST EAX,EAX consume-routing). Skipping the cut entirely
// dodges both.
extern "C" int __cdecl OnPlayFootstep(void* creature) {
    if (!creature) return 1;  // defensive: behave as engine's null-check would

    // Mimic the engine's own field6_0x20==0 early-out. We replaced its JZ
    // with this hook (skip_original_bytes), so we MUST take this branch
    // ourselves when the engine would have.
    uint32_t field20 = 0;
    __try {
        field20 = *reinterpret_cast<uint32_t*>(
            static_cast<unsigned char*>(creature) + 0x20);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;  // bad pointer — abort safely, audio skipped
    }
    if (field20 == 0) {
        // Engine's natural early-out — same outcome as vanilla. Don't log
        // these (would dominate the log; they're not "our" suppressions).
        return 1;
    }

    void* leader = acc::engine::GetClientLeader();
    const bool is_leader = (leader && creature == leader);
    const bool stuck = acc::audio::footstep_suppress::WasStuckLastTick();
    const int verdict = (is_leader && stuck) ? 1 : 0;

    if (is_leader) {
        // Stamp the leader-walking signal for TickStuckAnnounce. Engine
        // fires PlayFootstep only when the walk animation is actively
        // ticking; the stuck-direction probe gates on this freshness so
        // a player who chose to stop never gets the rescue announce.
        acc::audio::footstep_suppress::NoteLeaderFootstep();
    }

    acclog::Write("FootstepSup", "PlayFootstep this=%p leader=%p field20=0x%x "
        "is_leader=%d stuck=%d verdict=%d",
        creature, leader, field20,
        is_leader ? 1 : 0, stuck ? 1 : 0, verdict);

    return verdict;  // 1 = suppress (wrapper jumps to 0x0061a632)
}
