// Pitch-variance neutraliser — make every fire of the same WAV resref
// land at exactly the same playback frequency, scoped to our specific
// Play*OneShotSound calls.
//
// Why: the engine's CExoSoundSourceInternal applies an automatic per-fire
// pitch jitter as anti-repetition cosmetic. For accessibility cues that
// breaks two assumptions the rest of the patch relies on:
//   1. HRTF localisation is frequency-dependent. The brain's "where is
//      that sound" decode uses the ear's frequency-shaped filter
//      response; jittering pitch shifts the cue across HRTF notches and
//      smears the perceived spatial position from one fire to the next.
//   2. Per-cue identification by timbre. Wall / Door / Item / NPC each
//      pick a distinct sample whose natural pitch helps the user
//      distinguish them; ±10% engine jitter smudges that distinction.
// Doppler / pan / volume / distance attenuation are unaffected — those
// don't go through pitch_variance.
//
// Scope mechanism — thread_local flag set by audio_bus's PlayCue3D /
// PlayCue around the engine call. The detour at
// CExoSoundSourceInternal::CalculatePitchVarianceFrequency reads the
// flag:
//   * Flag set → the engine is computing variance for a source we just
//     asked it to create on this thread; replace the function body with
//     a "return base_frequency" handler.
//   * Flag clear → some other engine code triggered the variance calc
//     (footstep, sword swing, ambient one-shot, mid-playback chorus on
//     a different thread). Fall through to the original function so
//     that sound keeps its jitter.
//
// Detour design at function entry (0x005db3d0):
//   * 5-byte prologue cut: PUSH ECX; PUSH ESI; PUSH EDI; MOV ESI, ECX.
//     skip_original_bytes = false so the wrapper emits the prologue and
//     the stack is correctly framed before the consumed_exit jump.
//   * exclude_from_restore = ["eax"] so the handler's int return reaches
//     the wrapper's `TEST EAX,EAX; JZ +5; JMP rel32 consumed_exit`
//     dispatch.
//   * consumed_exit_address = 0x005db449 (the shared
//     `POP EDI; POP ESI; POP ECX; RET` epilogue) — pops the 12 bytes the
//     cut just pushed and RETs with EAX still holding base_frequency.
//   * Handler returns 0 to fall through to the original function (when
//     the flag is clear OR the source pointer is invalid). The wrapper
//     emits the cut bytes before the dispatch, so falling through to
//     hookAddress + 5 (= 0x005db3d5) lands at the correct continuation
//     with stack/regs in the same state the prologue would have left.
//
// Edge case — base_frequency == 0 with flag set: we return 1 instead of
// 0 because a 0 return makes the wrapper's TEST take the JZ → fall-
// through path even though we wanted to consume. base_frequency = 0 is
// not expected for any real cue (sample-rate-derived value, always
// positive); the branch exists for defensive null-this and read-fault
// handling.

#include "audio_pitch.h"

#include <windows.h>
#include <cstdint>

#include "log.h"

namespace acc::audio::pitch {

namespace {

// Field offsets inside CExoSoundSourceInternal — verified against Lane's
// SARIF database (struct definition cited in audio_bus.h above the
// kAddrCExoSoundSourceInternalCalculatePitchVarianceFrequency constant).
constexpr ptrdiff_t kOffsetBaseFrequency = 0x48;

// Per-thread depth counter. Counter (not bool) so nested helpers can
// safely Begin/End without prematurely re-arming jitter. The audio
// thread sees its own counter (always 0), so its mid-playback chorus
// calls fall through to the original function.
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
    // Out-of-scope sound (engine footstep / GUI / ambient / mid-playback
    // chorus) — fall through to the original function so it keeps its
    // jitter. The wrapper has already emitted the prologue cut bytes;
    // returning 0 routes it to hookAddress + 5 which is the correct
    // post-prologue continuation.
    if (!acc::audio::pitch::IsScopedZeroActive()) return 0;

    // Defensive — null-this / SEH-faulting read both consume to keep
    // the engine in a safe state; the alternative (fall through with a
    // bad pointer) would crash the audio thread.
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

    // Heartbeat — once per second, only when our scope is active. Tells
    // us the hook is firing on cue-creation calls and gives us the
    // actual base_frequency value the engine uses (useful future-tuning
    // signal). For per-fire resref logging (used 2026-05-07 to confirm
    // missing-resref no-op behaviour), see git log on this file.
    static DWORD s_last_heartbeat_ms = 0;
    if (now_ms - s_last_heartbeat_ms >= 1000u) {
        acclog::Write("PitchHook", "scoped fire — source=%p base_freq=%d (heartbeat)",
            source, base_frequency);
        s_last_heartbeat_ms = now_ms;
    }

    return base_frequency != 0 ? base_frequency : 1;
}
