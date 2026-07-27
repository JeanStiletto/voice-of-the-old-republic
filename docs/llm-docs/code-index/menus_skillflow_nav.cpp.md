# menus_skillflow_nav.cpp (24 lines)

Tiny shared implementation of the 2D grid-scan primitives used by both
CSWGuiSkillFlow-shaped menus (chargen feats grid, powers level-up grid). No
engine-specific reads — operates purely through the caller-supplied
`IsFilledFn` predicate.

## Declarations (in source order)

- L7-L12 — `int FirstFilledCol(int row, IsFilledFn isFilled)` — first filled column, or -1
- L14-L21 — `int NearestFilledCol(int row, int want, IsFilledFn isFilled)` — column nearest `want` among filled columns, expanding outward; falls back to `FirstFilledCol`
