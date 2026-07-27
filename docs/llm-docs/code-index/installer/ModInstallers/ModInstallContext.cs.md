# ModInstallContext.cs (32 lines)

Plain data-carrier record handed by `ModInstallerCoordinator` to each `IModInstaller.InstallAsync` call. Bundles the resolved game dir, detected locale, the shared staged HoloPatcher.exe path, and the two UI callback hooks (progress slice + status text) the coordinator has already scoped to that installer's slot.

## Declarations (in source order)

- L11 — `public sealed class ModInstallContext`
- L14 — `public string GameDir { get; init; }`
- L17 — `public GameLocale Locale { get; init; }` — detected via dialog.tlk header
- L23 — `public string HoloPatcherExePath { get; init; }` — lazily populated by `HoloPatcherProvider`
- L26 — `public Action<int> Progress { get; init; }` — 0..100 for this installer's slice
- L29 — `public Action<string> StatusUpdate { get; init; }`
