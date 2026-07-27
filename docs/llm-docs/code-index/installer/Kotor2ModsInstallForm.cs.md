# Kotor2ModsInstallForm.cs (155 lines)

Progress-bar dialog that runs the KOTOR 2 mod pipeline (K2CP then Tweak Pack, via `ModInstallerCoordinator.BuildKotor2Pipeline`) against the KOTOR 2 install directory. Downloads HoloPatcher first (same provider as the KOTOR 1 flow), then calls `ModInstallerCoordinator.InstallSelectedAsync`. No cancel button — aborting HoloPatcher mid-write could leave a half-patched Override, and `HoloPatcherRunner`'s 10-minute timeout bounds every run, so `FormClosing` refuses to close until `_finished` flips. Caller is responsible for gating on TSLRCM already being installed. Talks to GitHubClient, HoloPatcherProvider, ModInstallerCoordinator, ModSelection.

## Declarations (in source order)

- L22 — `public class Kotor2ModsInstallForm : Form`
- L33 — `List<ModInstallResult> Results { get; private set; }` — per-mod results in pipeline order
- L35 — `Kotor2ModsInstallForm(string k2GamePath, ModSelection selection)` — wires `Shown += RunAsync`, FormClosing refuses mid-install
- L49 — `void InitializeComponents()` — title/status labels + progress bar, no Cancel/Close button (ControlBox = false)
- L87 — `Task RunAsync()` — downloads HoloPatcher (0-10% progress), runs the pipeline (10-100%), catches exceptions into a failed `ModInstallResult`
- L127 — `void UpdateProgress(int value)` — marshals via BeginInvoke
- L133 — `void UpdateStatus(string message)` — marshals, logs, and raises a UIA `RaiseAutomationNotification` on the status label so NVDA/JAWS speak each update (same live-region workaround as MainForm)
