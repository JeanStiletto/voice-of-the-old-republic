#include "engine_manager.h"

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // CExoArrayList, kPanelControlsOffset
#include "engine_panels.h"   // PanelKind, IdentifyPanel
#include "log.h"

namespace acc::engine {

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
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (panelData && panelCount > 0) {
        int n = panelCount > 32 ? 32 : panelCount;
        for (int i = 0; i < n; ++i) {
            if (panelData[i] == panel) return true;
        }
    }
    int   modalSize = *reinterpret_cast<int*>(base + kMgrModalStackSizeOffset);
    void** modalData = *reinterpret_cast<void***>(base + kMgrModalStackDataOffset);
    if (modalData && modalSize > 0) {
        int n = modalSize > 32 ? 32 : modalSize;
        for (int i = 0; i < n; ++i) {
            if (modalData[i] == panel) return true;
        }
    }
    return false;
}

void* FindOwningPanel(void* control) {
    if (!control) return nullptr;
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return nullptr;
    if (panelCount > 16) panelCount = 16;
    for (int i = 0; i < panelCount; ++i) {
        void* p = panelData[i];
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

void* GetForegroundPanel(void* mgr) {
    if (!mgr) return nullptr;
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
    return nullptr;
}

void LogManagerStack(void* mgr, const char* tag) {
    const char* ctx = tag ? tag : "?";
    if (!mgr) {
        acclog::Write("ManagerStack", "(%s) mgr=NULL", ctx);
        return;
    }
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelSize  = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    int   modalSize  = *reinterpret_cast<int*>(base + kMgrModalStackSizeOffset);
    void** modalData = *reinterpret_cast<void***>(base + kMgrModalStackDataOffset);
    void* fg = GetForegroundPanel(mgr);
    // One block per dump: an unchanged stack (same ctx, same panel/modal
    // pointers) folds to a "(repeated Nx)" summary. Here the pointers ARE the
    // state we care about, so identity is the full display text (no Key()) —
    // any pointer change is a real stack change and re-prints in full.
    acclog::BlockLog block("ManagerStack");
    block.Line("(%s) mgr=%p panels.size=%d modal.size=%d fg=%p",
               ctx, mgr, panelSize, modalSize, fg);
    int pn = panelSize;
    if (pn < 0 || pn > 32) pn = (pn < 0) ? 0 : 32;
    for (int i = 0; i < pn && panelData; ++i) {
        block.Line("(%s)   panels[%d]=%p", ctx, i, panelData[i]);
    }
    int mn = modalSize;
    if (mn < 0 || mn > 32) mn = (mn < 0) ? 0 : 32;
    for (int i = 0; i < mn && modalData; ++i) {
        block.Line("(%s)   modal[%d]=%p", ctx, i, modalData[i]);
    }
}

}  // namespace acc::engine
