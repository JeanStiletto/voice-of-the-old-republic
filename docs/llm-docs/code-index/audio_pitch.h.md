# audio_pitch.h (28 lines)

Pitch-variance neutraliser for accessibility cues. BeginScopedZero /
EndScopedZero bracket PlayOneShotSound calls in audio_bus.cpp. While the
per-thread flag is set, the detour returns base_frequency instead of the
engine's randomised value. thread_local so only our cue gets neutralised.

## Declarations (in source order)

- L16 — `namespace acc::audio::pitch`
- L20 — `void BeginScopedZero()`
  note: paired 1:1 with EndScopedZero; nestable via counter.
- L21 — `void EndScopedZero()`
- L24 — `bool IsScopedZeroActive()`
  note: read from the detour handler.
- L28 — `extern "C" int __cdecl OnCalculatePitchVarianceFrequency(void* source)`
  note: hook handler for CExoSoundSourceInternal::CalculatePitchVarianceFrequency; returns base_frequency when scoped-zero active, 0 to fall through otherwise.
