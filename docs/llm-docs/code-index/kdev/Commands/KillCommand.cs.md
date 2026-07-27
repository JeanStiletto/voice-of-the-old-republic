# KillCommand.cs (29 lines)

`kdev kill` — terminates any running `swkotor.exe` processes via `GameProcess.KillAll()` and reports found/killed/failed counts.

## Declarations (in source order)

- L6 — `static class KillCommand`
- L8 — `Command Build()`
- L15 — `int Run()` — delegates entirely to `GameProcess.KillAll()`, exit 0 if `AllKilled` else 1
