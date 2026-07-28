# audio_loop.h (85 lines)

Header for `LoopSource`, RAII over `CExoSoundSource` for sustained/looping spatial audio. Documents each Start() parameter's default and override semantics in detail (priority group 0x17 default vs 0xb GUI-click group for menu-pause survival; volume default 0x7f; distance band default 10m/20m vs compressed-scale overrides like the turret peg's 1-20m).

## Declarations (in source order)

- L16 — `class LoopSource` — non-copyable
- L19 — `~LoopSource()` — auto-Stops
- L49 — `bool Start(const char* resref, const Vector& worldPosition, bool looping=true, bool spatial=true, int priorityGroup=-1, int volumeByte=-1, float maxVolDist=-1.0f, float minVolDist=-1.0f)`
- L54 — `void UpdatePosition(const Vector&)` — safe no-op if inactive
- L62 — `void UpdateVolume(int volumeByte)` — used by swoop cues to drive their own loudness curve independent of 3D falloff
- L64 — `void Stop()` — idempotent
- L76 — `void SetPitchMultiplier(float multiplier)` — 1.0=unchanged, 2.0=+1 octave; safe no-op until engine creates 3D voice or on 2D sources
- L78 — `bool IsActive() const`
