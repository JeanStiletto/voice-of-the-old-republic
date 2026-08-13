// virtual credits row (Inventory + Store).
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
#include "menus_extract.h"    // FromControl — the K2 bench heading label
#include "menus_internal.h"   // detail::FindControlById — by-id anchors
#include "strings.h"
#include "engine_offsets_select.h"

namespace acc::menus::credits {

namespace {

// CSWGuiInGameInventory.credits_value_label offset — derived from Lane's
// struct DB (swkotor.exe.h): panel (0..0x64) + 3 CSWGuiLabels (each 0x140)
// = 0x64 + 0x3C0 = 0x424. The four labels in order are item_description_
// label, inventory_label, credits_label, credits_value_label.
const size_t kInventoryCreditsValueLabelOffset = acc::off::Todo(0x424);

// CSWGuiStore.credits_value_label offset — already documented in
// engine_offsets.h (kStoreCreditsValueLabelOffset = 0x1200). Re-aliased here
// so both anchors live in one table.
const size_t kStoreCreditsValueLabelOffsetLocal =
    kStoreCreditsValueLabelOffset;

// K2's crafting benches spend their own currency — components at a workbench,
// chemicals at a lab station — and show the pool exactly where a store shows
// your credits: an LBL_CREDITS heading plus an LBL_CREDITS_VALUE number.
// Anchored by .gui id rather than struct offset, because the two screens
// (component_p / chemical_p) put the pair at different struct offsets while
// agreeing on the ids.
//
// The heading is read from the panel instead of using our own word: the game
// already labels it "Alle Komponenten" / "Alle Chemikalien" in the player's
// language, which is both correct per bench and free of a translation we
// would have to maintain in seven files.
constexpr size_t kAnchorByGuiId       = (size_t)-1;
constexpr int    kCraftPoolValueGuiId = 6;   // LBL_CREDITS_VALUE
constexpr int    kCraftPoolLabelGuiId = 7;   // LBL_CREDITS

struct CreditsAnchorSpec {
    acc::engine::PanelKind kind;
    size_t                 valueOffset;  // or kAnchorByGuiId
    int                    valueGuiId;   // used when valueOffset is by-id
    int                    headingGuiId; // -1 → the "Credits: %s" wording
};

const CreditsAnchorSpec k_anchors[] = {
    { acc::engine::PanelKind::InGameInventory, kInventoryCreditsValueLabelOffset, -1, -1 },
    { acc::engine::PanelKind::Store,           kStoreCreditsValueLabelOffsetLocal, -1, -1 },
    { acc::engine::PanelKind::WorkbenchCreateItem,    kAnchorByGuiId,
      kCraftPoolValueGuiId, kCraftPoolLabelGuiId },
    { acc::engine::PanelKind::WorkbenchCreateMedical, kAnchorByGuiId,
      kCraftPoolValueGuiId, kCraftPoolLabelGuiId },
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

// The value label this spec anchors on, or null.
void* AnchorLabel(void* panel, const CreditsAnchorSpec* spec) {
    if (!panel || !spec) return nullptr;
    if (spec->valueOffset == kAnchorByGuiId) {
        return acc::menus::detail::FindControlById(panel, spec->valueGuiId);
    }
    return acc::off::Ptr(panel, spec->valueOffset);
}

}  // namespace

bool IsCreditsRowAnchor(void* panel, void* labelControl) {
    const CreditsAnchorSpec* spec = FindSpecForPanel(panel);
    if (!spec || !labelControl) return false;
    return labelControl == AnchorLabel(panel, spec);
}

void ForEachCreditsRowAnchor(void* panel,
                             bool (*callback)(void* labelControl, int sortCy,
                                              void* userData),
                             void* userData) {
    if (!panel || !callback) return;
    const CreditsAnchorSpec* spec = FindSpecForPanel(panel);
    if (!spec) return;
    void* label = AnchorLabel(panel, spec);
    if (!label) return;
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
    const CreditsAnchorSpec* spec = FindSpecForPanel(panel);

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

    // Panels that carry their own heading label speak it verbatim
    // ("Alle Komponenten: 77"); the rest use our localised credits wording.
    if (spec && spec->headingGuiId >= 0) {
        void* heading = acc::menus::detail::FindControlById(
            panel, spec->headingGuiId);
        char headingText[64];
        headingText[0] = '\0';
        if (heading &&
            acc::menus::extract::FromControl(heading, headingText,
                                             sizeof(headingText), panel) &&
            headingText[0] != '\0') {
            snprintf(outBuf, bufSize, "%s: %s", headingText, value);
            return true;
        }
    }

    snprintf(outBuf, bufSize, acc::strings::Get(acc::strings::Id::FmtCredits),
             value);
    return true;
}

}  // namespace acc::menus::credits
