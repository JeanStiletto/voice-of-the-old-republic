# `kdev` — Dev CLI Design (v1)

Internal command-line tool for developing the KOTOR Accessibility patch. Wraps Lane Dibello's `KPatchCore` API and `create-patch.bat` build script behind a single ergonomic CLI so the inner dev loop is one command from "edited code" to "game running with my changes".

## Purpose

Working with Lane's Patch Manager directly means: GUI for install/uninstall, `create-patch.bat` for build, KPatchLauncher CLI for launch — three separate things, none of them automate the others. `kdev` collapses that into one tool with one config and one consistent error model.

## Decisions

### 1. CLI argument parsing — `System.CommandLine`

Microsoft's CLI parser library. Provides automatic `--help` generation, input validation, structured option parsing, and standard error messages. One NuGet dependency. Avoids reinventing argument-parsing infrastructure that drifts from reality if hand-maintained.

### 2. Config file format — TOML, parsed via `Tomlyn`

Matches Lane's ecosystem. Every other config in this project (`manifest.toml`, `hooks.toml`, `patch_config.toml`) is TOML, so our `kdev.toml` keeps cognitive load down. `Tomlyn` is a small actively-maintained .NET TOML library.

### 3. Patch source location — `patches/Accessibility/` at project root

Source lives in our project tree, not inside the cloned upstream repo at `third_party/Kotor-Patch-Manager/`. `kdev build` stages our source into the layout `create-patch.bat` requires (`Patches/<Name>/` next to a `Patches/Common/` and a `lib/` two levels up) using a temporary build directory. Symlinks Common and lib from the upstream clone to avoid duplication. Upstream stays read-only.

### 4. `exports.def` — committed, matching Lane

We commit a hand-curated `exports.def` alongside our C++ source, same as Lane does in his existing detour patches (`AdditionalConsoleCommands`, `LevelUpLimit`, `ScriptExtender`, `Widescreen`, etc.). When we add a new exported function we update the file in the same commit. The bat sees the file exists and skips its own (stale-prone) auto-generation path.

Optional safety net: `kdev status` lints for drift between `extern "C" __cdecl` definitions in source and the names listed in `exports.def`. Warning-only, ~20 lines of code.

### 5. Build caching — none, always rebuild

Every `kdev build` runs the bat fresh. No fingerprinting of input files, no `.last-build` artifact. Builds are 5–15 seconds; the cost of a stale-cache false negative ("my fix isn't in the build but the tool says it is") is far higher than the wait. Add caching later only if iteration speed becomes a measured pain point.

### 6. Watch mode — deferred to v2

No `--watch` flag in v1. Manual `kdev dev` invocation per iteration. KOTOR relaunches are heavy (~30 s end-to-end including save reload), so automatic re-trigger on file save would be actively harmful — accidental saves during editing would kill live game sessions. Adding watch later is a self-contained ~50-line feature.

## Project layout

```
C:\Users\fabia\Dev\kotor\
├── docs/
│   ├── tools.md
│   ├── accessibility-investigation.md
│   ├── kdev-design.md                    (this file)
│   └── llm-docs/
├── third_party/
│   ├── Kotor-Patch-Manager/              cloned source, read-only
│   └── KotorMessageInjector/             cloned source, debug tool
├── tools/
│   └── kdev/                             dev CLI source
│       ├── kdev.csproj
│       ├── Program.cs                    entry + command tree
│       ├── Config.cs                     Tomlyn loader, schema validation
│       ├── Commands/
│       │   ├── StatusCommand.cs
│       │   ├── BuildCommand.cs
│       │   ├── CleanCommand.cs
│       │   ├── ApplyCommand.cs
│       │   ├── LaunchCommand.cs
│       │   ├── DevCommand.cs
│       │   ├── KillCommand.cs
│       │   └── LogsCommand.cs
│       └── README.md
├── patches/
│   └── Accessibility/                    our patch source
│       ├── manifest.toml
│       ├── hooks.toml
│       ├── exports.def
│       └── *.cpp / *.h
├── build/                                generated .kpatch artifacts (gitignored)
├── logs/                                 run logs (gitignored)
├── kdev.toml                             project config
└── .gitignore
```

