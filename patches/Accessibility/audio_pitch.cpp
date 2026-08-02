// Pitch-variance neutraliser: scoped to our Play*OneShotSound calls,
// makes every fire of the same resref land at the same frequency. Engine
// jitter shifts HRTF response (smearing the perceived spatial position)
// and blurs per-cue timbre identification.
//
// Detour at 0x005db3d0 (function entry):
//   - 5-byte prologue cut: PUSH ECX; PUSH ESI; PUSH EDI; MOV ESI, ECX
//     (skip_original_bytes=false so the wrapper emits it).
//   - exclude_from_restore=["eax"] so the handler's return survives
//     into the wrapper's TEST EAX,EAX dispatch.
//   - consumed_exit_address=0x005db449 (shared POP/RET epilogue): pops
//     the 12 bytes the cut pushed and RETs with EAX = base_frequency.
//   - Handler returns 0 to fall through (flag clear or bad source) →
//     wrapper lands at hookAddress+5 with the prologue already done.
//
// base_frequency=0 with flag set: return 1 instead of 0 so the wrapper's
// TEST doesn't route to the fall-through path. Defensive only — sample-
// derived base_frequency is always positive in practice.

#include "audio_pitch.h"

#include <windows.h>
#include <cstdint>

#include "log.h"
#include "engine_game.h"
#include "engine_offsets_select.h"

namespace acc::audio::pitch {

namespace {

// K2 0x50 — witnessed in the K2 jitter twin 0x00710550 (base frequency
// read), matching audio_loop's kInternalBaseFrequencyOffset.
const ptrdiff_t kOffsetBaseFrequency = acc::off::Pick(0x48, 0x50);

// Counter (not bool) so nested helpers don't prematurely re-arm jitter.
// Audio thread sees its own counter (always 0) → chorus calls fall through.
thread_local int t_scoped_zero_depth = 0;

}  // namespace

void BeginScopedZero() {
    ++t_scoped_zero_depth;
}

void EndScopedZero() {
    if (t_scoped_zero_depth > 0) --t_scoped_zero_depth;
}

bool IsScopedZeroActive() {
    return t_scoped_zero_depth > 0;
}

}  // namespace acc::audio::pitch

extern "C" int __cdecl OnCalculatePitchVarianceFrequency(void* source) {
    // Gate cleared for KOTOR 2 in Batch 5. Same handler serves both games:
    // the K2 twin's callers ignore EAX and re-read the applied-rate field,
    // so consuming (which skips the jitter write) neutralises jitter there;
    // the returned base frequency only needs to be non-zero.
    // Out-of-scope sounds keep their jitter — fall through.
    if (!acc::audio::pitch::IsScopedZeroActive()) return 0;

    // Consume on bad source to avoid crashing the audio thread.
    if (!source) return 1;
    int base_frequency = 0;
    __try {
        base_frequency = *reinterpret_cast<int*>(
            reinterpret_cast<char*>(source) +
            acc::audio::pitch::kOffsetBaseFrequency);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
    DWORD now_ms = GetTickCount();

    // 1Hz heartbeat — proves the hook fires + records base_frequency.
    static DWORD s_last_heartbeat_ms = 0;
    if (now_ms - s_last_heartbeat_ms >= 1000u) {
        acclog::Write("PitchHook", "scoped fire — source=%p base_freq=%d (heartbeat)",
            source, base_frequency);
        s_last_heartbeat_ms = now_ms;
    }

    return base_frequency != 0 ? base_frequency : 1;
}
