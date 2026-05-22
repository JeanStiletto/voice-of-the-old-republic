// KOTOR Accessibility — virtual credits row (Inventory + Store).
//
// See menus_credits.h for the design summary. This file owns the per-kind
// anchor table and the read+format path.

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "menus_credits.h"

#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "strings.h"

namespace acc::menus::credits {

namespace {

// CSWGuiInGameInventory.credits_value_label offset — derived from Lane's
// struct DB (swkotor.exe.h): panel (0..0x64) + 3 CSWGuiLabels (each 0x140)
// = 0x64 + 0x3C0 = 0x424. The four labels in order are item_description_
// label, inventory_label, credits_label, credits_value_label.
constexpr size_t kInventoryCreditsValueLabelOffset = 0x424;

// CSWGuiStore.credits_value_label offset — already documented in
// engine_offsets.h (kStoreCreditsValueLabelOffset = 0x1200). Re-aliased here
// so both anchors live in one table.
constexpr size_t kStoreCreditsValueLabelOffsetLocal =
    kStoreCreditsValueLabelOffset;

struct CreditsAnchorSpec {
    acc::engine::PanelKind kind;
    size_t                 valueOffset;
};

constexpr CreditsAnchorSpec k_anchors[] = {
    { acc::engine::PanelKind::InGameInventory, kInventoryCreditsValueLabelOffset      },
    { acc::engine::PanelKind::Store,           kStoreCreditsValueLabelOffsetLocal     },
};
constexpr int k_anchorCount = static_cast<int>(
    sizeof(k_anchors) / sizeof(k_anchors[0]));

const CreditsAnchorSpec* FindSpecForPanel(void* panel) {
    if (!panel) return nullptr;
    auto kind = acc::engine::IdentifyPanel(panel);
    for (int i = 0; i < k_anchorCount; ++i) {
        if (k_anchors[i].kind == kind) return &k_anchors[i];
    }
    return nullptr;
}

}  // namespace

bool IsCreditsRowAnchor(void* panel, void* labelControl) {
    const CreditsAnchorSpec* spec = FindSpecForPanel(panel);
    if (!spec || !labelControl) return false;
    auto* expected = reinterpret_cast<unsigned char*>(panel) + spec->valueOffset;
    return labelControl == expected;
}

void ForEachCreditsRowAnchor(void* panel,
                             bool (*callback)(void* labelControl, int sortCy,
                                              void* userData),
                             void* userData) {
    if (!panel || !callback) return;
    const CreditsAnchorSpec* spec = FindSpecForPanel(panel);
    if (!spec) return;
    auto* label = reinterpret_cast<unsigned char*>(panel) + spec->valueOffset;
    // sortCy=1 lands the credits row at the very top of the chain — above
    // every real button (Inventory: exit/useitem/switch at cy 350+; Store:
    // cancel/examine/accept at cy 350+). User hears "Credits: N" first on
    // chain Down from the panel-open opener, then can keep Down-ing into
    // the listbox.
    callback(label, /*sortCy=*/1, userData);
}

bool ExtractCreditsRow(void* panel, void* labelControl,
                       char* outBuf, size_t bufSize) {
    if (!labelControl || bufSize == 0) return false;
    if (!IsCreditsRowAnchor(panel, labelControl)) return false;

    char value[32];
    value[0] = '\0';
    __try {
        if (!acc::engine::ReadGuiString(labelControl,
                                        kLabelGuiStringPtrOffset,
                                        value, sizeof(value))) {
            // Fall back to the inline CExoString / strref path. On a freshly
            // opened panel before the engine's populate writes gui_string,
            // CExoString still carries the .gui-load placeholder ("9999999");
            // we treat that as empty below.
            acc::engine::ExtractTextOrStrRefIndirect(
                labelControl, kLabelTextOffset, kLabelStrRefOffset,
                kLabelTextObjectOffset, value, sizeof(value));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (value[0] == '\0') return false;

    snprintf(outBuf, bufSize, acc::strings::Get(acc::strings::Id::FmtCredits),
             value);
    return true;
}

}  // namespace acc::menus::credits