## Configuration — `kdev.toml`

Project-root file, read at startup. Schema:

```toml
[game]
install = 'C:\Program Files (x86)\Steam\steamapps\common\swkotor'

[patch_manager]
release = 'C:\Tools\KotorPatchManager-v0.4.2'

[upstream]
clone = 'third_party/Kotor-Patch-Manager'

[accessibility]
source = 'patches/Accessibility'
build_output = 'build'
patch_id = 'accessibility'
```

`Config.cs` parses and validates: every path resolves, every required key present, version of the patch manager release matches what we expect. Helpful error messages name the missing or invalid key.

## Subcommands

Each is a verb: short, descriptive, predictable.

- **`kdev status`** — health check. Validates config paths, runs `GameDetector.DetectVersion()` against the configured game exe, lists patches currently installed in the game dir, reports last build artifact age, reports whether KOTOR is currently running, optionally lints `exports.def` against C++ source. Exits 0 if everything is green; non-zero with a numbered fix list otherwise. **First command to implement** — also serves as the test that the scaffold works.

- **`kdev build`** — stages `patches/Accessibility/` into a temp build dir, sets `SKIP_PAUSE=1`, invokes `create-patch.bat`, captures stdout/stderr to `logs/build-<utc>.log`, copies the resulting `.kpatch` into `build/`. On failure dumps the last 50 lines of `build.log`.

- **`kdev clean`** — kills any running KOTOR process, calls `KPatchCore.Applicators.PatchRemover.RemoveAllPatches(gameExe)`. Idempotent — if nothing to clean, says so and exits 0.

- **`kdev apply`** — copies `build/Accessibility.kpatch` into the runtime patches directory, calls `PatchApplicator.InstallPatches({ PatchIds: ["accessibility"] })`. Streams `InstallResult.Messages` to stdout.

- **`kdev launch [--monitor]`** — calls `GameLauncher.LaunchWithInjection(...)`. With `--monitor`, blocks until the game exits and reports exit code.

- **`kdev dev`** — the daily driver. Runs `clean → build → apply → launch --monitor` in sequence. All output streamed to `logs/dev-<utc>.log` and stdout. Fail-fast: any step's failure aborts the sequence with that step's exit code.

- **`kdev kill`** — terminates any running `swkotor.exe` process. Useful when iterating on a hook that hangs the game.

- **`kdev logs [--follow]`** — tails the most recent log under `logs/`. With `--follow` keeps streaming.

## Error handling and exit codes

All KPatchCore calls return `{Success: bool, Error: string?, Messages: List<string>}`. Convention:

- `Messages` → write each to stdout/log as informational
- `Error` → write to stderr with a `ERROR:` prefix
- `Success == false` → exit non-zero with a code that names the failure class

Exit codes:
- `0` — success
- `1` — generic failure
- `2` — config invalid or missing
- `3` — game install not found or wrong version
- `4` — build failed (compile error, missing VS, etc.)
- `5` — apply failed (patch incompatible, install error)
- `6` — launch failed (injection error, Steam not running)

Every command starts with the same preflight: `Config.Load()` → fail with code 2 on bad config. After that each command can do its own validation and use its own codes.

## Logging conventions

