# Kotor2ModSelectionForm.cs (218 lines)

KOTOR 2 counterpart of ModSelectionForm, shown after engine patches are applied (their result is prepended into the description as `_statusLine`). Three checkboxes, all default-on: TSLRCM, K2CP, Tweak Pack. TSLRCM is not a `ModInstallers.IModInstaller` (it's a plain exe-installer run rather than a HoloPatcher payload), so it's exposed separately as `InstallTslrcm`; K2CP and Tweak Pack ride the shared `ModSelection` consumed by `ModInstallerCoordinator`. Talks to CancelConfirm, InstallerLocale, Config.TslrcmDownloadPageUrl (footnote). Same AccessibleName composition pattern (label + description) as ModSelectionForm/GameVersionSelectionForm.

## Declarations (in source order)

- L22 — `public class Kotor2ModSelectionForm : Form`
- L43 — `bool ProceedWithInstall { get; private set; }`
- L46 — `bool InstallTslrcm { get; private set; } = true`
- L53 — `ModSelection Selection { get; private set; }` — K1cp/RestoredCutContent/CompanionAndSwoopUpgrades forced false (KOTOR 1-only groupings)
- L60 — `Kotor2ModSelectionForm(string statusLine)` — statusLine prepended into the description
- L78 — `void InitializeComponents()` — builds TSLRCM/K2CP/TweakPack checkbox+description triples, footnote label, Back/Next
- L189 — `void ApplyLocale()` — pulls all text; footnote built from `K2Mods_Footnote_Format` + `Config.TslrcmDownloadPageUrl`
