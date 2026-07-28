# BuildCommand.cs (628 lines)

`kdev build` — compiles `patches/Accessibility/` into `Accessibility.kpatch`. Two paths: the default incremental compiler (per-TU `.obj` cache under `build/objcache/` keyed by an MSVC/flags signature, header deps tracked via `cl /showIncludes`, only stale TUs recompiled, then one `link` pass into `windows_x86.dll`) or `--bat`, the legacy full-rebuild path that stages inputs and shells out to upstream's `create-patch.bat`. Both package the DLL + `manifest.toml` + `*hooks.toml` into a zip `.kpatch`, then build the `dinput8.dll` proxy loader via `loader/build.bat`. Compile/link flags are pinned to match `create-patch.bat` exactly (comment at top). `DevCommand` calls `BuildCommand.Run()` (incremental, no `--clean`) as its build step.

## Declarations (in source order)

- L13 — `static class BuildCommand`
- L18 — `const string CompileFlags = "/nologo /c /O2 /MD /W3 /EHsc /std:c++17"`
- L19 — `const string LinkFlags = "/nologo /LD /MD"`
- L21 — `Command Build()` — `--clean` and `--bat` options
- L45 — `int Run()` — default entry point used by `kdev dev`: incremental, no clean
- L51 — `readonly record struct CompileUnit(string Src, string Obj, string Deps)`
- L53 — `int RunIncremental(bool clean)` — validates manifest/hooks/exports.def exist, resolves vcvars32 env once, invalidates objcache on toolchain/flag signature change, gathers `.cpp` from patch/Common/GameAPI dirs, compiles stale units in parallel, links, zips the `.kpatch`, builds the loader
  note: requires a checked-in `exports.def` — the incremental path does not auto-generate one (use `--bat` for that)
- L200 — `bool NeedsRecompile(CompileUnit u)` — stale if obj missing, source newer, deps sidecar missing, or any tracked header newer than the obj
- L215 — `int CompileChanged(...)` — parallel compile across `Environment.ProcessorCount`, returns failure count
- L262 — `void WriteDeps(CompileUnit u, string clStdout, string projectRoot, string upstream)` — parses `/showIncludes` locale-independently by matching the repo-root path substring, keeps only real existing files
- L284 — `string FilterNotes(string s, string projectRoot)` — strips `/showIncludes` note lines from captured compiler output for cleaner failure logs
- L300 — `(int exit, string stdout, string stderr) RunCl(...)` — runs `cl.exe` with a captured environment
- L328 — `string? FindVcvars32()` — via `vswhere.exe`, falling back to a hardcoded VS2022 Community path
- L360 — `IDictionary<string,string>? CaptureEnv(string vcvars)` — runs `vcvars32.bat` then `set` via cmd.exe and parses the env block
- L388 — `string? FindOnPath(...)` — locates `cl.exe` in the captured env's PATH
- L407 — `int RunViaBat()` — legacy path: stages `Accessibility/`, upstream `Common/`, `lib/`, and `create-patch.bat` into `build/staging`, runs it via cmd.exe piping `< NUL` (so unconditional `pause` calls don't hang), logs to `logs/build-*.log`, moves the produced `.kpatch`, builds the loader
- L562 — `int BuildLoader(KdevConfig config)` — runs `loader/build.bat`, verifies `dinput8.dll` was produced
- L611 — `void CopyDirectory(string source, string destination)` — recursive copy helper for the staging path
