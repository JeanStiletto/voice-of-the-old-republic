# menus_skillflow_nav.h (28 lines)

Shared header declaring the 2D grid navigation primitives used by
`menus_chargen_feats` and `menus_powers_levelup`. Column count comes from
`kSkillFlowColumnsPerRow` in `engine_offsets.h`; each panel supplies its own
"empty cell" sentinel via the `IsFilledFn` predicate rather than sharing row
storage.

## Declarations (in source order)

- L16 — `using IsFilledFn = bool (*)(int row, int col)`
- L19 — `int FirstFilledCol(int row, IsFilledFn isFilled)`
- L25 — `int NearestFilledCol(int row, int want, IsFilledFn isFilled)` — used by vertical nav to preserve cursor column across rows
