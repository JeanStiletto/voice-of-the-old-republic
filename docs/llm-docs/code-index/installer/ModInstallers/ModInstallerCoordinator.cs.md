# ModInstallerCoordinator.cs (127 lines)

Static orchestrator that builds and runs the ordered `IModInstaller` pipelines. `BuildPipeline()` is KOTOR 1's (today just `K1cpInstaller`, more TODO). `BuildKotor2Pipeline()` is KOTOR 2's (`K2cpInstaller` then `TweakPackInstaller`), run by `Kotor2ModsInstallForm` only after the caller has gated on TSLRCM presence — the community-mandated order is TSLRCM -> K2CP -> Tweak Pack. `InstallSelectedAsync` runs every selected installer, giving each an equal `[0..100]` progress slot; one installer failing does not stop the rest (no short-circuit), so the user always gets a full per-mod summary.

## Declarations (in source order)

- L18 — `public static class ModInstallerCoordinator`
- L20 — `public static IReadOnlyList<IModInstaller> BuildPipeline()` — KOTOR 1 pipeline: `[K1cpInstaller]`
- L36 — `public static IReadOnlyList<IModInstaller> BuildKotor2Pipeline()` — KOTOR 2 pipeline: `[K2cpInstaller, TweakPackInstaller]`
  note: caller must have already confirmed TSLRCM is installed before running this pipeline
- L50 — `public static async Task<List<ModInstallResult>> InstallSelectedAsync(IReadOnlyList<IModInstaller> pipeline, ModSelection selection, string gameDir, string holoPatcherExePath, Action<int> overallProgress, Action<string> statusUpdate)`
  note: `selection == null` means update-only path — returns empty results, skips all mods
  note: each selected installer's `ctx.Progress` remaps its own 0..100 into `[slotStart, slotEnd)` of the overall bar
