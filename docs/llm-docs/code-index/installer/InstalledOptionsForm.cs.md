# InstalledOptionsForm.cs (97 lines)

Small WinForms dialog shown when the mod is already installed and up to date. Presents four buttons: full reinstall, toggle bundled dsoal spatial-audio layer, collect a beta-test log bundle (via LogCollector), or close. Shares the `UpdateChoice` enum with UpdateAvailableForm (not in this batch) so Program.cs can dispatch on a single type regardless of which dialog produced it. All text via InstallerLocale; body composed onto AccessibleDescription for screen-reader announcement on open.

## Declarations (in source order)

- L12 — `public class InstalledOptionsForm : Form`
- L14 — `UpdateChoice UserChoice { get; private set; } = UpdateChoice.Close`
- L16 — `InstalledOptionsForm(string installedVersion, bool spatialAudioEnabled)`
- L21 — `void InitializeComponents(string installedVersion, bool spatialAudioEnabled)` — builds title/body labels and 4 buttons (Reinstall, toggle spatial audio, Collect Logs, Close), each setting `UserChoice` then closing
  note: toggle button label flips between Enable/Disable based on current `spatialAudioEnabled` state
