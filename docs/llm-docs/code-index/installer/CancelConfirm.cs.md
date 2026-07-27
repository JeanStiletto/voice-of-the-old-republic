# CancelConfirm.cs (34 lines)

Static helper wrapping the "Are you sure you want to cancel?" confirmation MessageBox shown by every installer form's FormClosing handler (welcome/game-version/mod-selection/ModdingInfo/MainForm) so the X button or a Cancel/Back button never silently kills the install. Default button is No so an absent-minded Enter does not abort. Uses InstallerLocale for the two strings.

## Declarations (in source order)

- L11 — `internal static class CancelConfirm`
- L21 — `public static bool ConfirmCancel(IWin32Window owner = null)`
  note: returns true only when user clicks Yes; MessageBoxDefaultButton.Button2 (No) is the default
