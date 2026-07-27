# Program.cs (1038 lines)

Entry point for the installer EXE. `Main` parses CLI args (`/uninstall`, `/quiet`, `/auto-update`, `--local-kpatch <path>`, positional game-path override), requires admin + a non-running game, then branches into uninstall mode, headless auto-update mode (`--auto-update`, used by the in-game F5 updater handoff — exit code 0/1), or the interactive flow: checks GitHub for the latest mod version, and shows `UpdateAvailableForm` / `InstalledOptionsForm` / the full install chain depending on whether the mod is already installed and whether an update is available.

`RunFullInstallFlow` chains `WelcomeForm` -> `GameVersionSelectionForm` -> (if KOTOR 2 checked) `RunKotor2PreparationFlow` -> `ModdingInfoForm` -> `ModSelectionForm` -> `MainForm`, any of which can cancel the chain. `RunKotor2PreparationFlow` applies Lane's static KOTOR 2 engine patches (4GB + borderless fullscreen via `InstallationManager`), then `Kotor2ModSelectionForm` -> `RunTslrcmInstall` (silent Inno with visible-wizard fallback, verified via `TslrcmDetector`) -> optional `WorkshopTlkHarvestForm` (only if TSLRCM installed, non-English non-Unknown locale, and a Workshop item exists for it) -> `Kotor2ModsInstallForm` (K2CP/Tweak Pack, gated on TSLRCM presence) -> one summary MessageBox. Also owns `PerformUninstall` (removes patcher runtime files, patches/ folder, shipped Override assets, restores renamed intro movies, unregisters, schedules self-delete via a `ping`-then-`del` cmd trick since `timeout` needs an interactive console), game-path detection (registry -> Steam per-app key -> common paths -> ProgramFiles scan) for both KOTOR 1 (`DetectGamePath`) and KOTOR 2 (`DetectKotor2GamePath`), version comparison (`IsNewerVersion`/`NormalizeVersion`), and small report/toggle helpers (`CollectLogsAndReport`, `ToggleSpatialAudioAndReport`).

## Declarations (in source order)

- L13 — `public enum UpdateChoice` — Close, UpdateOnly, FullInstall, ToggleSpatialAudio, CollectLogs
- L22 — `static class Program`
- L24 — `public const string GameExeName = "swkotor.exe"`
- L28 — `public static readonly string DefaultGamePath` — Steam-only default; GoG/Aspyr out of scope for first pass
- L32 — `[STAThread] static void Main(string[] args)`
  note: `--auto-update` skips every dialog, assumes mod already installed, sets `Environment.ExitCode = 1` on failure
- L262 — `private static void RunFullInstallFlow(string gamePath, string pathArgOverride, string latestVersion, string localKpatchPath)`
- L347 — `private static void RunKotor2PreparationFlow(string k2Path)`
  note: TSLRCM readme buries the "old saves incompatible" warning in an English doc; spoken explicitly in the summary here
- L505 — `private static void RunTslrcmInstall(string k2Path)` — dispatches on `TslrcmOutcome`
- L565 — `private static string ApplyKotor2EnginePatches(string k2Path)`
  note: null result from `InstallationManager.ApplyKotor2StaticPatches` means exe hash doesn't match a declared build — reported as skip, not error
- L611 — `private static void RunTslrcmWizardInteractive(string installerExe, string statusLine)` — visible-wizard fallback, announces handoff first so an unannounced English wizard doesn't steal focus
- L646 — `private static void TryDeleteTempFile(string path)`
- L662 — `private static void WarnIfRussianTranslationMissing(string installerLanguage, GameLocale gameLocale)`
  note: guidance only, never blocks install — no official Russian release and the community translation isn't licensed for bundling
- L683 — `private static void CollectLogsAndReport(string gamePath)` — builds beta-test .7z/.zip via `LogCollector`, reveals in Explorer
- L719 — `private static void ToggleSpatialAudioAndReport(string gamePath)`
- L746 — `public static void PerformUninstall(string gamePath)`
- L825 — `private static void ScheduleUninstallerSelfDelete(string gamePath)`
  note: uses `cmd /c ping 127.0.0.1 -n 5 -w 1000 >nul & del ...` — `timeout` refuses non-interactive consoles
- L852 — `private static bool IsRunningAsAdmin()`
- L863 — `private static bool IsGameRunning()`
- L880 — `public static string DetectGamePath()` — registry -> Steam App 32370 key -> DefaultGamePath -> ProgramFiles(x86)/ProgramFiles scan
- L905 — `private static string TryReadSteamAppInstallPath()` — Steam App ID 32370
- L926 — `public static string DetectKotor2GamePath()`
- L946 — `private static string TryReadSteamKotor2InstallPath()` — Steam App ID 208580 (Aspyr build)
- L958 — `public static bool IsValidKotor2GamePath(string path)`
- L964 — `public static bool IsValidGamePath(string path)`
- L981 — `public static bool IsSteamPath(string gamePath)` — decides `steam://run/32370` vs. direct exe launch post-install
- L1001 — `internal static bool IsNewerVersion(string latestVersion, string installedVersion)`
- L1017 — `internal static Version NormalizeVersion(string version)` — strips leading v/V, trailing `-suffix` and anything after a space
