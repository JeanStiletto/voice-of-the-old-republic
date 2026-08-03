#include "engine_panels.h"
#include "engine_panels_internal.h"
#include "engine_rebase.h"

#include <windows.h>  // SEH __try / __except
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine_game.h"     // IsKotor1 — the CGuiInGame slot walk is K1-only
#include "engine_manager.h"  // kAddrGuiManagerPtr, kMgrPanels*Offset, GetForegroundPanel
#include "engine_offsets.h"  // CExoArrayList, kPanelControlsOffset, kVtableListBox
#include "engine_reads.h"    // ReadGuiString
#include "log.h"

namespace acc::engine {

// Declared in engine_panels.h. Lives above the anonymous namespace so the
// detectors below AND the menus layer can both reach it — see the header
// for what it is for.
bool HasVtable(void* obj, uintptr_t expected) {
    if (!obj) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(obj);
        return reinterpret_cast<uintptr_t>(vt) == expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// CSWGuiSaveLoad structural signature. The panel is allocated dynamically
// when the user activates load/save and has no slot in CGuiInGame, so the
// offset table can't catch it — we detect it by the .gui-time control IDs
// that saveload.gui declares. IDs are baked into the resource at build
// time, language-independent, and identical between save and load contexts
// (both render through the same CSWGuiSaveLoad layout).
//
// Mirror of acc::menus::detail::IsSaveLoadPanel — kept here so engine-layer
// IdentifyPanel doesn't reach back into the menus layer. The two should
// stay in sync; if either set of IDs changes both must be updated.
namespace {

void* FindControlByGuiId(void* panel, int id) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 64 ? 64 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        int cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(c) + kControlIdOffset);
        if (cid == id) return c;
    }
    return nullptr;
}

