#include "engine_panels.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine_manager.h"  // kAddrGuiManagerPtr, kMgrPanels*Offset
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

bool IsSaveLoadStructural(void* panel) {
    constexpr int kIdGamesListbox  =  0;
    constexpr int kIdDeleteButton  = 11;
    constexpr int kIdBackButton    = 12;
    constexpr int kIdSaveLoadButton = 14;
    void* lb = FindControlByGuiId(panel, kIdGamesListbox);
    if (!lb) return false;
    void** lbVtable = *reinterpret_cast<void***>(lb);
    if (reinterpret_cast<uintptr_t>(lbVtable) != kVtableListBox) return false;
    return FindControlByGuiId(panel, kIdSaveLoadButton) != nullptr &&
           FindControlByGuiId(panel, kIdBackButton)     != nullptr &&
           FindControlByGuiId(panel, kIdDeleteButton)   != nullptr;
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
    if (IsSaveLoadStructural(panel)) {
        return recordAndReturn(PanelKind::SaveLoad, "SaveLoad");
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

}  // namespace acc::engine
