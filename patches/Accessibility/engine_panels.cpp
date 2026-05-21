#include "engine_panels.h"

#include <windows.h>  // SEH __try / __except
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine_manager.h"  // kAddrGuiManagerPtr, kMgrPanels*Offset, GetForegroundPanel
#include "engine_offsets.h"  // CExoArrayList, kPanelControlsOffset, kVtableListBox
#include "log.h"

namespace acc::engine {

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
            reinterpret_cast<unsigned char*>(c) + 0x50);
        if (cid == id) return c;
    }
    return nullptr;
}

// Verify a child control's vtable matches `expected`. Used by the
// structural detectors to disambiguate panels that share .gui-time IDs
// but differ in control type at those IDs (canonical case: SaveLoad's
// BTN_DELETE at ID 11 = Button vs. Workbench's LBL_UPGRADE44 at ID 11
// = LabelHilight). Returns false on null / SEH fault.
bool ControlHasVtable(void* control, uintptr_t expected) {
    if (!control) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(control);
        return reinterpret_cast<uintptr_t>(vt) == expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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
        return ControlHasVtable(del,  kVtableCSWGuiButton) &&
               ControlHasVtable(back, kVtableCSWGuiButton) &&
               ControlHasVtable(sl,   kVtableCSWGuiButton);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Workbench upgrade panel (upgrade.gui). 29 controls; uniquely identifiable
// by the 7 BTN_UPGRADE3X/4X slot buttons at .gui IDs 12..18 — all standard
// CSWGuiButtons — plus the BTN_ASSEMBLE button at ID 24. ID 11 is the
// LBL_UPGRADE44 LabelHilight (NOT a button), which is what disambiguates
// this panel from SaveLoad.
bool IsWorkbenchUpgradeStructural(void* panel) {
    if (!panel) return false;
    __try {
        // Quick coarse check: the panel needs a listbox at ID 0 (LB_ITEMS).
        // Workbench items go here; if missing this isn't the upgrade panel.
        void* lb = FindControlByGuiId(panel, /*LB_ITEMS=*/0);
        if (!lb) return false;
        void** lbVtable = *reinterpret_cast<void***>(lb);
        if (reinterpret_cast<uintptr_t>(lbVtable) != kVtableListBox) return false;
        // Probe the BTN_ASSEMBLE (ID 24) and one of the slot buttons
        // (ID 15 = BTN_UPGRADE41) for the standard CSWGuiButton vtable.
        // Two ID hits + vtable checks is enough to disambiguate from
        // every other 29-control heap-allocated panel we've seen.
        void* assemble = FindControlByGuiId(panel, /*BTN_ASSEMBLE=*/24);
        void* slot     = FindControlByGuiId(panel, /*BTN_UPGRADE41=*/15);
        return ControlHasVtable(assemble, kVtableCSWGuiButton) &&
               ControlHasVtable(slot,     kVtableCSWGuiButton);
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
        if (!ControlHasVtable(upgrade, kVtableCSWGuiButton)) return false;
        if (!ControlHasVtable(back,    kVtableCSWGuiButton)) return false;
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
bool IsWorkbenchSelectStructural(void* panel) {
    if (!panel) return false;
    __try {
        // id 0 on upgradesel.gui is BTN_RANGED (Button), not a ListBox.
        // This single check is enough to skip the items / upgrade panels.
        void* btnFirst = FindControlByGuiId(panel, /*BTN_RANGED=*/0);
        if (!ControlHasVtable(btnFirst, kVtableCSWGuiButton)) return false;
        void* btnUpg  = FindControlByGuiId(panel, /*BTN_UPGRADEITEMS=*/9);
        void* btnBack = FindControlByGuiId(panel, /*BTN_BACK=*/10);
        return ControlHasVtable(btnUpg,  kVtableCSWGuiButton) &&
               ControlHasVtable(btnBack, kVtableCSWGuiButton);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// CSWGuiLevelUpPanel identity by vtable. The panel is heap-allocated by
// CSWGuiInGameCharacter::ShowLevelUpGUI when the user clicks Levelaufst.,
// so it has no CGuiInGame slot. Lane's SARIF labels the vtable at
// 0x00759568 (verified via Ghidra ListSymbolsByName 2026-05-14).
constexpr uintptr_t kVtableCSWGuiLevelUpPanel = 0x00759568;

bool IsLevelUpStructural(void* panel) {
    if (!panel) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(panel);
        return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiLevelUpPanel;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

// CGuiInGame resolution chain. Address values verified against Lane's
// SARIF (CAppManager_vtable @ 0x007A39FC). Field offsets from the struct
// definitions in docs/llm-docs/re/swkotor.exe.h.
static constexpr uintptr_t kAddrAppManagerPtr        = 0x007A39FC;
static constexpr size_t    kAppManagerClientOff      = 0x04;
static constexpr size_t    kClientExoAppInternalOff  = 0x04;
static constexpr size_t    kClientExoAppGuiInGameOff = 0x40;

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

static const PanelKindOffset kPanelKindOffsets[] = {
    { 0x08, PanelKind::InGameMenu,                 "InGameMenu" },
    { 0x0c, PanelKind::InGameEquip,                "InGameEquip" },
    { 0x10, PanelKind::InGameInventory,            "InGameInventory" },
    { 0x14, PanelKind::InGameCharacter,            "InGameCharacter" },
    { 0x18, PanelKind::InGameAbilities,            "InGameAbilities" },
    { 0x1c, PanelKind::InGameMessages,             "InGameMessages" },
    { 0x20, PanelKind::InGameJournal,              "InGameJournal" },
    { 0x24, PanelKind::InGameMap,                  "InGameMap" },
    { 0x28, PanelKind::InGameOptions,              "InGameOptions" },
    { 0x3c, PanelKind::DialogCinematicCopy,        "DialogCinematicCopy" },
    { 0x40, PanelKind::DialogCinematic,            "DialogCinematic" },
    { 0x44, PanelKind::DialogComputer,             "DialogComputer" },
    { 0x48, PanelKind::DialogComputerCamera,       "DialogComputerCamera" },
    { 0x4c, PanelKind::BarkBubble,                 "BarkBubble" },
    { 0x50, PanelKind::Examine,                    "Examine" },
    { 0x54, PanelKind::Container,                  "Container" },
    { 0x58, PanelKind::CreateItemMenu,             "CreateItemMenu" },
    { 0x5c, PanelKind::CreateItemSubMenu,          "CreateItemSubMenu" },
    { 0x60, PanelKind::DialogLetterbox1,           "DialogLetterbox1" },
    { 0x64, PanelKind::DialogLetterbox2,           "DialogLetterbox2" },
    { 0x68, PanelKind::DialogLetterbox3,           "DialogLetterbox3" },
    { 0x6c, PanelKind::Fade,                       "Fade" },
    { 0x70, PanelKind::LoadModuleDebugMenu,        "LoadModuleDebugMenu" },
    { 0x74, PanelKind::PowersFeatsSkillsDebugMenu, "PowersFeatsSkillsDebugMenu" },
    { 0x78, PanelKind::PartySelection,             "PartySelection" },
    { 0x7c, PanelKind::InGamePause,                "InGamePause" },
    { 0x80, PanelKind::InGameGalaxyMap,            "InGameGalaxyMap" },
    { 0x84, PanelKind::Store,                      "Store" },
    { 0x8c, PanelKind::SoloModeQuery,              "SoloModeQuery" },
    { 0x90, PanelKind::MainInterface,              "MainInterface" },
    { 0x94, PanelKind::AreaTransition,             "AreaTransition" },
    { 0x98, PanelKind::MessageBoxModal,            "MessageBox" },
    { 0x9c, PanelKind::SkillInfoBox,               "SkillInfoBox" },
    { 0xa0, PanelKind::TutorialBox,                "TutorialBox" },
    { 0xa4, PanelKind::ControllerLossBox,          "ControllerLossBox" },
    { 0xa8, PanelKind::StatusSummary,              "StatusSummary" },
    // Dialogue input-routing surfaces (per CGuiInGame layout in
    // swkotor.exe.h:10282). The in-game session log shows that during
    // a CSWGuiDialogCinematic conversation, arrow-key input routes to
    // a separate foreground panel (0FDEE418 in patch-20260502-182804.log)
    // distinct from the rendering panel (DialogCinematicCopy at +0x3c).
    // Hypothesis: that routing target is one of these two — registering
    // both so the next log identifies which.
    { 0xf8, PanelKind::DialogMessagesAux,          "DialogMessagesAux" },
    { 0xfc, PanelKind::DialogMessages,             "DialogMessages" },
    // Heap-allocated kinds with no CGuiInGame slot. The sentinel offset
    // skips them during slot lookup; PanelKindName still resolves the
    // friendly name, and IdentifyPanel falls through to a structural
    // detector below.
    { kNoSlotOffset, PanelKind::SaveLoad,          "SaveLoad" },
    { kNoSlotOffset, PanelKind::InGameLevelUp,     "InGameLevelUp" },
    { kNoSlotOffset, PanelKind::WorkbenchSelect,   "WorkbenchSelect" },
    { kNoSlotOffset, PanelKind::WorkbenchItems,    "WorkbenchItems" },
    { kNoSlotOffset, PanelKind::WorkbenchUpgrade,  "WorkbenchUpgrade" },
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
    void* appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
    if (!appMgr) return nullptr;
    void* exoApp = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(appMgr) + kAppManagerClientOff);
    if (!exoApp) return nullptr;
    void* internal = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(exoApp) + kClientExoAppInternalOff);
    if (!internal) return nullptr;
    return *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(internal) + kClientExoAppGuiInGameOff);
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

    void* gui = ResolveGuiInGame();
    if (gui) {
        auto* base = reinterpret_cast<unsigned char*>(gui);
        for (int i = 0; i < kPanelKindOffsetCount; ++i) {
            if (kPanelKindOffsets[i].offset == kNoSlotOffset) continue;
            void* slot = *reinterpret_cast<void**>(base + kPanelKindOffsets[i].offset);
            if (slot != panel) continue;
            return recordAndReturn(kPanelKindOffsets[i].kind,
                                   kPanelKindOffsets[i].name);
        }
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
    if (IsWorkbenchSelectStructural(panel)) {
        return recordAndReturn(PanelKind::WorkbenchSelect, "WorkbenchSelect");
    }
    if (IsSaveLoadStructural(panel)) {
        return recordAndReturn(PanelKind::SaveLoad, "SaveLoad");
    }
    if (IsLevelUpStructural(panel)) {
        return recordAndReturn(PanelKind::InGameLevelUp, "InGameLevelUp");
    }

    return PanelKind::Unknown;
}

bool IsPanelKindInGameMenu(void* panel) {
    return IdentifyPanel(panel) == PanelKind::InGameMenu;
}

bool HasActiveDialogPanel() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return false;
    int n = panelCount > 16 ? 16 : panelCount;
    for (int i = 0; i < n; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        switch (IdentifyPanel(p)) {
        case PanelKind::DialogCinematic:
        case PanelKind::DialogCinematicCopy:
        case PanelKind::DialogComputer:
        case PanelKind::DialogComputerCamera:
            return true;
        default:
            break;
        }
    }
    return false;
}

// CGuiInGame::PrevSWInGameGui — engine-internal sub-screen pop.
// Verified address from the RE database (Lane's gzf, exposed via
// k1_win_gog_swkotor.exe.xml: FUNCTION ENTRY_POINT="0062cdf0").
// __thiscall, this in ECX, no params, void return.
static constexpr uintptr_t kAddrPrevSWInGameGui = 0x0062cdf0;
typedef void (__thiscall* PFN_PrevSWInGameGui)(void* gui);

bool CallPrevSWInGameGui() {
    void* gui = ResolveGuiInGame();
    if (!gui) {
        acclog::Write("PrevSWInGameGui",
                      "skipped: CGuiInGame not resolvable yet");
        return false;
    }
    auto fn = reinterpret_cast<PFN_PrevSWInGameGui>(kAddrPrevSWInGameGui);
    fn(gui);
    acclog::Write("PrevSWInGameGui", "dispatched gui=%p", gui);
    return true;
}

// CGuiInGame::SwitchToSWInGameGui — engine's universal sub-screen opener.
// Same call shape the in-game hotkeys (M, I, J, …) use upstream after the
// client-app handler translates the keypress to a GUI_id. Our own
// OnSwitchToSWInGameGui detour at 0x62cf2d closes the prior sub-screen
// before this function pushes the new one.
static constexpr uintptr_t kAddrSwitchToSWInGameGui = 0x0062cf10;
typedef void (__thiscall* PFN_SwitchToSWInGameGui)(void* gui, int gui_id);

bool CallSwitchToSWInGameGui(int guiId) {
    void* gui = ResolveGuiInGame();
    if (!gui) {
        acclog::Write("SwitchToSWInGameGui",
                      "skipped: CGuiInGame not resolvable yet");
        return false;
    }
    auto fn = reinterpret_cast<PFN_SwitchToSWInGameGui>(
        kAddrSwitchToSWInGameGui);
    fn(gui, guiId);
    acclog::Write("SwitchToSWInGameGui", "dispatched gui=%p GUI_id=%d",
                  gui, guiId);
    return true;
}

// CGuiInGame::HideSWInGameGui @ 0x0062cba0. Engine's universal sub-screen
// close primitive — invoked by CSWGuiInGameOptions::HandleInputEvent
// (0x006aaec0) with param_1=0 when Esc dismisses the in-game save/load
// menu. Empirically that path fully resyncs audio/pause; the matching
// MessageBoxModal close path skips HideSWInGameGui and leaves the world
// half-paused, which is what this call is meant to fix.
//
// __thiscall, this in ECX, single int parameter on the stack, undefined4
// return.
static constexpr uintptr_t kAddrHideSWInGameGui = 0x0062cba0;
typedef int (__thiscall* PFN_HideSWInGameGui)(void* gui, int param_1);

bool CallHideSWInGameGui(int param_1) {
    void* gui = ResolveGuiInGame();
    if (!gui) {
        acclog::Write("HideSWInGameGui",
                      "skipped: CGuiInGame not resolvable yet");
        return false;
    }
    auto fn = reinterpret_cast<PFN_HideSWInGameGui>(kAddrHideSWInGameGui);
    __try {
        fn(gui, param_1);
        acclog::Write("HideSWInGameGui",
                      "dispatched gui=%p param_1=%d", gui, param_1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("HideSWInGameGui",
                      "fault dispatching gui=%p param_1=%d", gui, param_1);
        return false;
    }
}

bool HasActiveMapPanel(void** outPanel) {
    if (outPanel) *outPanel = nullptr;
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = 0;
    void** panelData = nullptr;
    __try {
        panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
        panelData  = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!panelData || panelCount <= 0) return false;
    int n = panelCount > 16 ? 16 : panelCount;
    for (int i = 0; i < n; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        if (IdentifyPanel(p) == PanelKind::InGameMap) {
            if (outPanel) *outPanel = p;
            return true;
        }
    }
    return false;
}

bool HasActiveSubScreen() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return false;
    int n = panelCount > 16 ? 16 : panelCount;
    for (int i = 0; i < n; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        switch (IdentifyPanel(p)) {
        case PanelKind::InGameEquip:
        case PanelKind::InGameInventory:
        case PanelKind::InGameCharacter:
        case PanelKind::InGameAbilities:
        case PanelKind::InGameMessages:
        case PanelKind::InGameJournal:
        case PanelKind::InGameMap:
        case PanelKind::InGameOptions:
            return true;
        default:
            break;
        }
    }
    return false;
}

bool IsForegroundUiBlocking(UiBlockState* outState) {
    if (outState) *outState = UiBlockState{};

    // (1) Dialog panel anywhere in panels[]. Dialog reply turns briefly
    // swap fg to a transient Fade overlay while the actual CSWGuiDialog*
    // panel stays in panels[]; a pure fg-kind check misses that window.
    if (HasActiveDialogPanel()) {
        if (outState) outState->reason = UiBlockReason::DialogInStack;
        return true;
    }

    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    void* fgPanel = GetForegroundPanel(mgr);
    if (!fgPanel) return false;
    PanelKind fgKind = IdentifyPanel(fgPanel);

    if (outState) {
        outState->fgPanel = fgPanel;
        outState->fgKind  = fgKind;
    }

    // (2) Foreground is modal_stack[top] — engine has elevated it to capture
    // all input. Catches every PushModalPanel target without enumeration.
    auto* mgrBase = reinterpret_cast<unsigned char*>(mgr);
    int   modalSize = *reinterpret_cast<int*>(
        mgrBase + kMgrModalStackSizeOffset);
    void** modalData = *reinterpret_cast<void***>(
        mgrBase + kMgrModalStackDataOffset);
    if (modalSize > 0 && modalData && modalData[modalSize - 1] == fgPanel) {
        if (outState) {
            outState->reason        = UiBlockReason::ForegroundModal;
            outState->modalStackTop = modalSize - 1;
        }
        return true;
    }

    // (3) Foreground kind blacklist. InGameMenu strip stays foreground while
    // any sub-screen (Inventory / Map / Equip / …) is drilled — covers them
    // transitively. Strip itself routes Enter to its chain handler in
    // menus.cpp; in-world hotkeys never make sense while a menu is open.
    switch (fgKind) {
    case PanelKind::Container:
    case PanelKind::Store:
    case PanelKind::Examine:
    case PanelKind::DialogCinematic:
    case PanelKind::DialogCinematicCopy:
    case PanelKind::DialogComputer:
    case PanelKind::DialogComputerCamera:
    case PanelKind::TutorialBox:
    case PanelKind::MessageBoxModal:
    case PanelKind::StatusSummary:
    case PanelKind::AreaTransition:
    case PanelKind::InGameMenu:
        if (outState) outState->reason = UiBlockReason::ForegroundBlockingKind;
        return true;
    default:
        return false;
    }
}

}  // namespace acc::engine
