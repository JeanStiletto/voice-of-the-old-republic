// Layout helpers shared by menus_chargen_attr + menus_chargen_skills.
//
// Both panels share the same N-button + N-label + N-plus + N-minus shape
// (only N differs: 6 for attributes, 8 for skills) and the same cursor-
// warp Y-shift quirk on the engine's hit-test. The three helpers below
// were near-byte-identical in both .cpp files; consolidating them keeps
// the public per-panel namespaces intact (callers use
// IsChargenAttributesPanel / AbilityIndexFromButton / RowPitchForCursorWarp
// unchanged) while collapsing the internal implementations.

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::menus::chargen_layout {

// True iff `panel`'s vtable equals `expectedVtable`. SEH-guarded.
bool IsPanelOfVtable(void* panel, uintptr_t expectedVtable);

// Index 0..maxCount-1 if `control` is one of the panel's N-button array
// starting at `buttonsArrayOffset`; -1 otherwise (not in range, not
// aligned to the stride kCSWGuiButtonSize, or out-of-bounds).
int IndexFromButton(void* panel, void* control,
                    size_t buttonsArrayOffset, int maxCount);

// Row pitch (~30-45 px) computed from buttons[0] / buttons[1] extents.
// Returns 0 on SEH fault or if the result is outside [1, 100]. Used by
// the chain-step click-sim to compensate for the engine's hit-test
// shift on these panels.
int RowPitchFromButtonExtents(void* panel, size_t buttonsArrayOffset);

}  // namespace acc::menus::chargen_layout
