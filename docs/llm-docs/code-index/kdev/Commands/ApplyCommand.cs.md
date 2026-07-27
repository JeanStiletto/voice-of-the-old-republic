# ApplyCommand.cs (225 lines)

`kdev apply` — installs the built `.kpatch` into the game via `KPatchCore` (`PatchRepository`/`PatchApplicator`). Kills any running game first (via `GameProcess.KillAll`), stages `Accessibility.kpatch` plus any `config.AdditionalPatchIds` bundled patches into `build/patches/`, scans/installs them, then copies the Prism runtime DLL (`third_party/prism-dist/x86/prism.dll`) and the `dinput8.dll` proxy loader into the game install. Cleans up stale Tolk-era runtime files. Depends on `KdevConfig`, `GameProcess`, and `KPatchCore.Applicators`/`Managers`.

## Declarations (in source order)

- L8 — `static class ApplyCommand`
- L10 — `Command Build()`
- L17 — `int Run()` — main sequence: locate `.kpatch` + `KotorPatcher.dll` → kill game → stage patches (ours + `AdditionalPatchIds` resolved from the bundle dir) → `PatchApplicator.InstallPatches` → copy Prism DLL → copy loader DLL
  note: fails loudly (exit 5) if the game can't be stopped, or if bundled `additional_patches` IDs aren't found in `PatchManagerRelease/patches`
- L158 — `int CopyLoaderDll(KdevConfig config)` — copies `loader/dinput8.dll` to the game install root; errors if `kdev build` hasn't produced it
- L176 — `int CopyPrismRuntimeDll(KdevConfig config)` — copies `prism.dll` into `<install>/patches/`; also deletes stale `Tolk.dll`/`nvdaControllerClient32.dll` left by prior installs
