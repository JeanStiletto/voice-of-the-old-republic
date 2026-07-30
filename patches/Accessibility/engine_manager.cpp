#include "engine_manager.h"

#include <windows.h>
#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // CExoArrayList, kPanelControlsOffset
#include "engine_panels.h"   // PanelKind, IdentifyPanel
#include "log.h"

namespace acc::engine {

void* GetGuiManager() {
    __try {
        return *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

namespace {

// Both arrays are CExoArrayList-shaped (data, size, capacity) and differ only
// in their base offsets, so one body serves panels[] and modal_stack.
int ReadPanelList(void* mgr, size_t dataOffset, size_t sizeOffset,
                  void** out, int maxEntries, int* outRawCount) {
    if (outRawCount) *outRawCount = 0;
    if (!mgr || !out || maxEntries <= 0) return 0;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(mgr);
        int    size = *reinterpret_cast<int*>(base + sizeOffset);
        void** data = *reinterpret_cast<void***>(base + dataOffset);
        if (outRawCount) *outRawCount = size;
        if (!data || size <= 0) return 0;
        int n = size > maxEntries ? maxEntries : size;
        for (int i = 0; i < n; ++i) out[i] = data[i];
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

}  // namespace

int ReadPanelArray(void* mgr, void** outPanels, int maxEntries,
                   int* outRawCount) {
    return ReadPanelList(mgr, kMgrPanelsDataOffset, kMgrPanelsSizeOffset,
                         outPanels, maxEntries, outRawCount);
}

int ReadModalStack(void* mgr, void** outModal, int maxEntries,
                   int* outRawCount) {
    return ReadPanelList(mgr, kMgrModalStackDataOffset, kMgrModalStackSizeOffset,
                         outModal, maxEntries, outRawCount);
}

// Panels that exist in the engine's panels[] for rendering reasons but never
// receive input. Picking the topmost panel for foreground purposes should
// see through them to whatever interactive panel sits underneath. Currently
// just Fade — the screen-fade-to-black overlay slot at CGuiInGame+0x6c.
// After an area transition the engine pushes Fade onto panels[] and never
// pops it; if a higher-priority panel later closes, Fade ends up as
// panels[top] and our fg routing would otherwise return it, causing input
// (notably Esc) to fall through to the engine's last-resort handler (the
// quit-confirm dialog). See docs/in-game-menu-stack-investigation notes
// in the patch-20260510-003647.log review.
static bool IsTransparentForegroundKind(PanelKind k) {
    switch (k) {
    case PanelKind::Fade:
        return true;
    default:
        return false;
    }
}

bool IsPanelInManager(void* panel) {
    if (!panel) return false;
    void* mgr = GetGuiManager();
    if (!mgr) return false;

    constexpr int kCap = 32;
    void* entries[kCap];
    int n = ReadPanelArray(mgr, entries, kCap);
    for (int i = 0; i < n; ++i) {
        if (entries[i] == panel) return true;
    }
    n = ReadModalStack(mgr, entries, kCap);
    for (int i = 0; i < n; ++i) {
        if (entries[i] == panel) return true;
    }
    return false;
}

void* FindOwningPanel(void* control) {
    if (!control) return nullptr;
    constexpr int kCap = 32;
    void* panels[kCap];
    int panelCount = ReadPanelArray(GetGuiManager(), panels, kCap);
    for (int i = 0; i < panelCount; ++i) {
        void* p = panels[i];
        if (!p) continue;
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(p) + kPanelControlsOffset);
        if (!list->data || list->size <= 0) continue;
        // CSWGuiInGameCharacter alone has 60+ children (47 labels +
        // lbl_goods[10] + 4 more labels + 8 buttons + slider + scene +
        // btn_3dchar). The 32-child cap was masking the panel from
        // AnnounceControl's owner-resolution path, which was forcing
        // perkind extraction to fall back to "control N" placeholders
        // for buttons at indices > 32. 256 matches the cap used by the
        // chain walker in menus_chain.cpp's RebindChain.
        int n = list->size > 256 ? 256 : list->size;
        for (int j = 0; j < n; ++j) {
            if (list->data[j] == control) return p;
        }
    }
    return nullptr;
}

// NOT migrated to ReadPanelArray, deliberately. The last-resort return below
// indexes panelData[panelSize - 1] using the RAW size, while the scan above it
// only covers the first 32 entries — so with a size > 32 the two disagree and
// the fallback reaches outside its own scan window. Copying into a bounded
// buffer would quietly change that. It is already guarded (this was one of the
// F2 fixes), so it is not part of the unguarded class B3b closes; the
// raw-size/window mismatch is recorded as a separate finding instead.
void* GetForegroundPanel(void* mgr) {
    if (!mgr) return nullptr;
    // SEH-guarded: the null checks below cannot catch a STALE manager or
    // panel pointer, which is what this array holds during teardown.
    __try {
        auto* base = reinterpret_cast<unsigned char*>(mgr);
        int   modalSize = *reinterpret_cast<int*>(base + kMgrModalStackSizeOffset);
        void** modalData = *reinterpret_cast<void***>(base + kMgrModalStackDataOffset);
        if (modalSize > 0 && modalData) {
            void* top = modalData[modalSize - 1];
            if (top) return top;
        }
        int   panelSize = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
        void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
        if (panelSize > 0 && panelData) {
            // Walk down from the top, skipping render-only kinds (Fade) so the
            // returned fg is the topmost panel that actually accepts input.
            // Bound the walk to a sane limit just in case panelSize is corrupt.
            int n = panelSize > 32 ? 32 : panelSize;
            for (int i = n - 1; i >= 0; --i) {
                void* p = panelData[i];
                if (!p) continue;
                if (IsTransparentForegroundKind(IdentifyPanel(p))) continue;
                return p;
            }
            // Every entry was either null or transparent — return the actual
            // top so callers still see *something* and can decide what to do.
            return panelData[panelSize - 1];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

void LogManagerStack(void* mgr, const char* tag) {
    const char* ctx = tag ? tag : "?";
    if (!mgr) {
        acclog::Write("ManagerStack", "(%s) mgr=NULL", ctx);
        return;
    }
    constexpr int kCap = 32;
    void* panels[kCap];
    void* modal[kCap];
    int panelSize = 0, modalSize = 0;
    int pn = ReadPanelArray(mgr, panels, kCap, &panelSize);
    int mn = ReadModalStack(mgr, modal, kCap, &modalSize);
    void* fg = GetForegroundPanel(mgr);
    // One block per dump: an unchanged stack (same ctx, same panel/modal
    // pointers) folds to a "(repeated Nx)" summary. Here the pointers ARE the
    // state we care about, so identity is the full display text (no Key()) —
    // any pointer change is a real stack change and re-prints in full.
    acclog::BlockLog block("ManagerStack");
    block.Line("(%s) mgr=%p panels.size=%d modal.size=%d fg=%p",
               ctx, mgr, panelSize, modalSize, fg);
    for (int i = 0; i < pn; ++i) {
        block.Line("(%s)   panels[%d]=%p", ctx, i, panels[i]);
    }
    for (int i = 0; i < mn; ++i) {
        block.Line("(%s)   modal[%d]=%p", ctx, i, modal[i]);
    }
}

}  // namespace acc::engine
