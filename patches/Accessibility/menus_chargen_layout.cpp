// See menus_chargen_layout.h for purpose.

#include "menus_chargen_layout.h"

#include <windows.h>
#include <cstddef>
#include <cstring>

#include "engine_game.h"   // IsKotor2 — per-game cursor-warp hit-test shim
#include "engine_offsets.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_chain.h"
#include "menus_extract.h"
#include "prism.h"

namespace acc::menus::chargen_layout {

namespace {

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

}  // namespace

bool IsPanel(const PanelDesc& d, void* panel) {
    if (!panel) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(vt) == d.vtable;
}

int IndexFromButton(const PanelDesc& d, void* panel, void* control) {
    if (!IsPanel(d, panel)) return -1;
    if (!control) return -1;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    auto* btn  = reinterpret_cast<unsigned char*>(control);
    ptrdiff_t off = btn - base;
    if (off < (ptrdiff_t)d.buttonsOffset) return -1;
    ptrdiff_t rel = off - (ptrdiff_t)d.buttonsOffset;
    if (rel % (ptrdiff_t)kCSWGuiButtonSize != 0) return -1;
    int i = (int)(rel / (ptrdiff_t)kCSWGuiButtonSize);
    if (i < 0 || i >= d.count) return -1;
    return i;
}

// KOTOR 1 ONLY, same shape as the class-icon column shim in menus_chain.cpp:
// KOTOR 1's abchrgen/skchrgen hit-test resolves one row ABOVE the value
// button's own extent, so the warp has to aim a full row-pitch low or the
// engine's OnEnterPointsButton populates the description listbox for the
// wrong row. KOTOR 2 has no such shift — its warps land on the row's own
// LBL (which spans the value button) and every logged description matched
// the focused row (patch-20260803-095222.log).
//
// The K2 exit used to happen by accident, via the `pitch > 100` reject in
// RowPitchFromButtonExtents: K2 stretches its single 800x600 layout to the
// window, so the authored 41-unit pitch measures 123 px at 2880x1800. That
// is a bare pixel constant standing in for a per-game fact — at a window
// narrow enough to keep the pitch under 100 the compensation would have
// come back and shifted every K2 row by one. Gate on the game instead.
int RowPitchForCursorWarp(const PanelDesc& d, void* panel, void* control) {
    if (acc::game::IsKotor2()) return 0;
    if (IndexFromButton(d, panel, control) < 0) return 0;
    return RowPitchFromButtonExtents(panel, d.buttonsOffset);
}

void SyncSelectedFromChainFocus(const PanelDesc& d) {
    void* panel = acc::menus::chain::g_chainPanel;
    if (!IsPanel(d, panel)) return;
    if (acc::menus::chain::g_chainIndex < 0 ||
        acc::menus::chain::g_chainIndex >= acc::menus::chain::g_chainCount) {
        return;
    }
    void* focused =
        acc::menus::chain::g_chain[acc::menus::chain::g_chainIndex].control;
    int idx = IndexFromButton(d, panel, focused);
    if (idx < 0) return;

    auto* base = reinterpret_cast<unsigned char*>(panel);
    int* slot = reinterpret_cast<int*>(base + d.selectedOffset);
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
        acclog::Write(d.logTag, "%s %d -> %d (focused=%p)",
                      d.selectedFieldName, prev, idx, focused);
    }
}

void CaptureLabels(const PanelDesc& d, void* panel) {
    if (!IsPanel(d, panel)) return;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    for (int i = 0; i < d.count; ++i) {
        void* labelCtl = base + d.labelsOffset +
                         (size_t)i * kCSWGuiLabelSize;
        void* buttonCtl = base + d.buttonsOffset +
                          (size_t)i * kCSWGuiButtonSize;

        char text[64] = {0};
        if (!acc::engine::ReadLabelText(labelCtl, text, sizeof(text))) continue;

        acc::menus::extract::CaptureCycleCategory(buttonCtl, text);
        acclog::Write(d.logTag, "capture[%d] button=%p label=\"%s\"",
                      i, buttonCtl, text);
    }
}

bool AnnounceDescription(const PanelDesc& d, void* panel, void* control) {
    int idx = IndexFromButton(d, panel, control);
    if (idx < 0) return false;

    typedef void (__thiscall* PFN_OnEnter)(void* this_, void* btn);
    __try {
        auto fn = reinterpret_cast<PFN_OnEnter>(d.onEnterPointsButton);
        fn(panel, control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    // Read description_listbox.controls[0] — the row whose text the engine
    // just rewrote.
    auto* base = reinterpret_cast<unsigned char*>(panel);
    void* listBox = base + d.descListBoxOffset;
    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(listBox) + kListBoxControlsOffset);
    void* row = nullptr;
    __try {
        if (lbList && lbList->data && lbList->size > 0) {
            row = lbList->data[0];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!row) return false;

    // Same two-path text read the row names use, via the shared engine
    // helper. Both pre-merge copies spelled the fallback out inline; the
    // attributes copy also tested buf[0] afterwards instead of trusting the
    // return value, which would have spoken a partially-written buffer if a
    // read faulted mid-way. The helper's contract (false => nothing usable)
    // is the convention everywhere else and is what we follow here.
    char buf[1024];
    if (!acc::engine::ReadLabelText(row, buf, sizeof(buf))) return false;

    // Flatten embedded newlines for the single-line diagnostic log — the
    // engine renders "Attribut: <attr>.\n<body>", which would otherwise
    // hide the body. Speech gets the original text with its newlines.
    char dump[1024];
    {
        size_t n = strnlen(buf, sizeof(buf) - 1);
        if (n >= sizeof(dump)) n = sizeof(dump) - 1;
        for (size_t i = 0; i < n; ++i) {
            char c = buf[i];
            dump[i] = (c == '\n' || c == '\r') ? ' ' : c;
        }
        dump[n] = '\0';
    }

    prism::Speak(buf, /*interrupt=*/false);
    acclog::Write(d.logTag,
                  "chain-step description focus=%p idx=%d (first 300 chars: \"%.300s\")",
                  control, idx, dump);
    return true;
}

bool IsDescriptionListbox(const PanelDesc& d, void* listBox) {
    if (!listBox) return false;
    void* panel = acc::menus::chain::g_chainPanel;
    if (!IsPanel(d, panel)) return false;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    return listBox == reinterpret_cast<void*>(base + d.descListBoxOffset);
}

}  // namespace acc::menus::chargen_layout
