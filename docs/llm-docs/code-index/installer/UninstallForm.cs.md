# UninstallForm.cs (152 lines)

WinForms dialog for standalone uninstall mode (`swkotor_installer.exe /uninstall`). Shows a confirm MessageBox, then runs `Program.PerformUninstall` on a background thread while updating a status label and progress bar. On success, offers `Logger.AskAndSave()`; on failure, offers `Logger.AskAndSave(alwaysAsk: true)` + `Logger.OpenLogFile()` so the user can grab logs for a bug report. Talks to `Program.PerformUninstall` (the actual file-removal logic lives there, not in this form).

Accessibility: `AccessibleDescription` is set on the form and the uninstall button to a concatenated heading+description+path string so a screen reader gets the full context on focus, matching the "first-sight = title + focus" pattern.

## Declarations (in source order)

- L8 — `public class UninstallForm : Form`
- L18 — `public UninstallForm(string gamePath)`
- L25 — `private void InitializeComponents()` — builds title/status/path labels, progress bar, Uninstall/Cancel buttons
- L92 — `private async void UninstallButton_Click(object sender, EventArgs e)`
  note: confirms via MessageBox first; disables buttons + shows progress bar during the async uninstall; re-enables controls on failure
- L144 — `private void UpdateStatus(string message)` — marshals to UI thread via `InvokeRequired`/`Invoke`
