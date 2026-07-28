# GameProcess.cs (72 lines)

Shared helpers for inspecting and terminating the running `swkotor.exe` process. Used by `ApplyCommand`, `CleanCommand`, `KillCommand`, `DumpTextCommand`.

## Declarations (in source order)

- L8 — `static class GameProcess`
- L10 — `const string ExeName = "swkotor.exe"`
- L11 — `const string ProcessName = "swkotor"`
- L12 — `const int WaitForExitMs = 5000`
- L14 — `sealed record KillSummary(int FoundCount, int KilledCount, int FailedCount)` — `AllKilled`, `NothingToKill` computed properties
- L23 — `bool IsRunning()`
- L29 — `KillSummary KillAll()` — kills every `swkotor` process tree, waits up to 5s each, logs per-PID progress
