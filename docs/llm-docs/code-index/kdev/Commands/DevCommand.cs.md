# DevCommand.cs (50 lines)

`kdev dev` — the daily-driver command. Chains `clean` → `build` → `apply` → `launch --monitor` by calling each command's static `Run()` directly (no subprocess spawning), aborting immediately with the failing step's exit code.

## Declarations (in source order)

- L10 — `static class DevCommand`
- L12 — `Command Build()`
- L19 — `int Run()` — builds a `(string Name, Func<int> Run)[]` steps array (`CleanCommand.Run`, `BuildCommand.Run`, `ApplyCommand.Run`, `LaunchCommand.Run(monitor:true)`), runs in order, stops at first non-zero exit
