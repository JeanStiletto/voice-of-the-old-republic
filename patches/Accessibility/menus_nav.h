// Cursor stepping shared by the standalone screen navigators.
//
// Header-only: one integer function with no engine access, so it needs no
// TU of its own. Lives here rather than in menus_internal.h, which declares
// itself a private contract between menus.cpp and menus_extract.cpp.

#pragma once

namespace acc::menus::nav {

// Step `cur` by one (Up/Down) or jump to an end (Home/End), clamped to
// [0, count-1]. Never wraps.
//
// The no-wrap part is a deliberate project convention, not an accident of
// how the first screen was written: hitting an end re-announces the same
// row, and that repeated label IS the boundary cue for a screen-reader
// user. Wrapping silently teleports the cursor to the far end with nothing
// to mark the jump. Every screen that grows arrow navigation should call
// this rather than hand-rolling the ladder — three of them had already
// written out the same nine lines.
//
// Callers guard with `if (up || down || home || end)`; with all four false
// the cursor stays put (clamped). `count <= 0` yields 0.
inline int ClampedCursorStep(int cur, int count,
                             bool up, bool down, bool home, bool end) {
    if (count <= 0) return 0;
    int ni = cur;
    if      (up)   ni = cur - 1;
    else if (down) ni = cur + 1;
    else if (home) ni = 0;
    else if (end)  ni = count - 1;
    if (ni < 0) ni = 0;
    if (ni >= count) ni = count - 1;
    return ni;
}

}  // namespace acc::menus::nav
