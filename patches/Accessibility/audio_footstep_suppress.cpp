#include "audio_footstep_suppress.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "engine_area.h"
#include "engine_offsets.h"
#include "engine_player.h"
#include "log.h"
#include "prism.h"
#include "spatial_change_detector.h"
#include "strings.h"

namespace acc::audio::footstep_suppress {

namespace {

// Velocity-based threshold, frame-rate independent. 0.3 m/s sits safely
// below walk (2 m/s) and run (5 m/s) but above wall-slide noise.
constexpr float kStuckSpeedMetersPerSec = 0.3f;
constexpr float kStuckSpeedSq =
    kStuckSpeedMetersPerSec * kStuckSpeedMetersPerSec;

// Below this dt the speed sample is noisy; preserve previous state.
constexpr uint64_t kMinDeltaMs = 8;

bool      g_have_last_sample = false;
Vector    g_last_pos          = {0.0f, 0.0f, 0.0f};
uint64_t  g_last_tick_ms      = 0;
bool      g_was_stuck         = false;

// Stuck-direction probe gating: leader-footstep freshness ⇒ walking;
// 0.5 m over 2 s window ⇒ no progress despite walk-anim. Probe latches
// once per episode; re-arms on meaningful displacement.
constexpr uint64_t kFootstepFreshnessMs           = 200;
constexpr uint64_t kStuckWindowMs                 = 2000;
constexpr float    kAnnounceDisplacementMeters    = 0.5f;
constexpr float    kAnnounceDisplacementMetersSq  =
    kAnnounceDisplacementMeters * kAnnounceDisplacementMeters;

// 1.5 m probe ≈ 2 character lengths; 0.5 m body clearance covers
// creature capsules (~0.4 m radius) plus margin.
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

// ---------- Circling-detection state ------------------------------------
//
// Existing TickStuckAnnounce gates on "displaced < 0.5m over 2s" — fires
// when the player is wedged motionless. In the 2026-05-27 cantina
// reproduction the player is moving ~3m per rotation pivot, easily
// exceeding the 0.5m threshold, so the checkpoint resets on every
// window expiration and the stuck-announce never fires — even though
// the user is unmistakably going in circles inside a 6×9m area.
//
// Circling detector measures path length (sum of per-tick |delta|)
// vs net displacement (|end - start|). High ratio = lots of motion,
// little net progress = oscillating in obstacles.
constexpr uint64_t kCirclingWindowMs       = 4000;   // 4s window
constexpr float    kCirclingMinPathMeters  = 4.0f;   // need real motion first
                                                     // (4m at 1m/s ⇒ walking)
constexpr float    kCirclingMaxNetMeters   = 2.5f;   // and went nowhere
constexpr float    kCirclingPathRatio      = 2.5f;   // path/net >= ratio

Vector   g_circling_anchor_pos = {0.0f, 0.0f, 0.0f};
uint64_t g_circling_anchor_ms  = 0;
Vector   g_circling_prev_pos   = {0.0f, 0.0f, 0.0f};
float    g_circling_path_len   = 0.0f;
bool     g_circling_announced  = false;

// 8 cardinals, +X = East, +Y = North. Order: N, NE, E, SE, S, SW, W, NW.
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

