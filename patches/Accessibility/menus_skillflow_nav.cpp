#include "menus_skillflow_nav.h"

#include "engine_offsets.h"  // kSkillFlowColumnsPerRow

namespace acc::menus::skillflow_nav {

int FirstFilledCol(int row, IsFilledFn isFilled) {
    for (int c = 0; c < kSkillFlowColumnsPerRow; ++c) {
        if (isFilled(row, c)) return c;
    }
    return -1;
}

int NearestFilledCol(int row, int want, IsFilledFn isFilled) {
    if (isFilled(row, want)) return want;
    for (int d = 1; d < kSkillFlowColumnsPerRow; ++d) {
        if (isFilled(row, want - d)) return want - d;
        if (isFilled(row, want + d)) return want + d;
    }
    return FirstFilledCol(row, isFilled);
}

}  // namespace acc::menus::skillflow_nav
