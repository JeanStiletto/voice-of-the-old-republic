# TslrcmInstallForm.cs (377 lines)

WinForms dialog that downloads the TSLRCM installer exe from DeadlyStream via `DeadlyStreamClient` (guest scrape), verifies it against `Config.TslrcmInstallerSha256` (fail-closed — mismatch routes the user to a manual download rather than running an unverified exe), and — when the KOTOR 2 folder is known — runs it silently with Inno Setup's `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /DIR=...`. Silent is the default because the TSLRCM wizard itself is English-only while this installer speaks the user's language; `TslrcmOutcome` enum tells `Program.RunTslrcmInstall` which of 5 states resulted so it can offer the interactive wizard as fallback.

Verification gotcha: exit code 0 alone isn't trusted — `FingerprintDialogTlk()` (size+mtime) is captured before and after the silent run, and if dialog.tlk is unchanged the run is treated as a failure even though Inno reported success. Cancel is only honored during download; mid-install cancellation is blocked in `FormClosing` to avoid a half-written mod. Status updates raise `RaiseAutomationNotification` for screen-reader announcement (download every 25%, install heartbeat every ~15s).

## Declarations (in source order)

- L13 — `public enum TslrcmOutcome` — Cancelled, DownloadFailed, DownloadedOnly, SilentInstallFailed, SilentInstalled
- L58 — `public class TslrcmInstallForm : Form`
- L80 — `public TslrcmInstallForm(string k2GamePath)`
  note: `k2GamePath` null means detection failed; form stops after download+verify (`DownloadedOnly`)
- L100 — `private void InitializeComponents()`
- L154 — `private async Task RunAsync()` — download -> verify SHA-256 -> (if k2Path known) silent install
- L225 — `private async Task RunSilentInstallAsync(string installerExe)`
  note: switches progress bar to Marquee (duration unknown); fingerprints dialog.tlk before/after to catch a silent no-op
- L294 — `private string FingerprintDialogTlk()` — size:mtime-ticks string; probe errors return a never-equal GUID string rather than failing the install
- L314 — `private void OnProgress(long done, long total)` — coalesces byte progress to whole-percent UI updates, announces only every 25%
- L345 — `private void UpdateStatus(string message, bool announce)` — marshals to UI thread; raises UIA notification when announce=true
- L370 — `private static void TryDelete(string path)`
