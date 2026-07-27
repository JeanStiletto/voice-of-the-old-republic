# CleanCommand.cs (63 lines)

`kdev clean` — kills any running game (`GameProcess.KillAll`) then uninstalls every patch from the game install via `KPatchCore.Applicators.PatchRemover.RemoveAllPatches`, restoring from backup. First step of `DevCommand`'s daily-driver chain.

## Declarations (in source order)

- L7 — `static class CleanCommand`
- L9 — `Command Build()`
- L16 — `int Run()` — kill game (abort with exit 1 if it can't be stopped) → `PatchRemover.RemoveAllPatches(config.GameExe)` → report removed-file count and whether a backup was restored
