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
    return IsButtonAtOffset(panel, control, kInventoryFilterButtonOffset);
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
