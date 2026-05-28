// 2D grid navigation primitives shared by the two CSWGuiSkillFlow-shaped
// menus (chargen feats grid, powers level-up grid). Each panel's row data
// lives in its own TU with a per-panel "empty cell" sentinel (featId vs
// powerId @ 0xffff); callers pass that cell predicate as a function
// pointer so the scan loops only have to be written once.
//
// Column count is kSkillFlowColumnsPerRow from engine_offsets.h.

#pragma once

namespace acc::menus::skillflow_nav {

// Cell predicate. True if (row, col) is a filled cell on the caller's
// chart. Row range is the caller's chart row count (NOT including the
// trailing button rows — those have their own per-panel handling).
using IsFilledFn = bool (*)(int row, int col);

// First filled column in chart row `row`, or -1 if none.
int FirstFilledCol(int row, IsFilledFn isFilled);

// Pick the column closest to `want` in chart row `row` among filled
// columns. Used by vertical nav to preserve the cursor's column when
// entering a new chart row, falling back to the nearest filled cell.
// Returns FirstFilledCol(row) on miss.
int NearestFilledCol(int row, int want, IsFilledFn isFilled);

}  // namespace acc::menus::skillflow_nav
