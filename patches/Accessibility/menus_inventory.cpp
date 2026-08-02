#include "menus_inventory.h"

#include "engine_offsets.h"
#include "engine_rebase.h"
#include "log.h"

#include <cstddef>
#include <cstdint>

#include <windows.h>

namespace acc::menus::inventory {

namespace {

typedef void (__thiscall* PFN_PanelThiscall)(void* panel);

// Embedded button objects live AT panel+offset (the ctor constructs them in
// place via &this->useitem_button etc.), so the chain captures &button == that
// address. Same identification the journal module uses for its Sort/Swap
// buttons — locale-independent, and immune to the caption changing under us
// (which the filter button's does on every activation).
bool IsButtonAtOffset(void* panel, void* control, std::size_t offset) {
    if (!panel || !control) return false;
    return reinterpret_cast<unsigned char*>(panel) + offset ==
           reinterpret_cast<unsigned char*>(control);
}

}  // namespace

bool IsUseItemButton(void* panel, void* control) {
    return IsButtonAtOffset(panel, control, kInventoryUseItemButtonOffset);
}

bool IsFilterButton(void* panel, void* control) {
    // KOTOR 2 replaced K1's single filter-CYCLE button with seven direct
    // filter buttons laid out one button-stride apart (BTN_ALL..BTN_QUESTS).
    // Any of them changes the filtered list, so all get the repopulate-
    // then-invalidate treatment.
    if (acc::game::IsKotor2()) {
        if (!panel || !control) return false;
        auto base = reinterpret_cast<uintptr_t>(panel);
        auto c    = reinterpret_cast<uintptr_t>(control);
        if (c < base + kInventoryFilterFirstButtonOffset ||
            c > base + kInventoryFilterLastButtonOffset) return false;
        return ((c - base) - kInventoryFilterFirstButtonOffset) %
                   kInventoryFilterButtonStride == 0;
    }
    return IsButtonAtOffset(panel, control, kInventoryFilterButtonOffset);
}

void SyncListBoxSelection(void* panel, void* row) {
    if (!panel || !row) return;
    if (!acc::addr::Ok(kAddrCSWGuiListBoxSetSelectedControl)) {
        acclog::Write("Menus.Inventory",
                      "SyncListBoxSelection skipped (unresolved on build %s)",
                      acc::addr::ActiveBuildName());
        return;
    }
    __try {
        void* lb = reinterpret_cast<unsigned char*>(panel) +
                   kInventoryItemListBoxOffset;
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
        if (!lbList || !lbList->data) return;

        int idx = -1;
        for (int i = 0; i < lbList->size; ++i) {
            if (lbList->data[i] == row) { idx = i; break; }
        }
        if (idx < 0) {
            // Row is not in item_listbox — chain focus is on something else
            // (or the list was rebuilt since the rebind). Leave the engine's
            // selection alone rather than guessing.
            acclog::Write("Menus.Inventory",
                          "SyncListBoxSelection: row=%p not in item_listbox "
                          "(size=%d) — selection untouched", row, lbList->size);
            return;
        }

        auto* selPtr = reinterpret_cast<short*>(
            reinterpret_cast<unsigned char*>(lb) + kListBoxSelectionIndexOffset);
        short before = *selPtr;
        auto setSel = reinterpret_cast<PFN_CSWGuiListBoxSetSelectedControl>(
            kAddrCSWGuiListBoxSetSelectedControl);
        // playSound=0: the row name was already announced on the chain step,
        // and the activate about to follow plays its own item sound.
        setSel(lb, idx, 0);
        acclog::Write("Menus.Inventory",
                      "SyncListBoxSelection: row=%p idx=%d sel %d->%d",
                      row, idx, (int)before, (int)*selPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Inventory",
                      "SyncListBoxSelection SEH (panel=%p row=%p)", panel, row);
    }
}

void ForceRepopulate(void* panel) {
    if (!panel) return;
    if (!acc::addr::Ok(kAddrInventoryPopulateItemListBox)) {
        acclog::Write("Menus.Inventory",
                      "ForceRepopulate skipped (unresolved on build %s)",
                      acc::addr::ActiveBuildName());
        return;
    }
    __try {
        auto fn = reinterpret_cast<PFN_PanelThiscall>(
            kAddrInventoryPopulateItemListBox);
        fn(panel);
        acclog::Write("Menus.Inventory",
                      "ForceRepopulate: PopulateItemListBox(panel=%p)", panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Inventory",
                      "ForceRepopulate SEH (panel=%p)", panel);
    }
}

}  // namespace acc::menus::inventory
