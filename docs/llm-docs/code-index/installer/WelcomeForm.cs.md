# WelcomeForm.cs (227 lines)

Two-page welcome dialog, the installer's first screen. Page 1: title/description,
detected-language combo (via LanguageDetector; changing it fires
InstallerLocale.SetLanguage so all forms re-localise live), latest mod version
label. Page 2: game purchase info with Steam/GoG store-page buttons. Sets
ProceedWithInstall for MainForm to continue. Accessibility: page switches focus
the primary button and mirror the page text into AccessibleDescription (form +
primary button) so screen readers announce the content; closing without
proceeding routes through CancelConfirm.

## Declarations (in source order)

- L8 — `public class WelcomeForm : Form`
- L11 — `const string KotorSteamPageUrl / KotorGogPageUrl`
  note: KOTOR 1 store pages, locale-independent
- L31 — `public bool ProceedWithInstall { get; private set; }`
- L32 — `public string SelectedLanguage { get; private set; }`
- L33 — `public string LatestModVersion { get; set; }`
- L35 — `public WelcomeForm()`
  note: subscribes InstallerLocale.OnLanguageChanged → ApplyLocale; unsubscribed in FormClosing
- L43 — `private void InitializeComponents()`
  note: FormClosing without ProceedWithInstall asks CancelConfirm.ConfirmCancel and can veto the close
- L173 — `private void ShowPage1()` / L181 — `private void ShowPage2()`
- L189 — `private void ApplyLocale()`
- L209 — `private void UpdateAccessibleDescription()`
  note: per-page body text copied to form and primary-button AccessibleDescription
