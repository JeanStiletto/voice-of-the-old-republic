// Pitch-variance neutraliser for accessibility cues.
//
// BeginScopedZero / EndScopedZero bracket the engine's PlayOneShotSound
// calls in audio_bus.cpp. While the per-thread flag is set, the detour
// at CExoSoundSourceInternal::CalculatePitchVarianceFrequency returns
// the source's base frequency instead of the engine's randomised value
// — pitch jitter shifts HRTF response, which degrades localisation and
// breaks per-cue identification.
//
// thread_local rather than global: only the source we just asked the
// engine to create gets neutralised. Resref-matching would also catch
// engine-driven plays of the same resrefs (Inventory_Add, etc.).

#pragma once

namespace acc::audio::pitch {

// Pair 1:1, Begin before the engine call, End after even on failure.
// Nestable via counter if needed.
void BeginScopedZero();
void EndScopedZero();

// Read from the detour handler.
bool IsScopedZeroActive();

}  // namespace acc::audio::pitch

extern "C" int __cdecl OnCalculatePitchVarianceFrequency(void* source);
