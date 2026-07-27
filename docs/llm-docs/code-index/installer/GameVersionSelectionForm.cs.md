# GameVersionSelectionForm.cs (178 lines)

WinForms wizard screen between the welcome dialog and ModdingInfoForm; asks which game(s) to target via two checkboxes (KOTOR 1 checked by default, KOTOR 2 unchecked). Next requires at least one box checked — pressing it with neither checked shows a plain announced MessageBox rather than silently disabling the button, so a screen-reader user always gets an explanation. Talks to CancelConfirm (FormClosing guard), InstallerLocale (all text + live language switching via OnLanguageChanged), and Config.DisplayName for the window title. Each checkbox's AccessibleName is composed as "label. description" so NVDA/JAWS announce both on focus (same pattern as ModSelectionForm).

## Declarations (in source order)

- L22 — `public class GameVersionSelectionForm : Form`
- L36 — `bool ProceedWithInstall { get; private set; }`
- L42 — `GameVersionSelection Selection { get; private set; }`
- L44 — `GameVersionSelectionForm()` — wires ApplyLocale, OnLanguageChanged, FormClosing cancel-confirm guard
- L61 — `void InitializeComponents()` — builds title/description labels, K1/K2 checkbox+description pairs, Back/Next buttons
  note: Next click validates at least one checkbox checked before setting Selection and closing
- L153 — `void ApplyLocale()` — pulls all text from InstallerLocale; composes checkbox AccessibleName = text + description
