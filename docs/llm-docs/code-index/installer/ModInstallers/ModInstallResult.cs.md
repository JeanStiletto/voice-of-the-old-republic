# ModInstallResult.cs (31 lines)

Outcome record for a single `IModInstaller.InstallAsync` run, collected by `ModInstallerCoordinator` into a per-mod summary list surfaced to the user at the end of the install pipeline. Three static factory methods keep call sites terse.

## Declarations (in source order)

- L9 — `public sealed class ModInstallResult`
- L12 — `public string Id { get; init; }` — matches `IModInstaller.Id`
- L15 — `public bool Success { get; init; }`
- L18 — `public bool Skipped { get; init; }` — true when user opted out, not a failure
- L21 — `public string Error { get; init; }`
- L24 — `public List<string> Messages { get; init; } = new()`
- L26 — `public static ModInstallResult Ok(string id)`
- L27 — `public static ModInstallResult SkippedResult(string id)`
- L28 — `public static ModInstallResult Fail(string id, string error)`
