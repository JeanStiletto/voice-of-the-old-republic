# RegistryManager.cs (128 lines)

Static wrapper around the Windows Add/Remove Programs uninstall registry key (`HKLM\...\Uninstall\KotorAccessibility`). Writes `DisplayName`/`Version`/`Publisher`/`InstallLocation`/uninstall strings on install so the mod shows up in Windows Settings > Apps, and provides lookups (`GetRegisteredInstallLocation`, `GetRegisteredVersion`) that `Program.cs` uses for game-path auto-detection and update-version comparison. All methods swallow exceptions and log rather than throw — registry access can fail without admin rights or if the key doesn't exist yet.

## Declarations (in source order)

- L10 — `public static class RegistryManager`
- L12 — `private const string UninstallKeyPath = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"`
- L13 — `private const string AppKeyName = "KotorAccessibility"`
- L15 — `public static void Register(string installPath, string version, string uninstallerPath = null)`
  note: `UninstallString`/`QuietUninstallString` point at the uninstaller exe if provided, else `Environment.ProcessPath`
- L63 — `public static void Unregister()` — deletes the subkey tree; no-op if missing
- L97 — `public static bool IsRegistered()`
- L107 — `public static string GetRegisteredInstallLocation()`
- L117 — `public static string GetRegisteredVersion()`
