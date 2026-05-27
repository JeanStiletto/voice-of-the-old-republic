# passive_narrate.h (39 lines)

Passive-selection narration header. Hooks CClientExoAppInternal::ShowObject for clean user-driven focus-change detection (avoids combat AI noise from polling GetLastTarget).

## Declarations (in source order)

- L23 — `namespace acc::passive_narrate`
- L27 — `void OnEngineShowObject(uint32_t handle);`
  note: updates current-focus cache AND drives delta-based ambient narration; suppress-on-first-tick pattern prevents save-resume noise
- L33 — `void RequestQEReannounce();`
  note: deferred because our input detour fires before the engine's Q/E handler calls ShowObject; Tick drains it next frame
- L36 — `void Tick();`
  note: per-tick drain of pending Q/E re-announce
