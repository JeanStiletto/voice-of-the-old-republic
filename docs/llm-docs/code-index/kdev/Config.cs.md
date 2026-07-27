# Config.cs (242 lines)

Loads and validates `kdev.toml`, the project-level config every command depends on (`KdevConfig.Load()` / `LoadOrPrintErrors`). Walks upward from the current directory to find `kdev.toml`, parses it via Tomlyn, resolves all paths relative to the project root (the directory containing `kdev.toml`), and exposes required fields (`game.install`, `patch_manager.release`, `upstream.clone`, `accessibility.source`/`build_output`/`patch_id`) plus an optional `[re]` section (SARIF/jq/Ghidra paths) whose defaults mirror `docs/tools.md`. `Validate()` is a separate pass checking the resolved paths actually exist on this machine (distinct from `Load()`'s shape enforcement) — used by `StatusCommand` and `LoadOrPrintErrors`.

## Declarations (in source order)

- L9 — `sealed record KdevConfig(ProjectRoot, GameInstall, GameExe, PatchManagerRelease, UpstreamClone, AccessibilitySource, BuildOutput, PatchId, AdditionalPatchIds, ReSarifPath, ReJqPath, ReGhidraHeadless, ReGhidraProjectDir, ReGhidraProjectName, ReGhidraProgram, ReGhidraScripts)`
- L30 — `string LogsDir` — `ProjectRoot/logs`
- L32 — `const string FileName = "kdev.toml"`
- L38 — `string? FindConfigFile(string? startDir)` — walks parent directories looking for `kdev.toml`
- L54 — `KdevConfig Load(string? configPath)` — parses TOML, resolves all paths, applies `[re]` defaults when keys are absent
  note: `[re]` section is fully optional — every key has a fallback matching the canonical setup in `docs/tools.md`
- L119 — `KdevConfig? LoadOrPrintErrors(out int exitCode)` — convenience wrapper: prints to stderr and sets exit code (2 = parse/shape failure, 3 = validation failure) instead of throwing; used by nearly every command
- L150 — `IReadOnlyList<string> Validate()` — checks game install/exe, `KotorPatcher.dll` in the release bin, upstream clone dir, accessibility source dir all exist
- L181 — `IReadOnlyList<string> OptionalStringArray(TomlTable table, string dottedKey)`
- L198 — `string RequireString(...)`
- L208 — `string? OptionalString(...)`
- L217 — `object? NavigateTable(TomlTable table, string dottedKey)` — dotted-key traversal (e.g. `"game.install"`)
- L229 — `string ResolvePath(string projectRoot, string path)` — rooted paths pass through; relative paths resolve against `projectRoot`
- L237 — `sealed class KdevConfigException : Exception`
