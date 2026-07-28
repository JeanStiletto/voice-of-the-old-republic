# IModInstaller.cs (32 lines)

Interface implemented by each bundled optional mod installer (K1CP, K2CP, Tweak Pack, and future JDR / Party Conversations / Thematic Companions / Swoop Bike Upgrades / UniWS / HR Menus). Contract: each installer is fully self-contained — downloads its own source, stages, applies, and cleans up. `ModInstallerCoordinator` owns ordering and per-mod selection gating via `ModSelection`.

## Declarations (in source order)

- L14 — `public interface IModInstaller`
- L17 — `string Id { get; }` — short stable identifier, e.g. "k1cp"
- L19 — `string DisplayName { get; }` — human-readable name shown in UI/summary
- L26 — `bool IsSelected(ModSelection selection)` — coordinator skips installers returning false
- L29 — `Task<ModInstallResult> InstallAsync(ModInstallContext ctx)`
  note: contract says it throws nothing; reports failure via `ModInstallResult.Success`