    // Nearby blocker bodies (creatures + placeables). Self is excluded
    // by position epsilon instead of handle compare.
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
        // Z stays — SegmentCrossesWalkmesh is planar.

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

// Fires when walking but oscillating: cumulative path length to net
// displacement ratio is high over a 4s window. Reuses RunStuckProbe; the
// "circling detected" log line distinguishes the trigger path.
void TickCirclingAnnounce(const Vector& pos, uint64_t now_ms, bool walking) {
    if (!walking) {
        g_circling_anchor_ms = 0;
        g_circling_path_len  = 0.0f;
        g_circling_announced = false;
        return;
    }

    if (g_circling_anchor_ms == 0) {
        g_circling_anchor_pos = pos;
        g_circling_anchor_ms  = now_ms;
        g_circling_prev_pos   = pos;
        g_circling_path_len   = 0.0f;
        return;
    }

    const float dx_step = pos.x - g_circling_prev_pos.x;
    const float dy_step = pos.y - g_circling_prev_pos.y;
    g_circling_path_len += sqrtf(dx_step * dx_step + dy_step * dy_step);
    g_circling_prev_pos = pos;

    if ((now_ms - g_circling_anchor_ms) < kCirclingWindowMs) {
        return;
    }

    // Window elapsed — evaluate.
    const float dx_net = pos.x - g_circling_anchor_pos.x;
    const float dy_net = pos.y - g_circling_anchor_pos.y;
    const float netDist = sqrtf(dx_net * dx_net + dy_net * dy_net);
    const float ratio   = (netDist > 0.01f)
        ? (g_circling_path_len / netDist) : g_circling_path_len;

    const bool circling = (g_circling_path_len >= kCirclingMinPathMeters) &&
                          (netDist <= kCirclingMaxNetMeters) &&
                          (g_circling_path_len >=
                           netDist * kCirclingPathRatio);

    if (circling) {
        if (!g_circling_announced) {
            acclog::Write("StuckProbe",
                "circling detected — path=%.2fm net=%.2fm ratio=%.2f "
                "window_ms=%llu",
                g_circling_path_len, netDist, ratio,
                static_cast<unsigned long long>(
                    now_ms - g_circling_anchor_ms));
            RunStuckProbe(pos);
            g_circling_announced = true;
        }
        // Keep the anchor — re-arm only when the user escapes.
    } else {
        // Real progress — re-anchor + re-arm.
        g_circling_anchor_pos = pos;
        g_circling_anchor_ms  = now_ms;
        g_circling_path_len   = 0.0f;
        g_circling_announced  = false;
    }
}

void TickStuckAnnounce(const Vector& pos, uint64_t now_ms) {
    const bool walking =
        (g_last_leader_footstep_ms != 0) &&
        ((now_ms - g_last_leader_footstep_ms) < kFootstepFreshnessMs);
    if (!walking) {
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
        g_progress_checkpoint_pos = pos;
        g_progress_checkpoint_ms  = now_ms;
        g_stuck_announced = false;
        return;
    }

    // Latch — re-arms via the displacement branch above.
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
        // No player loaded — reset so next observation seeds fresh.
        g_have_last_sample = false;
        g_was_stuck = false;
        g_progress_checkpoint_ms = 0;
        g_stuck_announced = false;
        g_last_leader_footstep_ms = 0;
        g_circling_anchor_ms = 0;
        g_circling_path_len  = 0.0f;
        g_circling_announced = false;
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
    if (dt_ms < kMinDeltaMs) return;  // sample too fresh to trust

    const float dx = pos.x - g_last_pos.x;
    const float dy = pos.y - g_last_pos.y;
    // Z excluded — vertical jitter on uneven walkmesh isn't motion.
    const float distSq = dx * dx + dy * dy;
    const float dt_s = static_cast<float>(dt_ms) * 0.001f;
    const float speedSq = distSq / (dt_s * dt_s);
    const bool prev_stuck = g_was_stuck;
    g_was_stuck = (speedSq < kStuckSpeedSq);

    // Edge-driven log: full fidelity without per-tick spam.
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

    // Walking signal computed here so both detectors see the same value.
    const bool walking =
        (g_last_leader_footstep_ms != 0) &&
        ((now_ms - g_last_leader_footstep_ms) < kFootstepFreshnessMs);
    TickCirclingAnnounce(pos, now_ms, walking);
}

}  // namespace acc::audio::footstep_suppress

// CSWCCreature::PlayFootstep detour. Hook REPLACES the engine's JZ at
// 0x0061a31a (skip_original_bytes=true) so the handler owns both branches:
//   return 1 → wrapper jumps to 0x0061a632 (engine's natural early-out
//              destructor cascade). Use for our stuck-suppression OR to
//              mimic the engine's own field6_0x20==0 branch.
//   return 0 → resume at 0x0061a320 (audio plays).
// ESI holds `this` at the cut point.
//
// The hooks.toml comment above the entry documents why every "obvious"
// cut location clobbered EFLAGS or EAX.
extern "C" int __cdecl OnPlayFootstep(void* creature) {
    if (!creature) return 1;

    // Mimic the engine's field6_0x20==0 early-out — we replaced its JZ.
    uint32_t field20 = 0;
    __try {
        field20 = *reinterpret_cast<uint32_t*>(
            static_cast<unsigned char*>(creature) + 0x20);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;  // bad pointer — abort safely, audio skipped
    }
    if (field20 == 0) return 1;  // engine's natural early-out

    void* leader = acc::engine::GetClientLeader();
    const bool is_leader = (leader && creature == leader);
    const bool stuck = acc::audio::footstep_suppress::WasStuckLastTick();
    // Suppress non-leader footsteps too while the leader is stuck — otherwise
    // a companion or scripted NPC walking nearby fills the silence the player
    // relies on to detect "I'm blocked". KOTOR's footstep audio attenuates
    // with distance, so any non-leader footstep we'd hear is by definition
    // close enough to mask the leader's own silence; safe to mute the whole
    // class until the leader's velocity recovers.
    const int verdict = stuck ? 1 : 0;

    if (is_leader) {
        // Stamps walk-anim freshness for the stuck-direction probe gate.
        acc::audio::footstep_suppress::NoteLeaderFootstep();
    }

    acclog::Write("FootstepSup", "PlayFootstep this=%p leader=%p field20=0x%x "
        "is_leader=%d stuck=%d verdict=%d",
        creature, leader, field20,
        is_leader ? 1 : 0, stuck ? 1 : 0, verdict);

    return verdict;  // 1 = suppress (wrapper jumps to 0x0061a632)
}
