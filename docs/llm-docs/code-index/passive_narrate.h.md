# passive_narrate.h (50 lines)

Public surface for the ShowObject-hook-driven passive/Q-E narration.
Documents why ShowObject (not polling GetLastTarget) is the hook point:
last_target is also written by combat AI every round, so per-tick polling
would race it.

## Declarations (in source order)

- L27 — `void OnEngineShowObject(uint32_t handle)`
  note: updates the Q/E re-announce cache AND drives delta-based ambient narration.
- L38 — `void RequestQEReannounce(int directionCode)`
  note: directionCode is the engine's logical Q/E code (204=E, 205=Q), stored for the sentinel-skip retry.
- L44 — `bool IsInSynthesizedQE()`
- L47 — `void Tick()`
