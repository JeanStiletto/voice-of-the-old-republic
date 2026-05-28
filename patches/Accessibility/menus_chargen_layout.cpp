// See menus_chargen_layout.h for purpose.

#include "menus_chargen_layout.h"

#include <windows.h>
#include <cstddef>

#include "engine_offsets.h"  // kCSWGuiButtonSize, kControlExtentOffset

namespace acc::menus::chargen_layout {

bool IsPanelOfVtable(void* panel, uintptr_t expectedVtable) {
    if (!panel) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(vt) == expectedVtable;
}

int IndexFromButton(void* panel, void* control,
                    size_t buttonsArrayOffset, int maxCount) {
    if (!panel || !control) return -1;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    auto* btn  = reinterpret_cast<unsigned char*>(control);
    ptrdiff_t off = btn - base;
    if (off < (ptrdiff_t)buttonsArrayOffset) return -1;
    ptrdiff_t rel = off - (ptrdiff_t)buttonsArrayOffset;
    if (rel % (ptrdiff_t)kCSWGuiButtonSize != 0) return -1;
    int i = (int)(rel / (ptrdiff_t)kCSWGuiButtonSize);
    if (i < 0 || i >= maxCount) return -1;
    return i;
}

int RowPitchFromButtonExtents(void* panel, size_t buttonsArrayOffset) {
    if (!panel) return 0;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    int top0 = 0, top1 = 0;
    __try {
        // CSWGuiControl extent: { left, top, width, height } as four ints
        // starting at +kControlExtentOffset. Second int is `top`.
        auto* ext0 = reinterpret_cast<int*>(
            base + buttonsArrayOffset + kControlExtentOffset);
        auto* ext1 = reinterpret_cast<int*>(
            base + buttonsArrayOffset + kCSWGuiButtonSize +
            kControlExtentOffset);
        top0 = ext0[1];
        top1 = ext1[1];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    int pitch = top1 - top0;
    if (pitch <= 0 || pitch > 100) return 0;
    return pitch;
}

}  // namespace acc::menus::chargen_layout
