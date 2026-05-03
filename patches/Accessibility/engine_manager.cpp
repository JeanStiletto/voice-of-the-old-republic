#include "engine_manager.h"

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // CExoArrayList, kPanelControlsOffset
#include "log.h"

namespace acc::engine {

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
        int n = list->size > 32 ? 32 : list->size;
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
        return panelData[panelSize - 1];
    }
    return nullptr;
}

void LogManagerStack(void* mgr, const char* tag) {
    if (!mgr) {
        acclog::Write("ManagerStack(%s): mgr=NULL", tag ? tag : "?");
        return;
    }
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelSize  = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    int   modalSize  = *reinterpret_cast<int*>(base + kMgrModalStackSizeOffset);
    void** modalData = *reinterpret_cast<void***>(base + kMgrModalStackDataOffset);
    void* fg = GetForegroundPanel(mgr);
    acclog::Write("ManagerStack(%s) mgr=%p panels.size=%d modal.size=%d fg=%p",
                  tag ? tag : "?", mgr, panelSize, modalSize, fg);
    int pn = panelSize;
    if (pn < 0 || pn > 32) pn = (pn < 0) ? 0 : 32;
    for (int i = 0; i < pn && panelData; ++i) {
        acclog::Write("ManagerStack(%s)   panels[%d]=%p", tag ? tag : "?",
                      i, panelData[i]);
    }
    int mn = modalSize;
    if (mn < 0 || mn > 32) mn = (mn < 0) ? 0 : 32;
    for (int i = 0; i < mn && modalData; ++i) {
        acclog::Write("ManagerStack(%s)   modal[%d]=%p", tag ? tag : "?",
                      i, modalData[i]);
    }
}

}  // namespace acc::engine
