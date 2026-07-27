# ModSelection.cs (39 lines)

Immutable record of the user's per-category optional-mod selection, shared across both the KOTOR 1 (ModSelectionForm) and KOTOR 2 (Kotor2ModSelectionForm) flows. Each game's form forces the other game's flags to false. Always-installed components (accessibility mod, KotorPatcher runtime, Prism, widescreen) are not represented here since they're not user-selectable.

## Declarations (in source order)

- L9 — `public sealed class ModSelection`
- L12 — `bool K1cp { get; init; } = true` — KOTOR 1 Community Patch + locale patch
- L15 — `bool RestoredCutContent { get; init; } = true` — Juhani Dialogue Restoration + Party Conversations
  note: not yet wired to an installer as of this file; ModSelectionForm forces it false at Next-click time
- L18 — `bool CompanionAndSwoopUpgrades { get; init; } = true` — Thematic Companions + Swoop Bike Upgrades (also not yet wired)
- L24 — `bool K2cp { get; init; } = true` — set by Kotor2ModSelectionForm only
- L30 — `bool TweakPack { get; init; } = true` — set by Kotor2ModSelectionForm only
- L32 — `static ModSelection AllOn()`
- L34 — `override string ToString()`
