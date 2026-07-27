# ModSelectionForm.cs (245 lines)

KOTOR 1 optional-mod screen between ModdingInfoForm and MainForm. Originally scoped for three groupings (K1CP, restored cut content, companion/swoop upgrades) as standalone checkboxes, but only K1CP has a wired installer as of this version (`ModInstallers.ModInstallerCoordinator.BuildPipeline`) — the other two checkbox/description pairs are commented out in `InitializeComponents`/`ApplyLocale` rather than removed, so the code documents the intended-but-unshipped UI. Accepts an optional `GameLocale` (from `GameLocaleDetector`) and, when the install is a Russian community translation, folds two extra footnote paragraphs into the existing footnote (detection confirmation + the translation author's statement that K1CP is untested against it) rather than adding a separate dialog step. Same AccessibleName composition and CancelConfirm/InstallerLocale wiring pattern as GameVersionSelectionForm.

## Declarations (in source order)

- L20 — `public class ModSelectionForm : Form`
- L39 — `bool ProceedWithInstall { get; private set; }`
- L45 — `ModSelection Selection { get; private set; } = ModSelection.AllOn()`
- L55 — `readonly GameLocale _gameLocale`
- L57 — `ModSelectionForm(GameLocale gameLocale = GameLocale.Unknown)`
- L75 — `void InitializeComponents()` — K1CP checkbox+description live; cut-content and companions checkbox/description pairs commented out (L115-142)
  note: Next click forces RestoredCutContent, CompanionAndSwoopUpgrades, K2cp, TweakPack all false — re-enabling the commented toggles requires also removing this override, otherwise consent is silently bypassed
- L193 — `void ApplyLocale()` — pulls text; footnote built via `BuildFootnote()`
- L229 — `string BuildFootnote()` — appends Russian-specific detection + K1CP-untested paragraphs when `_gameLocale == GameLocale.Russian`
