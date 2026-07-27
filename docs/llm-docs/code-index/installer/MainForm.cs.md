# MainForm.cs (651 lines)

The primary install/update driver form: path picker + progress bar + status label, running the full pipeline (download .kpatch -> stage runtime -> ApplyKPatch -> install loader/Prism/Override-assets/priority-group -> swkotor.ini stability + keymap tweaks -> disable intro movies -> optional-mod pipeline via ModInstallerCoordinator -> copy uninstaller -> registry -> completion). Supports a `--headless` mode used by the in-game auto-updater (F5 in-game -> UAC -> this form auto-triggers install on Shown, skips confirmation/success dialogs and the readme/launch checkboxes since the calling batch script owns relaunch, and sets `Environment.ExitCode = 1` on failure for the batch to detect) and a `--local-kpatch <path>` dev override that skips the GitHub download entirely. `UpdateStatus` raises a UIA `RaiseAutomationNotification` on the status label (WinForms Labels are not live regions, so plain `.Text` changes are invisible to NVDA/JAWS/Narrator until the user navigates to them) with `MostRecent` processing so a fresh update interrupts a pending one. Talks to InstallationManager, GitHubClient, ModInstallerCoordinator/HoloPatcherProvider, IntroMovieDisabler, SwkotorIniTweaker, RegistryManager, Logger, CancelConfirm — effectively the hub every other class in this batch feeds into.

## Declarations (in source order)

- L14 — `public class MainForm : Form`
- L35 — `MainForm(string detectedGamePath, bool updateOnly, string language, ModSelection modSelection, string localKpatchPath, bool headless)`
  note: null modSelection means the update-only path (no re-prompt for optional mods on a kpatch update)
- L54 — `override void OnShown(EventArgs e)` — headless auto-triggers InstallButton_Click
- L64 — `void InitializeComponents()` — builds all controls; headless forces launchCheckBox off and readmeCheckBox off
- L181 — `void BrowseButton_Click(...)` — FolderBrowserDialog
- L201 — `void ValidatePath()` — enables Install button only for a valid game path
- L218 — `async void InstallButton_Click(object sender, EventArgs e)` — the full install pipeline, ~30 numbered steps with try/catch/finally cleanup
  note: widescreen/K1CP/etc failures are individually best-effort (logged as warnings) so a partial failure doesn't roll back the whole install; only kpatch-apply failure and unhandled exceptions abort
- L546 — `void UpdateStatus(string message)` — marshals via Invoke, logs, raises UIA automation notification (screen-reader speech path)
- L576 — `void UpdateProgress(int value)`
- L582 — `void SetControlsEnabled(bool enabled)`
- L591 — `static bool AnyOptionalSelected(ModSelection selection)`
- L594 — `void OpenReadme()` — opens localized README doc page via Config.ModSiteUrl
- L611 — `void LaunchGame()` — prefers `steam://run/32370` only when `_gamePath` matches Steam's registered install (Program.IsSteamPath), else launches the exe directly
