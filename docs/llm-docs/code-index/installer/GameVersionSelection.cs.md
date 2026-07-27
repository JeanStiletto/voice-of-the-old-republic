# GameVersionSelection.cs (19 lines)

Tiny immutable result record for GameVersionSelectionForm: which game(s) the installer should target. KOTOR 1 defaults true (fully supported), KOTOR 2 defaults false (in preparation).

## Declarations (in source order)

- L8 — `public sealed class GameVersionSelection`
- L11 — `bool Kotor1 { get; init; } = true`
- L14 — `bool Kotor2 { get; init; } = false`
- L16 — `override string ToString()`
