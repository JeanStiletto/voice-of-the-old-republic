# StatusCommand.cs (56 lines)

`kdev status` — loads `KdevConfig`, prints the resolved configuration (game install/exe, patch manager release, upstream clone, accessibility source/build/id), then runs `config.Validate()` and lists any problems (missing paths/DLLs) with exit code 3 if any found, or 2 if `kdev.toml` itself fails to load.

## Declarations (in source order)

- L6 — `static class StatusCommand`
- L8 — `Command Build()`
- L15 — `int Run()` — `KdevConfig.Load()` (catch `KdevConfigException` → exit 2) → print resolved config → `Validate()` → exit 3 on problems, else 0
