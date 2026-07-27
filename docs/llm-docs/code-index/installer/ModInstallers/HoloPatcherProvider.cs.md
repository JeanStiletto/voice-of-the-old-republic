# HoloPatcherProvider.cs (118 lines)

Downloads and stages HoloPatcher.exe (from `Config.HoloPatcherRepositoryUrl` @ `Config.HoloPatcherPinnedTag`) at install time so the installer can headlessly drive TSLPatcher-style installs (K1CP, K2CP, Tweak Pack). Talks to `GitHubClient` for the release-asset download and `ZipFile` for extraction. LGPL-3.0 licensed upstream; attribution required (see README / About panel).

Gotcha: HoloPatcher v1.60-patcher-beta4 has an `is_running_from_temp` guard that rejects running from `%TEMP%`, so `StagingRoot` deliberately extracts under `%LOCALAPPDATA%\KotorAccessibility\holopatcher\` instead of the system temp dir.

## Declarations (in source order)

- L29 — `public static class HoloPatcherProvider`
- L36 — `private static string StagingRoot` — `%LOCALAPPDATA%\KotorAccessibility\holopatcher`
- L47 — `public static async Task<string> DownloadAsync(GitHubClient github, Action<int> progress = null)`
  note: returns null + logs warning on failure rather than throwing; caller surfaces as per-mod failure
- L85 — `private static string ExtractHoloPatcherExe(string zipPath, string targetDir)`
  note: throws if `Config.HoloPatcherExePathInsideZip` entry missing — signals upstream zip layout changed
- L99 — `public static void Cleanup(string holoPatcherExePath)` — best-effort recursive delete of the staging dir
