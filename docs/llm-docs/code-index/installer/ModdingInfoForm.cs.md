# ModdingInfoForm.cs (151 lines)

Informational wizard screen between the welcome dialog and ModSelectionForm, listing components that are ALWAYS installed and not user-selectable (accessibility mod, Prism speech bridge, widescreen patches, ini tweaks) — as opposed to ModSelectionForm's optional groupings. Uses a single multi-line read-only TextBox instead of a stack of Labels specifically so a screen reader can navigate the content line-by-line with arrow keys; non-focusable Labels would otherwise force the user to fight the UI to read multi-paragraph content. Content is built section-by-section (heading + underline + body) via `BuildContent`/`AppendSection`.

## Declarations (in source order)

- L18 — `public class ModdingInfoForm : Form`
- L26 — `bool ProceedWithInstall { get; private set; }`
- L28 — `ModdingInfoForm()`
- L45 — `void InitializeComponents()` — title/description labels, read-only multiline `_contentBox`, Back/Next buttons
- L99 — `void ApplyLocale()` — pulls text, rebuilds `_contentBox.Text` via BuildContent(), mirrors title+description onto AccessibleDescription so NVDA reads it on open before focus reaches the content box
- L116 — `static string BuildContent()` — sections in read order: Accessibility mod, Prism, Widescreen, Ini Tweaks
- L141 — `static void AppendSection(StringBuilder sb, string heading, string body)` — heading + dashed underline + body + blank lines