bool IsSaveLoadStructural(void* panel) {
    if (!panel) return false;
    // SEH wrap mirrors IsLevelUpStructural below. IdentifyPanel runs the
    // structural detectors on any slot-table miss; during Annehmen on
    // InGameLevelUp the engine destroys the panel synchronously inside the
    // FireActivate vtable[15] dispatch and re-enters our hooks (or a
    // tick-level helper like GetForegroundPanel) with a stale or
    // mid-mutation pointer. The deref of panel.controls (offset 0x20)
    // inside FindControlByGuiId then AVs (crash analysed 2026-05-21,
    // dump swkotor.exe.14400.dmp, edi=0xa508ac00).
    constexpr int kIdGamesListbox  =  0;
    constexpr int kIdDeleteButton  = 11;
    constexpr int kIdBackButton    = 12;
    constexpr int kIdSaveLoadButton = 14;
    __try {
        void* lb = FindControlByGuiId(panel, kIdGamesListbox);
        if (!lb) return false;
        void** lbVtable = *reinterpret_cast<void***>(lb);
        if (reinterpret_cast<uintptr_t>(lbVtable) != kVtableListBox) return false;
        // Tighten: require IDs 11/12/14 to be actual CSWGuiButtons. The
        // workbench upgrade panel (upgrade.gui) coincidentally has the
        // same {0, 11, 12, 14} ID quartet, but its ID 11 is LBL_UPGRADE44
        // (a LabelHilight), not a Button. Without this check the workbench
        // upgrade panel false-matches as SaveLoad and the SaveLoad input
        // handler hijacks all keys (Enter → ID 14 / Esc → ID 12),
        // breaking the panel — see patch-20260521-175339.log analysis.
        void* del  = FindControlByGuiId(panel, kIdDeleteButton);
        void* back = FindControlByGuiId(panel, kIdBackButton);
        void* sl   = FindControlByGuiId(panel, kIdSaveLoadButton);
        return HasVtable(del,  kVtableCSWGuiButton) &&
               HasVtable(back, kVtableCSWGuiButton) &&
               HasVtable(sl,   kVtableCSWGuiButton);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Workbench upgrade panel (upgrade.gui). 29 controls; uniquely identifiable
// by the 7 BTN_UPGRADE3X/4X slot buttons at .gui IDs 12..18 — all standard
// CSWGuiButtons — plus the BTN_ASSEMBLE button at ID 24. ID 11 is the
// LBL_UPGRADE44 LabelHilight (NOT a button), which is what disambiguates
// this panel from SaveLoad.
// K2 upgrade_p.gui RE-NUMBERS everything (mined from its own gui.bif):
// BTN_ASSEMBLE is id 11, the normal slot buttons BTN_UPGRADE31/32/33 are
// ids 7/8/6, the saber bank BTN_UPGRADE31_LS..36_LS ids 17/18/19/23/24/25,
// BTN_BACK id 13. The probes below therefore go per game — K1's id-15 probe
// hits LBL_UPGRADE32_LS (a label) on K2 and the panel silently never
// identified there.
bool IsWorkbenchUpgradeStructural(void* panel) {
    if (!panel) return false;
    __try {
        // Quick coarse check: the panel needs a listbox at ID 0 (LB_ITEMS).
        // Workbench items go here; if missing this isn't the upgrade panel.
        void* lb = FindControlByGuiId(panel, /*LB_ITEMS=*/0);
        if (!lb) return false;
        void** lbVtable = *reinterpret_cast<void***>(lb);
        if (reinterpret_cast<uintptr_t>(lbVtable) != kVtableListBox) return false;
        // Probe BTN_ASSEMBLE and one slot button for the standard
        // CSWGuiButton vtable, at each game's own ids. Two ID hits +
        // vtable checks is enough to disambiguate from every other
        // heap-allocated listbox-at-0 panel we've seen.
        const bool k2 = acc::game::IsKotor2();
        void* assemble = FindControlByGuiId(panel, k2 ? 11 : 24);  // BTN_ASSEMBLE
        void* slot     = FindControlByGuiId(panel, k2 ? 7 : 15);   // BTN_UPGRADE31 / BTN_UPGRADE41
        return HasVtable(assemble, kVtableCSWGuiButton) &&
               HasVtable(slot,     kVtableCSWGuiButton);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Workbench items panel (upgradeitems.gui). 5 controls: LB_ITEMS (id 0),
// LB_DESCRIPTION (id 2), LBL_TITLE (id 3), BTN_UPGRADEITEM (id 4),
// BTN_BACK (id 5). Identified by the LB_ITEMS at id 0 + the BTN_BACK
// at id 5 (which uniquely sits at id 5 — saveload.gui has no id 5, and
// other listbox panels we know about don't put their back button at id 5).
bool IsWorkbenchItemsStructural(void* panel) {
    if (!panel) return false;
    __try {
        void* lb = FindControlByGuiId(panel, /*LB_ITEMS=*/0);
        if (!lb) return false;
        void** lbVtable = *reinterpret_cast<void***>(lb);
        if (reinterpret_cast<uintptr_t>(lbVtable) != kVtableListBox) return false;
        void* upgrade = FindControlByGuiId(panel, /*BTN_UPGRADEITEM=*/4);
        void* back    = FindControlByGuiId(panel, /*BTN_BACK=*/5);
        if (!HasVtable(upgrade, kVtableCSWGuiButton)) return false;
        if (!HasVtable(back,    kVtableCSWGuiButton)) return false;
        // Disambiguate from any other shape that might have a listbox at
        // ID 0 + buttons at IDs 4/5: require the panel to NOT also be the
        // upgrade panel (29 controls). upgradeitems.gui has exactly 5
        // controls; the upgrade panel's structural detector matches first
        // when both succeed, but checking control count here keeps each
        // detector self-consistent.
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
        if (!list || !list->data) return false;
        return list->size >= 4 && list->size <= 8;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Workbench category-select panel (upgradesel.gui). 11 controls: four
// pairs of category Button + ProtoItem icon at IDs 0/1, 2/3, 4/5, 6/7;
// LBL_TITLE at id 8; BTN_UPGRADEITEMS ("Aufwerten") at id 9; BTN_BACK at
// id 10. Identified by the pair Button-at-0 + Button-at-9 + Button-at-10
// — id 0 is a Button on this panel (BTN_RANGED), distinguishing it from
// every other workbench panel where id 0 is a ListBox.
// CSWGuiUpgradeSelection (upgradesel.gui — the workbench category chooser:
// Ranged / Lightsaber / Melee / Armor). Heap-allocated, single class, so the
// vtable is a clean identifier (symbol CSWGuiUpgradeSelection_vtable). The
// structural check below was rejecting this panel — its category buttons use a
// button subclass whose vtable isn't kVtableCSWGuiButton — so it fell through
// to Unknown; the vtable test fixes that.
const uintptr_t kVtableCSWGuiUpgradeSelection = acc::addr::Pick(0x007571b0, 0x009A86CC);

bool IsWorkbenchSelectStructural(void* panel) {
    if (!panel) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(panel);
        if (reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiUpgradeSelection) {
            return true;
        }
        // Fallback structural signature (kept in case a build relocates the
        // vtable): id 9 = BTN_UPGRADEITEMS, id 10 = BTN_BACK are plain buttons.
        void* btnUpg  = FindControlByGuiId(panel, /*BTN_UPGRADEITEMS=*/9);
        void* btnBack = FindControlByGuiId(panel, /*BTN_BACK=*/10);
        void* btnFirst = FindControlByGuiId(panel, /*BTN_RANGED=*/0);
        return btnFirst != nullptr &&
               HasVtable(btnUpg,  kVtableCSWGuiButton) &&
               HasVtable(btnBack, kVtableCSWGuiButton);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// CSWGuiLevelUpPanel identity by vtable. The panel is heap-allocated by
// CSWGuiInGameCharacter::ShowLevelUpGUI when the user clicks Levelaufst.,
// so it has no CGuiInGame slot. Lane's SARIF labels the vtable at
// 0x00759568 (verified via Ghidra ListSymbolsByName 2026-05-14).
const uintptr_t kVtableCSWGuiLevelUpPanel = acc::addr::Pick(0x00759568, 0x009AA1AC);

bool IsLevelUpStructural(void* panel) {
    return HasVtable(panel, kVtableCSWGuiLevelUpPanel);
}

// Character-creation step panels by vtable. Heap-allocated, no CGuiInGame
// slot, single class each, so vtable equality is the identifier. Verified
// via Ghidra ListSymbolsByName (CSWGuiCustomPanel_vtable / CSWGuiQuickPanel_
// vtable) and against the live panels in patch-20260608-135543.log
// (custom=0x007595e0 21 controls, quick=0x00759668 12 controls). Both drive
// their build steps (Porträt / Attribute / Fähigkeiten / Talente / Name /
// Spielen) sequentially, enabling one at a time via CSWGuiControl::SetEnabled
// (bit_flags bit 3) — same shape as the in-game level-up wizard.
const uintptr_t kVtableCSWGuiCustomPanel = acc::addr::Pick(0x007595e0, 0x009AA9A4);
const uintptr_t kVtableCSWGuiQuickPanel  = acc::addr::Pick(0x00759668, 0x009AAA24);

bool IsCharGenStructural(void* panel) {
    if (!panel) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(panel);
        uintptr_t v = reinterpret_cast<uintptr_t>(vt);
        return v == kVtableCSWGuiCustomPanel || v == kVtableCSWGuiQuickPanel;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// CSWGuiOptions title-screen options panel identity by vtable. The class
// is single-instance and lives in the engine's title-screen UI suite, so
// vtable equality is the cleanest identifier. Captured 2026-05-26 via the
// LogUnknownPanelDiagnostics probe (PanelProbe block in
// patch-20260526-180650.log). Its real class name is CSWGuiOptionsMain — the
// constant predates knowing that, and the name is what the KOTOR 2 value was
// looked up by.
const uintptr_t kVtableCSWGuiOptions = acc::addr::Pick(0x00758838, 0x009A183C);

bool IsMainMenuOptionsStructural(void* panel) {
    return HasVtable(panel, kVtableCSWGuiOptions);
}

// CSWGuiMainMenu title-screen panel. Single-instance, vtable equality is
// the cleanest identifier. Captured 2026-05-30 in the user-reported
// stuck-menu log (patch-20260530-191714.log frame at 19:17:40):
// `PanelProbe: first sight UNKNOWN panel=077F49D8 vtable=0x00752f70`.
// Classifying this lets AnnouncePanelTitle skip the generic label-walk
// (which lands on the DLC notice) and speak Id::PanelTitleMainMenu.
const uintptr_t kVtableCSWGuiMainMenu = acc::addr::Pick(0x00752f70, 0x009A597C);

bool IsMainMenuStructural(void* panel) {
    return HasVtable(panel, kVtableCSWGuiMainMenu);
}

// CSWGuiPazaakStart side-deck builder. Heap-allocated modal (no CGuiInGame
// slot), single class, so vtable equality is the identifier. Verified against
// the live panel dump (patch-20260601-071641.log: 79-control panel,
// vtable=0x007532e8).
const uintptr_t kVtableCSWGuiPazaakStart = acc::addr::Pick(0x007532e8, 0x009A614C);

bool IsPazaakStartStructural(void* panel) {
    return HasVtable(panel, kVtableCSWGuiPazaakStart);
}

// CSWGuiWagerPopup — the "Wie viel setzt du?" bet popup pushed over the
// side-deck builder. Single class, heap-allocated, so vtable equality is the
// identifier (CSWGuiWagerPopup_vtable, verified against the live panel dump in
// patch-20260601-090245.log: 8-control panel, vtable=0x007534c8).
const uintptr_t kVtableCSWGuiWagerPopup = acc::addr::Pick(0x007534c8, 0x009A6034);

bool IsPazaakWagerStructural(void* panel) {
    return HasVtable(panel, kVtableCSWGuiWagerPopup);
}

// CSWGuiQuestItem — the journal's "Auftrags-Gegenstände" sub-screen. Single
// class, heap-allocated and owned by the journal (no CGuiInGame slot), so
// vtable equality is the identifier (CSWGuiQuestItem_vtable, verified against
// the live panel dump in patch-20260603-090028.log: 3-element chain with a
// BTN_BACK at the bottom).
//
// KOTOR 2 has no such panel. Its RTTI names 122 CSWGui classes and covers every
// one KOTOR 1 has except this and CSWGuiScriptSelect below; the exe holds no
// "questitem" string either. K2's journal drops the quest-items sub-screen.
const uintptr_t kVtableCSWGuiQuestItem = acc::addr::Kotor1Only(0x00757c20);

bool IsQuestItemStructural(void* panel) {
    return HasVtable(panel, kVtableCSWGuiQuestItem);
}

// CSWGuiScriptSelect — the character sheet's "Kurzbefehle" combat-behaviour
// picker. Heap-allocated modal with no CGuiInGame slot, so vtable equality is
// the identifier (verified via Ghidra: destructor at 0x006e9ef0, vtable label
// at 0x007590a8, against the live panel dump in patch-20260613-205358.log).
//
// The other class KOTOR 2 does not have — see CSWGuiQuestItem above for the
// evidence. K2 exposes combat behaviour differently and ships no
// CSWGuiScriptSelect.
const uintptr_t kVtableCSWGuiScriptSelect = acc::addr::Kotor1Only(0x007590a8);

bool IsScriptSelectStructural(void* panel) {
    return HasVtable(panel, kVtableCSWGuiScriptSelect);
}

// CSWGuiPowersLevelUp picker (pwrlvlup.gui). The same class backs both the
// chargen Force-selection screen and the InGameLevelUp "Kr�fte" sub-screen;
// the SARIF documents the struct (swkotor.exe.h:16603) but doesn't name the
// vtable, so we identify structurally. Signature taken from the panel walk
// in patch-20260526-071446.log frame 12715: two ListBox children at .gui
// IDs 6 (powers_listbox) and 7 (description_listbox), with the four
// Button children at IDs 9..12 (recommended/select/accept/back). No other
// heap-allocated panel we've seen puts a listbox at ID 6 or 7, which keeps
// this distinct from SaveLoad (listbox at 0) and the Workbench shapes
// (listbox at 0).
bool IsPowersLevelUpStructural(void* panel) {
    if (!panel) return false;
    __try {
        // Primary: vtable equality — the clean, collision-proof identifier
        // used by every other single-instance heap panel (feats, level-up,
        // main menu, options sub-screens). This is what makes the powers
        // screen robust against loose structural signatures elsewhere in
        // the ladder, not merely its probe position.
        void** vt = *reinterpret_cast<void***>(panel);
        if (reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiPowersLevelUp) {
            return true;
        }
        // Fallback structural signature (kept in case a build relocates the
        // vtable): powers_listbox (id 6) and description_listbox (id 7) are
        // ListBoxes; BTN_ACCEPT (id 11) and BTN_BACK (id 12) are buttons.
        void* lbPowers = FindControlByGuiId(panel, /*powers_listbox=*/6);
        if (!HasVtable(lbPowers, kVtableListBox)) return false;
        void* lbDesc   = FindControlByGuiId(panel, /*description_listbox=*/7);
        if (!HasVtable(lbDesc, kVtableListBox)) return false;
        void* btnAccept = FindControlByGuiId(panel, /*BTN_ACCEPT=*/11);
        void* btnBack   = FindControlByGuiId(panel, /*BTN_BACK=*/12);
        return HasVtable(btnAccept, kVtableCSWGuiButton) &&
               HasVtable(btnBack,   kVtableCSWGuiButton);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Title-screen Options sub-screens. Each is a single-instance heap-allocated
// CSWGui* option panel with no CGuiInGame slot, so — like MainMenu / Options /
// Pazaak — vtable equality is the cleanest identifier. Vtables captured
// 2026-06-13 from the PanelProbe "first sight UNKNOWN" dumps in
// patch-20260613-194918.log; each is paired there with the title the
// label-walk speaks (e.g. 0x007587c0 → "Soundeinstellungen"). GoG-derived
// addresses match the Steam build (see memory ghidra_gog_steam_bytes_match),
// matching every other hardcoded title-screen vtable above.
//
// KOTOR 2 values by RTTI class name (all nine classes exist there). The class
// each PanelKind corresponds to is named in the comment, because the KOTOR 1
// column was captured from a live probe and carries no name of its own.
struct OptionsSubScreenVtable {
    uintptr_t vtable;
    PanelKind kind;
};
static const OptionsSubScreenVtable kOptionsSubScreenVtables[] = {
    // CSWGuiOptionsSound
    { acc::addr::Pick(0x007587c0, 0x009A1D8C), PanelKind::SoundSettings            },
    // CSWGuiOptionsSoundAdvanced
    { acc::addr::Pick(0x00758550, 0x009A1E4C), PanelKind::AdvancedSoundSettings    },
    // CSWGuiOptionsGraphics
    { acc::addr::Pick(0x007586f8, 0x009A1A9C), PanelKind::GraphicsSettings         },
    // CSWGuiOptionsGraphicsAdvanced
    { acc::addr::Pick(0x007584a0, 0x009A1BF4), PanelKind::AdvancedGraphicsSettings },
    // CSWGuiInGameAutoPause
    { acc::addr::Pick(0x00758ee0, 0x009A8FCC), PanelKind::AutoPauseOptions         },
    // CSWGuiOptionsFeedback
    { acc::addr::Pick(0x007581e8, 0x009A18DC), PanelKind::FeedbackOptions          },
    // CSWGuiInGameGameplay
    { acc::addr::Pick(0x00758e00, 0x009A8EE4), PanelKind::GameSettings             },
    // CSWGuiOptionsMouse
    { acc::addr::Pick(0x007585f8, 0x009A1EFC), PanelKind::MouseSettings            },
    // CSWGuiInGameOptKeyMappings
    { acc::addr::Pick(0x00759358, 0x009A9EA4), PanelKind::KeyboardMapping          },
};

// Returns the specific sub-screen kind, or Unknown if `panel`'s vtable
// isn't one of the nine. One detector covers all nine (vs. nine near-
// identical IsXStructural functions) because the only distinguishing
// signal is the vtable.
PanelKind IdentifyOptionsSubScreen(void* panel) {
    if (!panel) return PanelKind::Unknown;
    __try {
        uintptr_t vt = reinterpret_cast<uintptr_t>(*reinterpret_cast<void***>(panel));
        for (const auto& e : kOptionsSubScreenVtables) {
            if (e.vtable == vt) return e.kind;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return PanelKind::Unknown;
}

}  // namespace

bool IsWorkbenchUpgradeSlotButtonId(int cid) {
    // See the header comment: K1 packs the slot buttons at 12..18; K2
    // scatters them and reuses 13 for BTN_BACK.
    if (!acc::game::IsKotor2()) return cid >= 12 && cid <= 18;
    switch (cid) {
        case 6: case 7: case 8:                    // BTN_UPGRADE33/31/32
        case 17: case 18: case 19:                 // BTN_UPGRADE31/32/33_LS
        case 23: case 24: case 25:                 // BTN_UPGRADE34/35/36_LS
            return true;
        default:
            return false;
    }
}

// See the header for why the index lives here rather than at the call sites.
//
// The upper bounds are the entry counts each game's table actually carries.
// KOTOR 1's is the long-standing 16 (4 categories x 4 slots). KOTOR 2's blocks
// are 6 slots wide and a dump of the table shows four fully-populated category
// blocks plus a fifth that begins well-formed, so 30 is the evidence-backed
// bound; a category beyond that reports no-entry and the caller falls back to
// its generic label rather than reading past what has been looked at.
bool LookupUpgradeSlotType(int slotCustomValue, int category,
                           int* outUpgradeType, uint32_t* outStrRef) {
    int tableIdx;
    int tableCount;
    if (acc::game::IsKotor2()) {
        // KOTOR 2 indexes the raw slot value and biases the category.
        if (slotCustomValue < 0 || slotCustomValue > 5 || category < 1) return false;
        tableIdx   = slotCustomValue + (category - 1) * 6;
        tableCount = 30;
    } else {
        // KOTOR 1 biases the slot by four and does not bias the category.
        tableIdx   = (slotCustomValue - 4) + category * 4;
        tableCount = 16;
    }
    if (tableIdx < 0 || tableIdx >= tableCount) return false;

    int      upgradeType = -1;
    uint32_t strref      = 0;
    __try {
        auto* entry = reinterpret_cast<unsigned char*>(
            kAddrUpgradeSlotTypeTable + tableIdx * kUpgradeSlotTypeStride);
        upgradeType = *reinterpret_cast<int*>(entry);
        strref      = *reinterpret_cast<uint32_t*>(
            entry + kUpgradeSlotTypeStrRefOff);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    // Sentinel rows mark slot positions the category does not use.
    if (upgradeType == -1 || strref == 0) return false;

    if (outUpgradeType) *outUpgradeType = upgradeType;
    if (outStrRef)      *outStrRef      = strref;
    return true;
}

// CGuiInGame resolution chain. Address values verified against Lane's
// SARIF (CAppManager_vtable @ 0x007A39FC). Field offsets from the struct
// definitions in docs/llm-docs/re/swkotor.exe.h.
// (values live in engine_panels_internal.h — engine_panels_state.cpp
// walks the same chain)

// One row per named CGuiInGame slot. Adding a panel kind = add an enum
// value in engine_panels.h and a row here with its CGuiInGame field offset.
//
// Rows with `offset == kNoSlotOffset` are heap-allocated panels with no
// fixed CGuiInGame field — they are skipped during slot lookup and only
// referenced by PanelKindName for friendly-name resolution. They are
// identified structurally inside IdentifyPanel.
constexpr size_t kNoSlotOffset = static_cast<size_t>(-1);

struct PanelKindOffset {
    size_t      offset;
    PanelKind   kind;
    const char* name;
};

// KOTOR 2 values recovered 2026-08-01 by tools/re-scripts/k2_slot_table.py,
// which reads each slot off the `mov [this+off], eax` that files away the
// panel CGuiInGame's creator just constructed. See docs/kotor2-port.md.
//
// The shape: identical up to +0x74, then KOTOR 2 inserts members and every
// slot above shifts. So this table is a mixture and `Same` is NOT the default
// — InGameMessages in particular moves 0x1c -> 0x78, which on KOTOR 1 is
// PartySelection, so an unported table maps those two screens onto each other
// and misclassifies in silence rather than failing.
//
// Todo() rows are slots whose KOTOR 2 counterpart is not yet located. They
// poison to kUnportedOffset there and SlotTableLookup skips them, so the rest
// of the walk still runs; each simply falls through to structural / vtable
// identification the way it did before this port.
static const PanelKindOffset kPanelKindOffsets[] = {
    // Witnessed on both games (2026-08-01): ShowSWInGameGui / HideSWInGameGui
    // add/remove the [this+0x08] panel as the strip menu in both decompiles.
    { acc::off::Same(0x08), PanelKind::InGameMenu,          "InGameMenu" },
    { acc::off::Same(0x0c), PanelKind::InGameEquip,         "InGameEquip" },
    { acc::off::Same(0x10), PanelKind::InGameInventory,     "InGameInventory" },
    // RESOLVED 2026-08-02: KOTOR 2 stores its character sheet at the SAME
    // slot. The panel creator (0x007BE4C0) constructs it at 0x0084C3A0 —
    // which vtable_xrefs proves stores the CSWGuiInGameCharacter vtable
    // (0x009A3E7C) — and writes the result with `MOV [gui+0x14],EAX` at
    // 0x007BF439. The earlier "K2 puts a CSWGui3DSceneView here" note was
    // the slot-table tool misattributing a neighbouring allocation, the same
    // way it misread the DialogComputerCamera slot as BlackenedLabel.
    // Until this was resolved the character sheet was the one sub-screen
    // that never announced on KOTOR 2 (patch-20260801-232432.log: six
    // sub-screens spoke, InGameCharacter never appeared at all).
    { acc::off::Same(0x14), PanelKind::InGameCharacter,     "InGameCharacter" },
    { acc::off::Same(0x18), PanelKind::InGameAbilities,     "InGameAbilities" },
    { acc::off::Pick(0x1c, 0x78), PanelKind::InGameMessages, "InGameMessages" },
    { acc::off::Same(0x20), PanelKind::InGameJournal,       "InGameJournal" },
    { acc::off::Same(0x24), PanelKind::InGameMap,           "InGameMap" },
    { acc::off::Same(0x28), PanelKind::InGameOptions,       "InGameOptions" },
    // 0x3c is the ACTIVE-dialog-panel pointer (aliases 0x40 or 0x44), not a
    // creator-built slot — Same on K2, witnessed by the K2 helper 0x007CB750
    // (`[gui+0x3c] && [gui+0x3c]==[gui+0x40]`), which is K1's inline
    // dialog_cinematic_copy_==dialog_cinematic check factored out.
    { acc::off::Same(0x3c), PanelKind::DialogCinematicCopy, "DialogCinematicCopy" },
    { acc::off::Same(0x40), PanelKind::DialogCinematic,     "DialogCinematic" },
    { acc::off::Same(0x44), PanelKind::DialogComputer,      "DialogComputer" },
    // Same(0x48) witnessed directly: the panel creator stores the
    // CSWGuiDialogComputerCamera ctor result (0x008BD910) via
    // `MOV [gui+0x48],ECX` at 0x007BE9D4. Batch 2's note that
    // CSWGuiBlackenedLabel sits here was the slot-table tool misattributing
    // the immediately-following BlackenedLabel allocation.
    { acc::off::Same(0x48), PanelKind::DialogComputerCamera, "DialogComputerCamera" },
    { acc::off::Same(0x4c), PanelKind::BarkBubble,          "BarkBubble" },
    { acc::off::Same(0x50), PanelKind::Examine,             "Examine" },
    { acc::off::Same(0x54), PanelKind::Container,           "Container" },
    { acc::off::Same(0x58), PanelKind::CreateItemMenu,      "CreateItemMenu" },
    { acc::off::Same(0x5c), PanelKind::CreateItemSubMenu,   "CreateItemSubMenu" },
    { acc::off::Same(0x60), PanelKind::DialogLetterbox1,    "DialogLetterbox1" },
    { acc::off::Same(0x64), PanelKind::DialogLetterbox2,    "DialogLetterbox2" },
    { acc::off::Same(0x68), PanelKind::DialogLetterbox3,    "DialogLetterbox3" },
    { acc::off::Same(0x6c), PanelKind::Fade,                "Fade" },
    { acc::off::Same(0x70), PanelKind::LoadModuleDebugMenu, "LoadModuleDebugMenu" },
    { acc::off::Same(0x74), PanelKind::PowersFeatsSkillsDebugMenu,
                                                            "PowersFeatsSkillsDebugMenu" },
    // KOTOR 2's 0x78 is InGameMessages (above), so this must NOT inherit it.
    // KOTOR 2 swaps this slot with InGameMessages: K1 files PartySelection
    // at 0x78 and Messages at 0x1c; K2's creator (0x007BE4C0) stores the
    // CSWGuiPartySelection ctor result (0x0089CF30) at [gui+0x1c].
    { acc::off::Pick(0x78, 0x1c), PanelKind::PartySelection, "PartySelection" },
    { acc::off::Same(0x7c), PanelKind::InGamePause,         "InGamePause" },
    // 0x80 holds CSWGui3DSceneView on KOTOR 2.
    // Same slot on both games: K2 creator stores the CSWGuiInGameGalaxyMap
    // ctor result (0x008973D0) at [gui+0x80].
    { acc::off::Same(0x80), PanelKind::InGameGalaxyMap,     "InGameGalaxyMap" },
    { acc::off::Same(0x84), PanelKind::Store,               "Store" },
    { acc::off::Pick(0x8c, 0x94), PanelKind::SoloModeQuery, "SoloModeQuery" },
    // KOTOR 2 witnessed by SetSWGuiStatus @0x007C9C40: status 1 adds, any
    // other status removes, the panel at [this+0x98] — the exact behaviour
    // KOTOR 1's SetSWGuiStatus shows for main_interface at +0x90.
    { acc::off::Pick(0x90, 0x98), PanelKind::MainInterface, "MainInterface" },
    { acc::off::Pick(0x94, 0x9c), PanelKind::AreaTransition, "AreaTransition" },
    // KOTOR 2 files three CSWGuiMessageBox instances (0xa0, 0xa4, one more not
    // yet followed); KOTOR 1 tracks only the modal one. 0xa0 is the first.
    { acc::off::Pick(0x98, 0xa0), PanelKind::MessageBoxModal, "MessageBox" },
    { acc::off::Pick(0x9c, 0xac), PanelKind::SkillInfoBox,  "SkillInfoBox" },
    { acc::off::Pick(0xa0, 0xb0), PanelKind::TutorialBox,   "TutorialBox" },
    // KOTOR 2's 0xa4 is a CSWGuiMessageBox, not the controller-loss box —
    // which it does have as a class (CSWGuiControllerLossBox in the RTTI).
    // K2 creator stores the ControllerLossBox at [gui+0xb4] (the ctor is
    // inlined there — the CSWGuiControllerLossBox vftable store is visible
    // in the creator body itself). K2's 0xa4 is a second MessageBox
    // instance (ctor 0x0075B370 with arg 1), not this box.
    { acc::off::Pick(0xa4, 0xb4), PanelKind::ControllerLossBox, "ControllerLossBox" },
    { acc::off::Pick(0xa8, 0xb8), PanelKind::StatusSummary, "StatusSummary" },
    // Dialogue input-routing surfaces (per CGuiInGame layout in
    // swkotor.exe.h:10282). The in-game session log shows that during
    // a CSWGuiDialogCinematic conversation, arrow-key input routes to
    // a separate foreground panel (0FDEE418 in patch-20260502-182804.log)
    // distinct from the rendering panel (DialogCinematicCopy at +0x3c).
    // Hypothesis: that routing target is one of these two — registering
    // both so the next log identifies which.
    // KOTOR 1 only: K2's creator stores no panel slot past +0xb8, and the
    // +0xf8..0x11f region there belongs to other fields (message rings at
    // +0x110/+0x118). On K2 these kinds fall through to the vtable detector.
    { acc::off::Kotor1Only(0xf8), PanelKind::DialogMessagesAux, "DialogMessagesAux" },
    { acc::off::Kotor1Only(0xfc), PanelKind::DialogMessages,    "DialogMessages" },
    // Heap-allocated kinds with no CGuiInGame slot. The sentinel offset
    // skips them during slot lookup; PanelKindName still resolves the
    // friendly name, and IdentifyPanel falls through to a structural
    // detector below.
    { kNoSlotOffset, PanelKind::SaveLoad,          "SaveLoad" },
    { kNoSlotOffset, PanelKind::InGameLevelUp,     "InGameLevelUp" },
    { kNoSlotOffset, PanelKind::WorkbenchSelect,   "WorkbenchSelect" },
    { kNoSlotOffset, PanelKind::WorkbenchItems,    "WorkbenchItems" },
    { kNoSlotOffset, PanelKind::WorkbenchUpgrade,  "WorkbenchUpgrade" },
    { kNoSlotOffset, PanelKind::PowersLevelUp,     "PowersLevelUp" },
    { kNoSlotOffset, PanelKind::MainMenuOptions,   "MainMenuOptions" },
    { kNoSlotOffset, PanelKind::MainMenu,          "MainMenu" },
    { kNoSlotOffset, PanelKind::PazaakStart,       "PazaakStart" },
    { kNoSlotOffset, PanelKind::PazaakWager,       "PazaakWager" },
    { kNoSlotOffset, PanelKind::InGameQuestItems,  "InGameQuestItems" },
    { kNoSlotOffset, PanelKind::ScriptSelect,      "ScriptSelect" },
    { kNoSlotOffset, PanelKind::SoundSettings,            "SoundSettings" },
    { kNoSlotOffset, PanelKind::AdvancedSoundSettings,    "AdvancedSoundSettings" },
    { kNoSlotOffset, PanelKind::GraphicsSettings,         "GraphicsSettings" },
    { kNoSlotOffset, PanelKind::AdvancedGraphicsSettings, "AdvancedGraphicsSettings" },
    { kNoSlotOffset, PanelKind::AutoPauseOptions,         "AutoPauseOptions" },
    { kNoSlotOffset, PanelKind::FeedbackOptions,          "FeedbackOptions" },
    { kNoSlotOffset, PanelKind::GameSettings,             "GameSettings" },
    { kNoSlotOffset, PanelKind::MouseSettings,            "MouseSettings" },
    { kNoSlotOffset, PanelKind::KeyboardMapping,          "KeyboardMapping" },
};
static constexpr int kPanelKindOffsetCount =
    sizeof(kPanelKindOffsets) / sizeof(kPanelKindOffsets[0]);

const char* PanelKindName(PanelKind k) {
    if (k == PanelKind::Unknown) return "Unknown";
    for (int i = 0; i < kPanelKindOffsetCount; ++i) {
        if (kPanelKindOffsets[i].kind == k) return kPanelKindOffsets[i].name;
    }
    return "?";
}

void* ResolveGuiInGame() {
    void* internal = GetClientAppInternal();
    if (!internal) return nullptr;
    // The walk itself is guarded inside GetClientAppInternal(); this last hop
    // was previously unguarded along with the rest of the chain.
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) + kClientExoAppGuiInGameOff);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* ResolveMainInterface() {
    void* guiInGame = ResolveGuiInGame();
    if (!guiInGame) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(guiInGame) +
            kGuiInGameMainInterfaceOff);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadDialogReplyText(int replyIndex, char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    if (replyIndex < 0) return false;
    void* gui = ResolveGuiInGame();
    if (!gui) return false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(gui);
        // field69_0x114 = reply array capacity/count (SetReplyData guards on it).
        uint32_t count = *reinterpret_cast<uint32_t*>(
            base + kCGuiInGameReplyCountOffset);
        if (count == 0 || count > 256) return false;  // sanity bound
        if (static_cast<uint32_t>(replyIndex) >= count) return false;
        // field70_0x118 = pointer to the CExoString[] array (8 bytes/entry).
        void* arr = *reinterpret_cast<void**>(
            base + kCGuiInGameReplyTextArrayOffset);
        if (!arr) return false;
        return ReadCExoString(arr, static_cast<size_t>(replyIndex) * 8,
                              outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
        return false;
    }
}

int ReadDialogReplyCount() {
    void* gui = ResolveGuiInGame();
    if (!gui) return -1;
    __try {
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(gui) + kCGuiInGameReplyCountOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// (panel, kind) pairs already logged. Keeps the log tidy when persistent
// panels (HUD) get re-checked on every input event. FIFO-evicted at cap.
struct PanelKindCacheEntry {
    void*     panel;
    PanelKind kind;
};
static constexpr int kPanelKindCacheSize = 32;
static PanelKindCacheEntry g_panelKindCache[kPanelKindCacheSize];
static int g_panelKindCacheCount = 0;

// Unknown-panel probe. First-sight diagnostic dump for panels that miss
// the slot table AND every structural detector — the canonical case is
// the title-screen Options panel (CGuiInGame isn't resolvable pre-game,
// so the slot scan is skipped entirely, and no detector currently knows
// its shape). Dedup is by panel *vtable* (not panel pointer) so the dump
// fires exactly once per unique panel class across the whole session —
// re-opening Options reuses the same class so we don't re-log.
//
// What we capture: panel vtable, panel.controls.size, and per-control
// {vtable, .gui-id at +0x50, button-or-label rendered text}. That's
// enough to write a structural detector matching SaveLoad / Workbench
// shapes once the user sends us the log line.
namespace {

constexpr int kUnknownVtableCacheSize = 16;
uintptr_t g_unknownVtableCache[kUnknownVtableCacheSize] = {};
int       g_unknownVtableCacheCount = 0;

bool IsVtableAlreadyDumped(uintptr_t vt) {
    for (int i = 0; i < g_unknownVtableCacheCount; ++i) {
        if (g_unknownVtableCache[i] == vt) return true;
    }
    return false;
}

void RememberDumpedVtable(uintptr_t vt) {
    if (g_unknownVtableCacheCount >= kUnknownVtableCacheSize) {
        memmove(g_unknownVtableCache, g_unknownVtableCache + 1,
                sizeof(g_unknownVtableCache[0]) *
                    (kUnknownVtableCacheSize - 1));
        g_unknownVtableCacheCount = kUnknownVtableCacheSize - 1;
    }
    g_unknownVtableCache[g_unknownVtableCacheCount++] = vt;
}

void LogUnknownPanelDiagnostics(void* panel) {
    if (!panel) return;
    uintptr_t panelVt = 0;
    __try {
        panelVt = reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(panel));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (panelVt == 0) return;
    if (IsVtableAlreadyDumped(panelVt)) return;
    RememberDumpedVtable(panelVt);

    CExoArrayList* list = nullptr;
    int size = 0;
    __try {
        list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
        size = list ? list->size : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        size = 0;
    }
    acclog::Write("PanelProbe",
                  "first sight UNKNOWN panel=%p vtable=0x%08x controls=%d",
                  panel, static_cast<unsigned>(panelVt), size);
    if (!list || !list->data || size <= 0) return;

    int n = size > 32 ? 32 : size;
    for (int i = 0; i < n; ++i) {
        void* c = nullptr;
        __try {
            c = list->data[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            c = nullptr;
        }
        if (!c) {
            acclog::Write("PanelProbe", "  [%d] (null)", i);
            continue;
        }
        uintptr_t cvt = 0;
        int       cid = -1;
        __try {
            cvt = reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(c));
            cid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(c) + kControlIdOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            cvt = 0;
            cid = -1;
        }
        char text[96];
        text[0] = '\0';
        if (!ReadGuiString(c, kButtonGuiStringPtrOffset,
                           text, sizeof(text))) {
            ReadGuiString(c, kLabelGuiStringPtrOffset,
                          text, sizeof(text));
        }
        acclog::Write("PanelProbe",
                      "  [%d] %p vtable=0x%08x id=%d text=\"%s\"",
                      i, c, static_cast<unsigned>(cvt), cid, text);
    }
}

// The CGuiInGame slot walk, hoisted out of IdentifyPanel so the __try owns no
// unwinding objects (C2712) and returns the matching table index, or -1.
//
// The SEH is not optional. Before it existed this walk ran on KOTOR 2 against
// a KOTOR 1 pointer chain and dereferenced a garbage base, taking the process
// down within the first Update ticks of a session (first Batch 1 test round,
// 2026-07-31: WER fault inside accessibility.dll at this very compare). It
// stays on KOTOR 1 too — the walk reads engine memory through a multi-hop
// pointer chain that a mid-teardown frame can invalidate.
//
// Runs on BOTH games since Batch 2 (2026-08-01), when the slot table gained
// its KOTOR 2 column. Rows still on Todo() poison to kUnportedOffset there and
// are skipped alongside the no-slot sentinel, so an unported row costs that
// one kind its slot-table identification — it falls through to the structural
// and vtable detectors, exactly as it did before the port — and never a fault
// that would abandon the rest of the walk.
int SlotTableLookup(void* panel) {
    void* gui = ResolveGuiInGame();
    if (!gui) return -1;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(gui);
        for (int i = 0; i < kPanelKindOffsetCount; ++i) {
            const size_t off = kPanelKindOffsets[i].offset;
            if (off == kNoSlotOffset || off == acc::off::kUnportedOffset) {
                continue;
            }
            void* slot = *reinterpret_cast<void**>(base + off);
            if (slot == panel) return i;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return -1;
}

}  // namespace

PanelKind IdentifyPanel(void* panel) {
    if (!panel) return PanelKind::Unknown;

    auto recordAndReturn = [&](PanelKind k, const char* name) -> PanelKind {
        // First-sight log per (panel, kind) pair.
        for (int j = 0; j < g_panelKindCacheCount; ++j) {
            if (g_panelKindCache[j].panel == panel &&
                g_panelKindCache[j].kind  == k) {
                return k;  // already logged
            }
        }
        if (g_panelKindCacheCount >= kPanelKindCacheSize) {
            memmove(g_panelKindCache, g_panelKindCache + 1,
                    sizeof(g_panelKindCache[0]) * (kPanelKindCacheSize - 1));
            g_panelKindCacheCount = kPanelKindCacheSize - 1;
        }
        g_panelKindCache[g_panelKindCacheCount++] = { panel, k };
        acclog::Write("PanelKind", "panel=%p identified as %s", panel, name);
        return k;
    };

    int slotIdx = SlotTableLookup(panel);
    if (slotIdx >= 0) {
        return recordAndReturn(kPanelKindOffsets[slotIdx].kind,
                               kPanelKindOffsets[slotIdx].name);
    }

    // Slot-table miss: structural detectors for heap-allocated kinds. Run
    // unconditionally on every miss — these panels live transiently and
    // we don't know in advance whether `panel` is currently one of them.
    // Cost is bounded (a few control-id walks per probe) and only paid
    // when the slot table didn't classify the panel.
    //
    // Probe order: tighter (more-distinctive) signatures first. SaveLoad
    // and WorkbenchUpgrade used to collide on the {0, 11, 12, 14} ID
    // quartet — the tightened SaveLoad detector now requires ID 11 to be
    // a Button (saveload.gui's BTN_DELETE), so the workbench upgrade
    // panel's ID 11 = LBL_UPGRADE44 (LabelHilight) no longer false-matches.
    // Probing workbench-shapes before SaveLoad provides belt-and-braces
    // protection against future regressions.
    if (IsWorkbenchUpgradeStructural(panel)) {
        return recordAndReturn(PanelKind::WorkbenchUpgrade, "WorkbenchUpgrade");
    }
    if (IsWorkbenchItemsStructural(panel)) {
        return recordAndReturn(PanelKind::WorkbenchItems, "WorkbenchItems");
    }
    // PowersLevelUp must probe before WorkbenchSelect. WorkbenchSelect's
    // structural fallback was loosened (commit 29bdb2b) to "id 0 present +
    // id 9 button + id 10 button" — a signature the force-power picker
    // (pwrlvlup.gui) also satisfies (id 0 placeholder, id 9 "Empfohlen",
    // id 10 BTN_SELECT), so it stole the powers panel and the skill tree
    // went silent. PowersLevelUp now identifies by vtable (collision-proof),
    // but the workbench fallback is purely structural and ignores the vtable,
    // so it would still grab the powers panel if probed first — hence the
    // order. Per the "tighter first" rule above, the more-distinctive
    // detector claims its panel before the loose workbench fallback.
    if (IsPowersLevelUpStructural(panel)) {
        return recordAndReturn(PanelKind::PowersLevelUp, "PowersLevelUp");
    }
    if (IsWorkbenchSelectStructural(panel)) {
        return recordAndReturn(PanelKind::WorkbenchSelect, "WorkbenchSelect");
    }
    if (IsSaveLoadStructural(panel)) {
        return recordAndReturn(PanelKind::SaveLoad, "SaveLoad");
    }
    if (IsLevelUpStructural(panel)) {
        return recordAndReturn(PanelKind::InGameLevelUp, "InGameLevelUp");
    }
    if (IsCharGenStructural(panel)) {
        return recordAndReturn(PanelKind::CharGen, "CharGen");
    }
    if (IsMainMenuOptionsStructural(panel)) {
        return recordAndReturn(PanelKind::MainMenuOptions, "MainMenuOptions");
    }
    if (IsMainMenuStructural(panel)) {
        return recordAndReturn(PanelKind::MainMenu, "MainMenu");
    }
    if (IsPazaakStartStructural(panel)) {
        return recordAndReturn(PanelKind::PazaakStart, "PazaakStart");
    }
    if (IsPazaakWagerStructural(panel)) {
        return recordAndReturn(PanelKind::PazaakWager, "PazaakWager");
    }
    if (IsQuestItemStructural(panel)) {
        return recordAndReturn(PanelKind::InGameQuestItems, "InGameQuestItems");
    }
    if (IsScriptSelectStructural(panel)) {
        return recordAndReturn(PanelKind::ScriptSelect, "ScriptSelect");
    }
    // Title-screen Options sub-screens (Sound / Graphics / Advanced variants /
    // Auto-Pause / Feedback / Game / Mouse / Keyboard). One vtable-table probe
    // covers all nine.
    if (PanelKind k = IdentifyOptionsSubScreen(panel); k != PanelKind::Unknown) {
        return recordAndReturn(k, PanelKindName(k));
    }

    // Last resort: dump diagnostics so we can write a structural detector
    // for this shape later. Deduped by panel vtable so we get exactly one
    // log block per unique panel class — title-screen Options, future
    // mod-added screens, etc.
    LogUnknownPanelDiagnostics(panel);
    return PanelKind::Unknown;
}

bool IsPanelKindInGameMenu(void* panel) {
    return IdentifyPanel(panel) == PanelKind::InGameMenu;
}

void* FindPanelByKind(PanelKind kind) {
    constexpr int kCap = 32;
    void* panels[kCap];
    int n = ReadPanelArray(GetGuiManager(), panels, kCap);
    for (int i = 0; i < n; ++i) {
        void* p = panels[i];
        if (!p) continue;
        if (IdentifyPanel(p) == kind) return p;
    }
    return nullptr;
}

bool IsMainMenuOptionsSubScreen(PanelKind k) {
    switch (k) {
    case PanelKind::SoundSettings:
    case PanelKind::AdvancedSoundSettings:
    case PanelKind::GraphicsSettings:
    case PanelKind::AdvancedGraphicsSettings:
    case PanelKind::AutoPauseOptions:
    case PanelKind::FeedbackOptions:
    case PanelKind::GameSettings:
    case PanelKind::MouseSettings:
    case PanelKind::KeyboardMapping:
        return true;
    default:
        return false;
    }
}

bool IsModalPopupPanel(PanelKind k) {
    switch (k) {
    case PanelKind::MessageBoxModal:
    case PanelKind::TutorialBox:
    case PanelKind::AreaTransition:
    case PanelKind::StatusSummary:
    case PanelKind::ControllerLossBox:
    case PanelKind::SkillInfoBox:
    case PanelKind::SoloModeQuery:
        return true;
    default:
        return false;
    }
}

}  // namespace acc::engine
