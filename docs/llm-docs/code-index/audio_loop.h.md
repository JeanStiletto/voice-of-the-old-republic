# audio_loop.h (36 lines)

RAII wrapper over CExoSoundSource for engine-managed looping/3D sources.
Use when PlayCue3D's one-shot model doesn't fit (continuous tones, per-tick
position updates, explicit Stop needed). Single-threaded under OnUpdate.

## Declarations (in source order)

- L14 — `namespace acc::audio`
- L16 — `class LoopSource`
- L18 — `LoopSource() = default`
- L19 — `~LoopSource()`
  note: auto-calls Stop().
- L21 — `LoopSource(const LoopSource&) = delete`
- L22 — `LoopSource& operator=(const LoopSource&) = delete`
- L25 — `bool Start(const char* resref, const Vector& worldPosition)`
  note: returns true iff every engine call succeeded; false leaves handle inactive.
- L27 — `void UpdatePosition(const Vector& worldPosition)`
  note: safe no-op if inactive.
- L28 — `void Stop()`
  note: idempotent.
- L30 — `bool IsActive() const`
