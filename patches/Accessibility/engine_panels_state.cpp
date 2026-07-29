// foreground / UI-blocking model and GUI input-class plumbing.
//
// Split out of engine_panels.cpp by the Phase-1 structure pass
// (refactoring candidate 6). engine_panels.cpp stays the panel-identity
// registry - structural detectors, the PanelKind offset table,
// IdentifyPanel and the PanelKind-based classification predicates. This
// file owns the runtime-state questions layered on top of that registry:
//
//   * is a dialog / bark bubble / map / level-up panel up right now
//   * show / hide the in-game GUI and close back to the world
//   * get / set the GUI input class
//   * IsForegroundUiBlocking - the mod-wide "is the UI eating input" gate
//
// Declarations stay in engine_panels.h, so all 39 includers are
// unaffected.

#include "engine_panels.h"
#include "engine_panels_internal.h"
#include "engine_rebase.h"

#include <windows.h>  // SEH __try / __except
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_reads.h"
#include "log.h"

namespace acc::engine {

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

bool HasActiveBarkBubble() {
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
        if (IdentifyPanel(p) == PanelKind::BarkBubble) return true;
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

// CGuiInGame::SetGlobalDialogState @ 0x0062ec60. __thiscall(this, int). The
// engine sets this to 1 when a conversation is starting and back to 0 when it
// ends or aborts. ActionInitiateDialog sets it to 1 *before* the walk-to-talk;
// if that approach is blocked and we cancel it, the bit can be left stuck at 1
// (engine never runs AIActionDialogObject's bail that would clear it), which
// then gates further click/interact processing. The dialog-approach watchdog
// calls this with 0 after cancelling a blocked approach to avoid that limbo.
static constexpr uintptr_t kAddrSetGlobalDialogState = 0x0062ec60;
typedef void (__thiscall* PFN_SetGlobalDialogState)(void* gui, int state);

bool SetGlobalDialogState(int state) {
    void* gui = ResolveGuiInGame();
    if (!gui) {
        acclog::Write("GlobalDialogState",
                      "skipped: CGuiInGame not resolvable yet");
        return false;
    }
    auto fn = reinterpret_cast<PFN_SetGlobalDialogState>(kAddrSetGlobalDialogState);
    __try {
        fn(gui, state);
        acclog::Write("GlobalDialogState", "set state=%d gui=%p", state, gui);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("GlobalDialogState", "fault setting state=%d gui=%p", state, gui);
        return false;
    }
}

// CClientExoApp::SetInputClass @ 0x005eda60. __thiscall(this, int klass, int).
// klass 0 = in-world keyboard/mouse routing; the engine raises it while a
// menu/sub-screen owns input.
static constexpr uintptr_t kAddrSetInputClass = 0x005eda60;
typedef void (__thiscall* PFN_SetInputClass)(void* client, int klass, int p2);

bool CloseInGameMenuToWorld() {
    void* appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
    void* gui = ResolveGuiInGame();
    void* client = appMgr ? *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(appMgr) + kAppManagerClientAppOffset) : nullptr;
    if (!gui || !client) {
        acclog::Write("CloseInGameMenu", "skipped: gui=%p client=%p", gui, client);
        return false;
    }
    __try {
        // Replicate the in-game menu tabs' own Escape close EXACTLY. Every
        // CSWGuiInGame*::HandleInputEvent (Inventory @0x006b3ed0, Options, Map,
        // Journal, …) closes to the world with:
        //     if (HideSWInGameGui(gui, 0) != 0) SetInputClass(client, 0, 1);
        // Calling HideSWInGameGui alone (our first cut) left input_class != 0,
        // so in-world movement was dead AND case 0xdf (Esc -> Options) reissued
        // to the manager instead of opening the menu — the stuck state in
        // patch-20260609-115959.log. SetInputClass(0,1) is the missing half.
        auto hide = reinterpret_cast<PFN_HideSWInGameGui>(kAddrHideSWInGameGui);
        int ok = hide(gui, 0);
        if (ok != 0) {
            auto setClass = reinterpret_cast<PFN_SetInputClass>(kAddrSetInputClass);
            setClass(client, 0, 1);
        }
        acclog::Write("CloseInGameMenu",
                      "HideSWInGameGui(0)=%d + SetInputClass(0,1) gui=%p", ok, gui);
        return ok != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("CloseInGameMenu", "fault gui=%p client=%p", gui, client);
        return false;
    }
}

// Read the live input_class (CClientExoAppInternal +0x9c). 0/4 = in-world,
// 2 = a menu/sub-screen owns input (mouse shown). Diagnostic + gating helper.
// Chain: *kAddrAppManagerPtr → +0x4 CClientExoApp → +0x4 Internal → +0x9c.
int GetInputClass() {
    __try {
        void* appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appMgr) return -1;
        void* client = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appMgr) + kAppManagerClientAppOffset);
        if (!client) return -1;
        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(client) + 0x04);
        if (!internal) return -1;
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(internal) + 0x9c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Set the client input class via the engine's own setter. klass 0 = in-world
// routing; 2 = menu/GUI (mouse shown), the value the in-game screens run in.
// Exposed so the level-up wizard — which we open WITHOUT the full
// ShowSWInGameGui — can put the client into GUI input mode itself. Without it,
// physical keys stay world-coded (L/U/J → 208/214/221) and never translate to
// the manager's nav codes (181-185), so the wizard sits unreachable on the
// modal stack (the "frozen until Escape" limbo).
bool SetGuiInputClass(int klass) {
    void* appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
    void* client = appMgr ? *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(appMgr) + kAppManagerClientAppOffset) : nullptr;
    if (!client) {
        acclog::Write("InputClass", "SetGuiInputClass(%d) skipped: no client", klass);
        return false;
    }
    __try {
        reinterpret_cast<PFN_SetInputClass>(kAddrSetInputClass)(client, klass, 1);
        acclog::Write("InputClass", "SetInputClass(%d,1) -> now=%d",
                      klass, GetInputClass());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("InputClass", "SetGuiInputClass(%d) faulted", klass);
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

bool HasActiveLevelUpPanel() {
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
        if (IdentifyPanel(p) == PanelKind::InGameLevelUp) return true;
    }
    return false;
}

bool IsInGameOptionsSubScreen(void* panel) {
    if (!panel) return false;
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
        if (!p || p == panel) continue;
        if (IdentifyPanel(p) == PanelKind::InGameOptions) {
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