- `logs/kdev-<utc>.log` — main per-invocation log of what kdev attempted and outcomes (timestamped, one-line-per-event)
- `logs/build-<utc>.log` — captured stdout of `create-patch.bat` (mirror of upstream's own `build.log`)
- `logs/dev-<utc>.log` — combined log when `kdev dev` orchestrates multiple subcommands
- `logs/patch.log` — written by our injected DLL at runtime (convention we'll establish in our C++)
- Game's own `<install>/logs/` already exists and is unrelated; `kdev logs` may surface its tail too

UTC timestamps in filenames so logs sort lexicographically.

## Dependencies

- .NET 10 SDK (the system has 10 installed; targeting net10.0)
- `System.CommandLine` (NuGet — Microsoft, beta but widely used)
- `Tomlyn` (NuGet — small TOML parser)
- `KPatchCore` referenced at **compile time** via `<ProjectReference>` to `third_party/Kotor-Patch-Manager/src/KPatchCore/KPatchCore.csproj`. **Not** loaded at runtime — the release zip's KPatchLauncher.exe is a self-contained single-file publish that embeds KPatchCore.dll inside it, so there is no separate DLL on disk to load. Compile-time reference gives us type checking and IntelliSense; tradeoff is that `kdev` builds depend on the cloned source tree existing at the configured path.
- Visual Studio 2022 with C++ desktop workload (transitively, via `create-patch.bat`)

### Why not Assembly.LoadFrom

The original design called for runtime loading of `KPatchCore.dll` from `<release>/bin/`. Smoke testing revealed that file does not exist in the release zip — Lane publishes `KPatchLauncher.exe` as a self-contained single-file deployment with KPatchCore embedded. We pivoted to `<ProjectReference>` to the cloned source. This is documented here so the discrepancy with earlier sketches isn't confusing.

## Non-goals (explicitly out of scope for v1)

- Auto-discovery of game install path (config required)
- Multi-patch orchestration (focus is the single Accessibility patch)
- KOTOR 2 support (KOTOR 1 only)
- TUI / interactive UI
- Watch mode
- Build caching
- Bundling `KPatchCore.dll` inside `kdev` (load from release dir)
- Cross-platform (Windows-only, since the framework is)
- Replacement for the GUI launcher for end-users (kdev is for *us*, building the patch)

## v2 considerations

For when v1 hits real friction:

- `kdev dev --watch` — file system watch, debounce, auto-trigger
- Build cache with input fingerprinting
- `kdev release` — package the final `.kpatch` plus README into a HoloPatcher-ready zip
- `kdev hook list` — pretty-print the patch's hook table from `hooks.toml` for review
- `kdev probe` — call into `KotorMessageInjector` to read live game state for debug
- KOTOR 2 support via second `[game.kotor2]` config block

## Implementation order

1. ✅ Scaffold — csproj, Program.cs, Config.cs, command stubs. `kdev --help` works.
2. ✅ `kdev status` — config load + path validation. Smoke-tested: green.
3. ✅ `kdev kill` — terminates `swkotor.exe`. Logic in shared `GameProcess` helper.
4. ✅ `kdev clean` — first KPatchCore integration via `<ProjectReference>`; runs `PatchRemover.RemoveAllPatches()`. Smoke-tested against vanilla install.
5. ✅ `kdev build` — staging dir + create-patch.bat invocation; logs to `logs/build-<utc>.log`. Unhappy path (no source) verified; happy path requires real patch source.
6. ✅ `kdev apply` — stages built `.kpatch` into runtime patches dir, runs `PatchApplicator.InstallPatches`. Unhappy path verified.
7. ✅ `kdev launch` — `GameLauncher.LaunchGame` (auto-detects vanilla vs injected based on `patch_config.toml`); `--monitor` blocks until exit.
8. ✅ `kdev dev` — chains steps 4 → 5 → 6 → 7-with-monitor; fail-fast with the failing step's exit code. End-to-end orchestration verified up to expected build failure.
9. ✅ `kdev logs` — tails most recent log under `logs/`; `--follow` polls for new lines until Ctrl+C.

All eight commands compile cleanly under `dotnet build` (0 warnings, 0 errors in our code; 3 warnings in upstream `KPatchCore` are tolerated). Full happy path through `kdev dev` requires writing actual patch source in `patches/Accessibility/` and having Visual Studio 2022 with C++ tools installed — out of scope for the scaffold phase.
