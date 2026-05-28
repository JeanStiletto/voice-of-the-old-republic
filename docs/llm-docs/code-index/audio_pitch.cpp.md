# audio_pitch.cpp (79 lines)

Implements the pitch-variance neutraliser. Scoped depth is thread_local so
the audio thread (always depth 0) never gets its jitter suppressed. The
detour exits through the shared POP/RET epilogue (consumed_exit_address).

## Declarations (in source order)

- L27 — `namespace acc::audio::pitch`
- L29 — `namespace` (anonymous)
- L31 — `constexpr ptrdiff_t kOffsetBaseFrequency = 0x48`
  note: byte offset into CExoSoundSourceInternal to read the sample's base playback frequency.
- L34 — `thread_local int t_scoped_zero_depth = 0`
  note: counter (not bool) so nested helper calls don't prematurely re-arm jitter.
- L39 — `void BeginScopedZero()`
- L43 — `void EndScopedZero()`
- L47 — `bool IsScopedZeroActive()`
- L53 — `extern "C" int __cdecl OnCalculatePitchVarianceFrequency(void* source)`
