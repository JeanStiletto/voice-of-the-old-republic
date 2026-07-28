# audio_pitch.h (28 lines)

Header for the pitch-variance neutraliser. thread_local rather than global so only the source we just asked the engine to create gets neutralised (resref-matching would also catch unrelated engine-driven plays of the same sound).

## Declarations (in source order)

- L20 — `void BeginScopedZero()` — pair 1:1 with EndScopedZero, nestable via counter
- L21 — `void EndScopedZero()`
- L24 — `bool IsScopedZeroActive()` — read from the detour handler
- L28 — `extern "C" int __cdecl OnCalculatePitchVarianceFrequency(void* source)`
