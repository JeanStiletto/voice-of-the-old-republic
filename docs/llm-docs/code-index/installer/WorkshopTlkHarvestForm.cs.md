# WorkshopTlkHarvestForm.cs (291 lines)

WinForms dialog that fetches the localized TSLRCM `dialog.tlk` from the TSLRCM team's official per-language Steam Workshop items (German/French/Italian/Spanish/Russian item IDs hardcoded in `TryGetWorkshopItem`, verified 2026-07-27) and copies it over the English one the DeadlyStream TSLRCM 1.8.6 exe installs (that exe is English-only — verified by extraction, see docs/installer.md). Because Workshop items are UGC-depot hosted (no anonymous direct download via `GetPublishedFileDetails`), the flow opens the Workshop page via `steam://url/CommunityFilePage/<item>` and has the user click Subscribe manually — the one non-automatable step — then polls `workshop/content/<app>/<item>/` for a size-stable `dialog.tlk`, verifies the language via `GameLocaleDetector` (content-probe covers Russian, which reports the English language ID in its header), backs up the existing tlk as `dialog.tlk.english.bak`, and copies the localized one in.

Must run after TSLRCM but before K2CP / Tweak Pack (which append strrefs to dialog.tlk; replacing the file afterward would orphan them). Uses `RaiseAutomationNotification` on status-label updates for screen-reader announcements, same pattern as `TslrcmInstallForm`.

## Declarations (in source order)

- L38 — `public class WorkshopTlkHarvestForm : Form`
- L43 — `public static bool TryGetWorkshopItem(GameLocale locale, out string itemId)`
  note: hardcoded Workshop item IDs per locale (German 485551190, French 485553656, Italian 485556965, Spanish 485555217, Russian 2143250983)
- L58 — `private static readonly TimeSpan OverallTimeout = TimeSpan.FromMinutes(20)` — generous window for user to subscribe + Steam to pull ~335 MB
- L76 — `public WorkshopTlkHarvestForm(string k2GamePath, GameLocale expectedLocale, string itemId)`
- L91 — `private void InitializeComponents()` — title/status labels, marquee progress bar, Cancel button
- L144 — `private async Task RunAsync()`
  note: opens Workshop page, waits for stable file, verifies detected locale matches expected, backs up + replaces dialog.tlk
- L203 — `private async Task<string> WaitForWorkshopTlkAsync(string itemDir)`
  note: "stable" = two consecutive polls with the same size > 1 MB (screens out partial writes); announces heartbeat every ~15s
- L267 — `private void UpdateStatus(string message, bool announce)` — marshals to UI thread; raises UIA `AutomationNotificationKind.ActionCompleted` when announce=true
