// KOTOR Accessibility — chargen attributes panel tweaks.
// See menus_chargen_attr.h for the design rationale.

#include <windows.h>
#include <cstdint>
#include <cstddef>

#include "menus_chargen_attr.h"

#include "engine_offsets.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_chain.h"
#include "menus_extract.h"

namespace acc::menus::chargen_attr {

bool IsChargenAttributesPanel(void* panel) {
    if (!panel) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiAbilitiesCharGen;
}

int AbilityIndexFromButton(void* panel, void* control) {
    if (!IsChargenAttributesPanel(panel) || !control) return -1;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    auto* btn  = reinterpret_cast<unsigned char*>(control);
    ptrdiff_t off = btn - base;
    if (off < (ptrdiff_t)kAbilitiesCharGenButtonsArrayOffset) {
        return -1;
    }
    ptrdiff_t rel = off - (ptrdiff_t)kAbilitiesCharGenButtonsArrayOffset;
    if (rel % (ptrdiff_t)kCSWGuiButtonSize != 0) return -1;
    int i = (int)(rel / (ptrdiff_t)kCSWGuiButtonSize);
    if (i < 0 || i >= kAbilitiesCharGenAbilityCount) return -1;
    return i;
}

void SyncSelectedAbilityFromChainFocus() {
    void* panel = acc::menus::chain::g_chainPanel;
    if (!IsChargenAttributesPanel(panel)) return;
    if (acc::menus::chain::g_chainIndex < 0 ||
        acc::menus::chain::g_chainIndex >= acc::menus::chain::g_chainCount) {
        return;
    }
    void* focused =
        acc::menus::chain::g_chain[acc::menus::chain::g_chainIndex].control;
    int idx = AbilityIndexFromButton(panel, focused);
    if (idx < 0) return;

    auto* base = reinterpret_cast<unsigned char*>(panel);
    int* slot = reinterpret_cast<int*>(
        base + kAbilitiesCharGenSelectedAbilityOffset);
    int prev = 0;
    __try {
        prev = *slot;
        if (prev != idx) {
            *slot = idx;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (prev != idx) {
        acclog::Write("Menus.ChargenAttr",
                      "selected_ability %d -> %d (focused=%p)",
                      prev, idx, focused);
    }
}

int RowPitchForCursorWarp(void* panel, void* control) {
    if (AbilityIndexFromButton(panel, control) < 0) return 0;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    int top0 = 0, top1 = 0;
    __try {
        // CSWGuiControl extent: { left, top, width, height } as four ints
        // starting at +kControlExtentOffset. We want top, which is the
        // second int (offset +0x4 within the extent struct).
        auto* ext0 = reinterpret_cast<int*>(
            base + kAbilitiesCharGenButtonsArrayOffset + kControlExtentOffset);
        auto* ext1 = reinterpret_cast<int*>(
            base + kAbilitiesCharGenButtonsArrayOffset + kCSWGuiButtonSize +
            kControlExtentOffset);
        top0 = ext0[1];
        top1 = ext1[1];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    int pitch = top1 - top0;
    // Sanity-bound: real pitch is ~45 px; reject anything outside a
    // plausible UI row spacing range to avoid garbage offsets if a
    // future build moves the layout.
    if (pitch <= 0 || pitch > 100) return 0;
    return pitch;
}

void CaptureLabelsIfApplicable(void* panel) {
    if (!IsChargenAttributesPanel(panel)) return;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    for (int i = 0; i < kAbilitiesCharGenAbilityCount; ++i) {
        void* labelCtl = base + kAbilitiesCharGenLabelsArrayOffset +
                         (size_t)i * kCSWGuiLabelSize;
        void* buttonCtl = base + kAbilitiesCharGenButtonsArrayOffset +
                          (size_t)i * kCSWGuiButtonSize;

        char text[64] = {0};
        bool got = false;
        __try {
            if (acc::engine::ReadGuiString(
                    labelCtl, kLabelGuiStringPtrOffset,
                    text, sizeof(text)) && text[0] != '\0') {
                got = true;
            } else if (acc::engine::ExtractTextOrStrRefIndirect(
                           labelCtl,
                           kLabelTextOffset,
                           kLabelStrRefOffset,
                           kLabelTextObjectOffset,
                           text, sizeof(text)) &&
                       text[0] != '\0') {
                got = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            got = false;
        }

        if (got) {
            acc::menus::extract::CaptureCycleCategory(buttonCtl, text);
            acclog::Write("Menus.ChargenAttr",
                          "capture[%d] button=%p label=\"%s\"",
                          i, buttonCtl, text);
        }
    }
}

}  // namespace acc::menus::chargen_attr
