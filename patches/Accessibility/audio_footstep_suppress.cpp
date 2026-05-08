#include "audio_footstep_suppress.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "engine_offsets.h"  // Vector
#include "engine_player.h"   // GetPlayerPosition, GetClientLeader
#include "log.h"

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

}  // namespace

bool WasStuckLastTick() { return g_was_stuck; }

void Tick() {
    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) {
        // No player loaded (main menu, area-load). Reset state so the
        // next observation seeds fresh rather than comparing against
        // stale data from a different area.
        g_have_last_sample = false;
        g_was_stuck = false;
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

    acclog::Write("FootstepSup", "PlayFootstep this=%p leader=%p field20=0x%x "
        "is_leader=%d stuck=%d verdict=%d",
        creature, leader, field20,
        is_leader ? 1 : 0, stuck ? 1 : 0, verdict);

    return verdict;  // 1 = suppress (wrapper jumps to 0x0061a632)
}
