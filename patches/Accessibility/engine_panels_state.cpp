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

#include "engine_game.h"     // IsKotor1 — the client-internal state plumbing
#include "engine_manager.h"
#include "log.h"

namespace acc::engine {

bool HasActiveDialogPanel() {
    // SEH-guarded like every other panels[] walk in this TU: the null checks
    // below cannot catch a STALE manager/panel pointer, which is exactly what
    // this array holds during a module teardown or cutscene handoff.
    constexpr int kCap = 32;
    void* panels[kCap];
    int n = ReadPanelArray(GetGuiManager(), panels, kCap);
    __try {
        for (int i = 0; i < n; ++i) {
            void* p = panels[i];
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

bool HasActiveBarkBubble() {
    return FindPanelByKind(PanelKind::BarkBubble) != nullptr;
}

// CGuiInGame::PrevSWInGameGui — engine-internal sub-screen pop.
// Verified address from the RE database (Lane's gzf, exposed via
// k1_win_gog_swkotor.exe.xml: FUNCTION ENTRY_POINT="0062cdf0").
// __thiscall, this in ECX, no params, void return.
// KOTOR 2: one of a twin pair of 389-byte functions; 0x007CA3C0 DECREMENTS
// last_gui_panel with -1 -> 7 wrap (Prev), its twin 0x007CA230 increments
// with 8 -> 0 wrap (Next). Byte-read 2026-08-01 — do not swap them.
static const uintptr_t kAddrPrevSWInGameGui = acc::addr::Pick(0x0062cdf0, 0x007CA3C0);
typedef void (__thiscall* PFN_PrevSWInGameGui)(void* gui);

bool CallPrevSWInGameGui() {
    if (!acc::addr::Ok(kAddrPrevSWInGameGui)) {
        acclog::Write("PrevSWInGameGui", "skipped: address unresolved on build %s",
                      acc::addr::ActiveBuildName());
        return false;
    }
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
// KOTOR 2 identity decompile-confirmed 2026-08-01: modal-stack early-out,
// SetSWGuiStatus(4,1), strip+panel removal, PlayGuiSound(5), SetSoundMode(0)
// — every KOTOR 1 landmark, same order. Same signature.
static const uintptr_t kAddrHideSWInGameGui = acc::addr::Pick(0x0062cba0, 0x007CA060);
typedef int (__thiscall* PFN_HideSWInGameGui)(void* gui, int param_1);

bool CallHideSWInGameGui(int param_1) {
    if (!acc::addr::Ok(kAddrHideSWInGameGui)) {
        acclog::Write("HideSWInGameGui", "skipped: address unresolved on build %s",
                      acc::addr::ActiveBuildName());
        return false;
    }
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
static const uintptr_t kAddrSetGlobalDialogState = acc::addr::R(0x0062ec60);
typedef void (__thiscall* PFN_SetGlobalDialogState)(void* gui, int state);

bool SetGlobalDialogState(int state) {
    if (!acc::addr::Ok(kAddrSetGlobalDialogState)) {
        acclog::Write("GlobalDialogState", "skipped: address unresolved on build %s",
                      acc::addr::ActiveBuildName());
        return false;
    }
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
// KOTOR 2: the facade at 0x0073FEE0, witnessed by the engine's own tab-close
// paths calling it with (0,1)/(1,1) on the client facade; it forwards to the
// internal implementation at 0x007B3050 exactly as KOTOR 1's facade jumps to
// its internal. Both games take (klass, p2).
static const uintptr_t kAddrSetInputClass = acc::addr::Pick(0x005eda60, 0x0073FEE0);
typedef void (__thiscall* PFN_SetInputClass)(void* client, int klass, int p2);

bool CloseInGameMenuToWorld() {
    // Both halves are required — closing without the SetInputClass leaves
    // input_class != 0 and in-world movement dead (see the comment inside), so
    // if either address is unresolved, do neither.
    if (!acc::addr::Ok(kAddrHideSWInGameGui) || !acc::addr::Ok(kAddrSetInputClass)) {
        acclog::Write("CloseInGameMenu", "skipped: address unresolved on build %s",
                      acc::addr::ActiveBuildName());
        return false;
    }
    void* gui = ResolveGuiInGame();
    void* client = GetClientApp();
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
// Chain: GetClientAppInternal() → +0x9c.
int GetInputClass() {
    void* internal = GetClientAppInternal();
    if (!internal) return -1;
    __try {
        // Verified Same on KOTOR 2 (2026-08-01), by two independent
        // witnesses: KOTOR 1's own GetInputClass facade thunk reads
        // [internal+0x9c], and KOTOR 2's SetInputClass internal
        // (0x007B3050) guards and stores the same [this+0x9c] field.
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(internal) +
            kClientInternalInputClassOffset);
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
    // Address check first, mirroring CloseInGameMenuToWorld above. It matters
    // more since Batch 2: GetClientApp() now returns a real pointer on KOTOR 2,
    // so without this the function would reach the CALL and fault into its own
    // SEH on every invocation rather than declining cheaply.
    if (!acc::addr::Ok(kAddrSetInputClass)) {
        acclog::Write("InputClass",
                      "SetGuiInputClass(%d) skipped: address unresolved on "
                      "build %s", klass, acc::addr::ActiveBuildName());
        return false;
    }
    void* client = GetClientApp();
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
    constexpr int kCap = 32;
    void* panels[kCap];
    int n = ReadPanelArray(GetGuiManager(), panels, kCap);
    for (int i = 0; i < n; ++i) {
        void* p = panels[i];
        if (!p) continue;
        if (IdentifyPanel(p) == PanelKind::InGameMap) {
            if (outPanel) *outPanel = p;
            return true;
        }
    }
    return false;
}

bool HasActiveLevelUpPanel() {
    constexpr int kCap = 32;
    void* panels[kCap];
    int n = ReadPanelArray(GetGuiManager(), panels, kCap);
    for (int i = 0; i < n; ++i) {
        void* p = panels[i];
        if (!p) continue;
        if (IdentifyPanel(p) == PanelKind::InGameLevelUp) return true;
    }
    return false;
}

bool IsInGameOptionsSubScreen(void* panel) {
    if (!panel) return false;
    constexpr int kCap = 32;
    void* panels[kCap];
    int n = ReadPanelArray(GetGuiManager(), panels, kCap);
    for (int i = 0; i < n; ++i) {
        void* p = panels[i];
        if (!p || p == panel) continue;
        if (IdentifyPanel(p) == PanelKind::InGameOptions) {
            return true;
        }
    }
    return false;
}

bool HasActiveSubScreen() {
    // SEH-guarded — see the note on HasActiveDialogPanel.
    constexpr int kCap = 32;
    void* panels[kCap];
    int n = ReadPanelArray(GetGuiManager(), panels, kCap);
    __try {
        for (int i = 0; i < n; ++i) {
            void* p = panels[i];
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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
