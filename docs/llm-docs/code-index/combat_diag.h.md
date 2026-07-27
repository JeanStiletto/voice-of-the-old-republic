# combat_diag.h (70 lines)

Read-only diagnostic probe over the four state bits (`CSWCCreature` combat-mode bit, combat-round queue depth, auto-paused, pause-state, main-interface target handle) that decide chain-vs-overwrite in `DoPersonalAction`/`DoTargetAction`. Logs to `Combat.Diag`; also declares detour entry points wired via hooks.toml into `CSWSCombatRound::AddAction`/`RemoveAllActions`/`SetCurrentAction`/`RemoveLastAction` for deterministic ADD/CLEAR/SETCUR/REMLAST events instead of racing a poll.

## Declarations (in source order)

- L27 — `namespace acc::combat_diag`
- L29 — `void Tick()`
  note: per-frame from core_tick; emits a DELTA line whenever any tracked bit changes
- L33 — `void LogPreFire(const char* label)`
- L34 — `void LogPostFire(const char* label)`
  note: bare 1..7 fires after our prologue hook returns, so POST for those comes from the next Tick's DELTA instead
- L51 — `extern "C" void __cdecl OnCombatRoundAddAction(void*, void*, void*)`
  note: hooked at CSWSCombatRound::AddAction @0x4d3660; esp+N params require deref per the KPatchManager LEA-vs-MOV bug
- L54 — `extern "C" void __cdecl OnCombatRoundRemoveAllActions(void*)`
  note: hooked at @0x4d3770
- L67 — `extern "C" void __cdecl OnCombatRoundSetCurrentAction(void*, void*)`
- L69 — `extern "C" void __cdecl OnCombatRoundRemoveLastAction(void*)`
