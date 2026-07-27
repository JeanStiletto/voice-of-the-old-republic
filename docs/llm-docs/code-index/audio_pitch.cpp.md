# audio_pitch.cpp (78 lines)

Pitch-variance neutraliser hook handler + the thread_local scoped-flag mechanism that gates it. `BeginScopedZero`/`EndScopedZero` (called from audio_bus.cpp around PlayOneShotSound/Play3DOneShotSound) bracket exactly the engine calls we issued; `OnCalculatePitchVarianceFrequency` returns the source's unmodified base_frequency instead of the engine's randomised jitter only while the flag is set, so engine-driven plays of the same resref are unaffected. Uses a counter (not bool) so nested calls don't prematurely re-arm jitter; the audio thread's own counter is always 0 so its calls fall through untouched.

## Declarations (in source order)

- L31 — `kOffsetBaseFrequency = 0x48` — CExoSoundSourceInternal field offset
- L35 — `thread_local int t_scoped_zero_depth` — nesting counter
- L39 — `void BeginScopedZero()`
- L43 — `void EndScopedZero()`
- L47 — `bool IsScopedZeroActive()`
- L53 — `extern "C" int __cdecl OnCalculatePitchVarianceFrequency(void* source)` — hook handler; out-of-scope sounds fall through (return 0), in-scope returns base_frequency (or 1 defensively if 0/bad-pointer); 1Hz heartbeat log
  note: detour at function entry (5-byte prologue cut, skip_original_bytes=false), exclude_from_restore=["eax"], consumed_exit_address is the shared POP/RET epilogue — see hooks.toml comment above OnCalculatePitchVarianceFrequency for the exact byte-level rationale
