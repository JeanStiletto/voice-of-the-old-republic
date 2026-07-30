// GUI input dispatch + focus-change hook handlers.
//
// Split out of menus.cpp by the Phase-1 structure pass (refactoring
// candidate 1). Two engine hooks live here:
//
//   * OnHandleFocusChange (CSWGuiControl::HandleFocusChange) - log only.
//   * OnHandleInputEvent (CSWGuiManager::HandleInputEvent) - the ordered
//     gate list every GUI keypress runs through. This is the single
//     largest function in the mod and the reason the file exists: it was
//     ~40% of menus.cpp and has no state of its own beyond the two
//     menus.cpp-owned flags it reads through menus_internal.h.
//
// The definitions are verbatim from menus.cpp. Gate ORDER is behaviour -
// each subsystem gets first refusal in a specific sequence - so nothing in
// this file may be reordered without an in-game input pass.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "prism.h"
#include "menus.h"
#include "menus_internal.h"
#include "menus_abilities.h"
#include "menus_chain.h"
#include "menus_chargen_feats.h"
#include "menus_editbox.h"
#include "menus_galaxymap.h"
#include "menus_keymap.h"
#include "menus_listbox.h"
#include "menus_modsettings.h"
#include "menus_monitors.h"
#include "menus_pazaakdeck.h"
#include "menus_powers_levelup.h"
#include "engine_input.h"
#include "engine_keymap.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "bringup_announce.h"
#include "cycle_input.h"
#include "help.h"
#include "hotkeys.h"
#include "input_pipeline.h"
#include "interact_dispatch.h"
#include "minigame_pazaak.h"
#include "peek_description.h"

using namespace acc::engine;

// Chain cursor state lives in menus_chain.cpp; the dispatch gates read it
// directly (and advance g_chainIndex on a chain step). Same
// using-declaration pattern menus.cpp uses so the dense reads below stay
// as they were.
using acc::menus::chain::g_chain;
using acc::menus::chain::g_chainPanel;
using acc::menus::chain::g_chainIndex;
using acc::menus::chain::g_chainCount;

// Forward decl from core_dllmain.cpp - both hooks below are entry points
// that may fire before anything else has initialised Prism.
void EnsurePrismInitialized();

// CSWGuiControl::HandleFocusChange — hooked mid-function at 0x41896b.
// Demoted to log-only. The panel-level SetActiveControl hook above is the
// real announcement signal; HandleFocusChange fires twice per navigation
// (old loses focus + new gains focus) so speaking from here would echo.
extern "C" void __cdecl OnHandleFocusChange(void* thisPtr, int param_1) {
    EnsurePrismInitialized();
    static int n = 0;
    ++n;
    const char* tip; uint32_t tipLen; int id;
    ReadControlNameFields(thisPtr, tip, tipLen, id);
    acclog::Write("Menus.FocusChange", "#%d this=%p p1=%d id=%d tip[%u]=\"%s\"",
                  n, thisPtr, param_1, id, tipLen,
                  (tip && tipLen > 0) ? tip : "");
}

// CSWGuiManager::HandleInputEvent — hooked mid-function at 0x0040c907.
// This is the GUI manager's central input dispatcher: every key / mouse event
// the engine routes to any GUI surface passes through here before being
// virtual-dispatched to the active panel's per-class override. One hook
// covers every screen (title, Options, chargen, in-game menus, dialog,
// save/load) — replaces the old CSWGuiMainMenu-only hook at 0x67b395.
//
// We hook BEFORE the param_2 == 0 early-out, so we see press AND release
// edges. param_2 is logged as `val=` (0 = release, non-zero = press).
//
// At hook entry: ECX = this, EBX = param_1 (InputIndices key/button code),
// EAX = param_2 (state).
extern "C" int __cdecl OnHandleInputEvent(void* thisPtr, int param_1, int param_2) {
    EnsurePrismInitialized();
    static int n = 0;
    ++n;
    // Shared seq counter — lets readers correlate Menus.Input lines with
    // Diag.ClientHIE entries to verify the val=1 vs val=128 routing
    // hypothesis from docs/in-game-menu-input-investigation.md. Bumped
    // once per call so a synthesised pair (upstream → manager) reads as
    // two adjacent seqs. The ProcessInput hook (see input_pipeline.h)
    // also bumps seq once per frame silently, so gaps in seq reflect
    // elapsed frames between events.
    unsigned int seq = acc::input::NextSeq();

    // Press-release pairing. When OUR handler consumes a press (Enter on
    // a chain entry → QueueActivate, Esc on a tabbed sub-dialog → drill
    // close, etc.), the engine never sees that press. But the matching
    // RELEASE event still arrives, and the engine's release path fires
    // onClick on whatever is focused at release-time — which is usually
    // a different control than at press-time, because our QueueActivate
    // just opened a sub-screen / drilled / etc.
    //
    // Concrete observed double-fire:
    //   * Enter on "Spiel laden" → QueueActivate opens SaveLoad → engine
    //     release fires onClick on the now-focused row 0 → first save
    //     auto-loads.
    //
    // Fix: track which key code we consumed on press, pair-consume the
    // matching release before the handler chain runs.
    //
    // Scope is intentionally narrow. We DO NOT pair-consume releases for
    // presses that our handler didn't consume — those presses reached
    // the engine and the engine expects to see the matching release for
    // its own state machine (e.g. press+release click cycle, key-held
    // tracking). Suppressing them would change vanilla behaviour for
    // keys we explicitly chose not to intercept, which is the wrong
    // direction architecturally: extra accessibility hooks shouldn't
    // perturb the engine's natural flow.
    //
    // Single-slot tracking is enough — the engine drives input events
    // strictly press-then-release per key, and any new press overwrites
    // the tracker (same logic as a hardware key-state register).
    //
    // Pre-wrapper-fix (PR-4 in docs/upstream-prs.md) builds saw both
    // press and release routed to the engine's RELEASE path due to
    // EFLAGS clobber. That bug accidentally masked the double-fire by
    // making the press path a no-op. Now both paths are correctly hit,
    // we have to clean up after our own consumption.
    static int s_lastConsumedPress = 0;

    // Helper used by every consume path to keep the press-release tracker
    // in sync. All early returns from this function — radial, peek,
    // listbox dispatcher, editbox, and the bottom-of-function fall-through
    // — funnel through this so a release can never race past stale
    // tracker state. Press: set the tracker to `param_1` if we consumed,
    // clear it otherwise (stale tracker from an earlier consumed press
    // mustn't survive into an unrelated release later). Release: leave
    // the tracker alone — it's cleared by the early-out at the top of
    // the next call.
    auto trackPress = [&](int rv) -> int {
        // Bringup handoff (primary, keyboard-reachable signal). Every
        // consume path funnels through here, so the first time our manager
        // hook consumes anything the engine's input pump is provably live
        // and routing to the GUI manager — during the starved bring-up
        // window this hook does not fire at all. Hand off so the bringup
        // nag transitions to Responsive and never re-arms the "still
        // loading / press Alt F4" warning. NotifyInputPumpLive is idempotent
        // (no-op once Responsive), so calling it per-consume is free.
        //
        // This replaces the old 2nd-SetActiveControl signal (see
        // OnSetActiveControl), which a keyboard-only player never produces:
        // our chain navigates via synthetic mouse-over (MoveMouseToPosition),
        // which fires no SetActiveControl, so the phase stayed stuck in
        // Loading and the nag mis-fired on every keypress forever.
        if (rv == 1) {
            acc::bringup_announce::NotifyInputPumpLive();
        }
        // Suppress tracker updates when called from PollHomeEndKeys'
        // synthesised path: that call has no matching engine-sent release,
        // and a non-consumed synthesised press would zero the tracker for
        // an unrelated still-pending real press waiting on its release.
        if (param_2 != 0 && !acc::menus::s_synthesizedNav) {
            s_lastConsumedPress = (rv == 1) ? param_1 : 0;
        }
        return rv;
    };

    if (param_2 == 0 && s_lastConsumedPress != 0 && s_lastConsumedPress == param_1) {
        int translated = acc::engine::ManagerTranslateCode(param_1);
        if (translated != param_1) {
            acclog::Write("Menus.Input",
                          "#%d seq=%u this=%p key=logical(%d) -> %s(%d) val=%d "
                          "PAIR-CONSUMED (matches consumed press)",
                          n, seq, thisPtr, param_1,
                          acc::engine::InputIndexName(translated), translated,
                          param_2);
        } else {
            acclog::Write("Menus.Input",
                          "#%d seq=%u this=%p key=%s(%d) val=%d "
                          "PAIR-CONSUMED (matches consumed press)",
                          n, seq, thisPtr, acc::engine::InputIndexName(param_1),
                          param_1, param_2);
        }
        s_lastConsumedPress = 0;
        return 1;
    }

    // ---- Modifier-shadow consume (manager side) ----------------------------
    // Mirror of the in-world consume in input_pipeline.cpp: the engine is
    // modifier-blind, so a mod hotkey that reuses an engine GUI key with a
    // modifier (e.g. a future Ctrl+rebind, or Shift+number while a panel owns
    // the keys) would otherwise also drive the engine's bare-key handling. When
    // a registered mod binding owns the combo on the physical key this code
    // represents, swallow the engine event. Press edge only; routed through
    // trackPress so the matching release is pair-consumed and the engine's
    // release path can't fire onClick on a now-different control.
    if (param_2 != 0) {
        int vks[4];
        int nv = acc::engine_keymap::VksForCode(param_1, vks, 4);
        for (int i = 0; i < nv; ++i) {
            if (acc::hotkeys::ModifiedComboOwns(vks[i])) {
                acclog::Write("Menus.Input",
                    "#%d seq=%u this=%p key=%s(%d) CONSUMED — modifier-shadowed "
                    "mod hotkey owns vk=0x%02x (mods=0x%x)",
                    n, seq, thisPtr, acc::engine::InputIndexName(param_1),
                    param_1, vks[i], acc::hotkeys::CurrentModifiers());
                return trackPress(1);
            }
        }
    }

    // Synthesised-Esc passthrough. CClientExoAppInternal::HandleInputEvent's
    // case 0xdf falls to LAB_00622111 when its in-world Esc handling can't
    // run (typically: a MessageBox is up, gating field45_0xb4 != 0). That
    // path reissues to the manager with a hard-coded `param_1=0xb4,
    // param_2=1`. The val=1 is the synthesis fingerprint — vanilla
    // DirectInput presses always carry val=128 (raw 0x80). So
    // (param_2 == 1) AND Esc-code is unique to upstream synthesis.
    //
    // Why pass through instead of acting on it: the engine is using this
    // path to deliver Esc to whatever modal is currently blocking input
    // (the MessageBox). The engine's natural panel dispatch will:
    //   1. Translate 0xb4 → 0x28 (Esc)
    //   2. Forward to modal_stack[top]'s HandleInputEvent
    //   3. Run the modal's own Esc handler — which closes the popup AND
    //      resets input_class / mouse-shown / sw_gui_status correctly via
    //      the engine's own pop-modal cleanup chain.
    //
    // Our previous behaviour (recognise MessageBox-cancel, queue
    // FireActivate(cancel), CONSUME) duplicated step 3's effect via a
    // different primitive that does NOT run the engine's cleanup chain.
    // After Esc-dismiss the user reported "walking + Enter break" — this
    // is the engine ending up in input_class=2 / mouse shown / etc. with
    // no popup left to drive cleanup. Verified live in
    // patch-20260510-093604.log @ seq=491.
    //
    // Skip the pair-consume tracker for synthesis events: we're not
    // consuming the press, so there's nothing for the matching release
    // (which goes back through upstream's case 0xdf, returns immediately
    // on val=0, never reaches us) to pair against.
    if (param_2 == 1 && (param_1 == kInputEsc1 || param_1 == kInputEsc2)) {
        acclog::Write("Menus.Input",
                      "#%d seq=%u this=%p key=logical(%d) val=1 "
                      "SYNTHESISED-PASSTHROUGH (upstream case 0xdf reissue)",
                      n, seq, thisPtr, param_1);
        return 0;
    }

    // Mod-settings virtual submenu pre-empt. While the submenu is open
    // its HandleInput owns navigation (Up/Down/Enter/Esc) and consumes
    // every other GUI key so they don't bleed through to the parent
    // panel. Runs ahead of the chain / radial / etc. dispatchers
    // because the submenu has no real engine panel — the parent
    // Optionen panel is still foreground and would otherwise eat the
    // keys we want for the virtual menu.
    if (param_2 != 0 && acc::menus::modsettings::IsOpen()) {
        if (acc::menus::modsettings::HandleInput(param_1)) {
            acclog::Write("Menus.Input",
                          "#%d seq=%u this=%p key=%s(%d) val=%d "
                          "MOD-SETTINGS-CONSUMED",
                          n, seq, thisPtr,
                          acc::engine::InputIndexName(param_1), param_1,
                          param_2);
            return trackPress(1);
        }
    }

    // Help list overlay + raw-F1 suppression. The F1 keybind list navigates
    // itself off Win32 edges (help::PollWin32); here we only stop the engine
    // events reaching the underlying panel.
    //
    //   * Raw F1 (always): a physical F1 arrives at the manager pre-translation
    //     as InputIndex 0x27 (kInputActivate) — Enter arrives as 0xb5/0xbb, so
    //     0x27 here is unambiguously F1. The engine reuses 0x27 as its GUI
    //     "activate" code, so an un-suppressed F1 would fire the focused
    //     control. Swallow it; help::PollWin32 owns F1's open/close.
    //   * While the list is open: swallow the nav / Enter / Esc / Home / End
    //     events so the underlying panel doesn't navigate or activate beneath
    //     the overlay (mirrors the mod-settings pre-empt above).
    if (param_2 != 0) {
        if (param_1 == kInputActivate) {
            acclog::Write("Menus.Input",
                          "#%d seq=%u this=%p key=F1(0x27 activate-code) "
                          "HELP-F1-SUPPRESSED", n, seq, thisPtr);
            return trackPress(1);
        }
        if (acc::help::IsMenuOpen()) {
            switch (param_1) {
            case kInputNavUp:   case kInputNavDown:
            case kInputNavLeft: case kInputNavRight:
            case kInputHome:    case kInputEnd:
            case kInputEnter1:  case kInputEnter2:
            case kInputEsc1:    case kInputEsc2:
                acclog::Write("Menus.Input",
                              "#%d seq=%u this=%p key=%s(%d) val=%d HELP-CONSUMED",
                              n, seq, thisPtr,
                              acc::engine::InputIndexName(param_1), param_1, param_2);
                return trackPress(1);
            default:
                break;
            }
        }
    }

    // Enter delivered to the manager belongs to the GUI, not to the world
    // Interact path. Claim the InteractTarget / InteractForceRadial rising
    // edges so acc::interact::PollHotkey() (which runs from the next
    // OnUpdate tick) can't re-fire on the same keypress.
    //
    // Without this, every Enter that ended a dialog reply would tear the
    // dialog down via the engine's native handler, then PollHotkey would see
    // the dialog gone (gate=ALLOW), pick the still-stamped narrated target
    // (Trask / Feldkiste / …) and dispatch the default world action on it.
    // patch-20260520-074837.log lines 10325→10341 captured a Trask cycle;
    // patch-20260520-083257.log line 1032 captured a Feldkiste door open
    // bleed-through after a clean dialog end.
    //
    // ClaimRisingEdge (not Consume) is required: the engine fires manager
    // input dispatch BETWEEN our EndTick and the next BeginTick. At that
    // moment both `now` and `last` still hold the previous tick's values,
    // so Consume(last=now) has no effect — the upcoming BeginTick will
    // sample `now=true` and Pressed sees a fresh rising edge. Claim sets
    // a guard bit that survives BeginTick and is cleared by EndTick.
    //
    // Scope: any Enter rising-edge that reaches the manager. We don't gate
    // on panel kind — the manager only sees input the engine intended for
    // GUI, so claiming the world-Interact edge here is always correct.
    if (param_2 != 0 && (param_1 == kInputEnter1 || param_1 == kInputEnter2)) {
        acc::hotkeys::ClaimRisingEdge(acc::hotkeys::Action::InteractTarget);
        acc::hotkeys::ClaimRisingEdge(acc::hotkeys::Action::InteractForceRadial);
    }

    // Resolve the foreground panel via the manager's modal_stack / panels[].
    // g_currentPanel tracks "last panel that received SetActiveControl" — fine
    // for per-instance state (sibling-label lookup, cycle-category capture)
    // but UNRELIABLE for routing, because flows that pre-instantiate multiple
    // panels in one frame (character creation: modal + 2 wizards) leave
    // g_currentPanel pointing at the last-walked panel, which is NOT the
    // visible foreground. Verified from patch-20260502-164320.log: in that
    // flow modal_stack.size goes 0→4 with the user-visible Standardcharakter
    // modal correctly at modal[top], while g_currentPanel had latched onto
    // a backgrounded wizard. See ManagerStack diagnostic and report.
    //
    // Fallback to g_currentPanel only when the manager pointer or the
    // foreground resolves to null (early-init frames before any panel
    // exists, or screens we don't yet understand).
    void* activePanel = nullptr;
    {
        void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
        void* fg = GetForegroundPanel(mgr);
        activePanel = fg ? fg : g_currentPanel;
        // First-fire-per-pair divergence log: when fg != g_currentPanel we
        // want to see it in the log, but only once per (fg, g_currentPanel)
        // tuple to avoid spamming during steady-state (every keypress in a
        // multi-panel flow would otherwise emit a line).
        if (fg && fg != g_currentPanel) {
            static void* s_lastFg = nullptr;
            static void* s_lastCp = nullptr;
            if (fg != s_lastFg || g_currentPanel != s_lastCp) {
                acclog::Write("Routing", "fg=%p current=%p (using fg)",
                              fg, g_currentPanel);
                s_lastFg = fg;
                s_lastCp = g_currentPanel;
            }
        }

        // Drill override: when the user has Entered into a sub-screen, retarget
        // the chain from the strip (kept in fg by SendPanelToBack) to the
        // sub-screen panel. Only fires when fg actually IS the strip — leaves
        // tutorial modals and Options sub-tabs (which become fg in their own
        // right) routing through fg directly.
        if (g_drilledIntoSubScreen) {
            if (IdentifyPanel(activePanel) == PanelKind::InGameMenu) {
                void* sub = acc::menus::monitors::FindActiveSubScreenPanel();
                if (sub) {
                    activePanel = sub;
                } else {
                    g_drilledIntoSubScreen = false;
                    acclog::Write("Drill", "sub-screen gone from panels[]; "
                                  "returning to strip");
                }
            }
        }
    }

    // Chain navigation: consume nav-up / nav-down on key-down. We only handle
    // press edges (param_2 != 0) so key-up events still pass through cleanly.
    // Other keys (Tab, Enter, mouse, F-keys) always pass through; activation
    // comes free from the engine via the normal click pipeline once the
    // cursor is over the chain target.
    bool consumed = false;

    // NOTE: the in-world action menu (unified_action_menu, Shift+Enter /
    // Shift+1..7) is NOT routed through this manager hook. It pauses the
    // world via an overlay hold and takes its nav/Enter/Esc keys through
    // interact_hotkey's Win32 poll; the engine-pause-menu open on Esc is
    // suppressed in input_pipeline. So nothing to gate here.

    // Shift+Up / Shift+Down description peek. Runs before any panel
    // handler that consumes Up/Down (Container, equip picker, generic
    // chain) so a held Shift is read as "peek the focused item's
    // description" rather than navigating rows. Panels not in the
    // peek registry pass through unchanged. See peek_description.h.
    //
    // Pass the chain's current focus pointer when valid — peek's
    // panel-specific refresh functions need it to re-stage the
    // description for the focused row (panel.activeControl tracks a
    // different helper control during chain nav, not the item entry).
    {
        void* peekFocus = nullptr;
        if (g_chainPanel == activePanel &&
            g_chainIndex >= 0 &&
            g_chainIndex < g_chainCount) {
            peekFocus = g_chain[g_chainIndex].control;
        }
        if (acc::peek::HandleShiftArrow(param_1, param_2, activePanel,
                                        peekFocus)) {
            return trackPress(1);
        }
    }

    // In-game Fähigkeiten screen — dedicated two-level handler (tab level:
    // Up/Down pick a tab, Enter drills in; list level: Up/Down browse, Esc
    // back). Runs before the listbox dispatcher and chain nav so it owns every
    // nav key on this screen; the engine's own paths for it are mouse-only or
    // crash-prone.
    {
        int rv = 0;
        if (acc::menus::abilities::HandleInput(n, thisPtr, activePanel,
                                               param_1, param_2, rv)) {
            return trackPress(rv);
        }
    }

    // Listbox-driven panels (Container loot, SaveLoad, EquipPicker)
    // dispatch through menus_listbox::TryHandleInput. Step 4 of the
    // refactor: three structurally similar handlers (~340 lines inline)
    // collapsed into a spec-table-driven dispatcher with one entry per
    // panel. See menus_listbox.h for the contract; the spec entries in
    // menus_listbox.cpp are where each panel's quirks (announce format,
    // Enter/Esc dispatch, fall-through behaviour) live.
    {
        int rv = 0;
        if (acc::menus::listbox::TryHandleInput(n, thisPtr, activePanel,
                                                 param_1, param_2, rv)) {
            return trackPress(rv);
        }
    }

    // Editbox (input field) — when the editbox spec is in edit mode it
    // claims Up/Down (re-speak text), Enter (submit), Esc (silent exit).
    // Letters / Backspace / Left / Right are not consumed and reach the
    // engine's editbox handler unchanged; the per-tick monitor catches
    // their effects via the (text, caret) diff. Sits before chain nav so
    // an in-edit-mode Up/Down re-read fires before chain nav would
    // otherwise step focus on the same key.
    {
        int rv = 0;
        if (acc::menus::editbox::TryHandleInput(n, thisPtr, activePanel,
                                                 param_1, param_2, rv)) {
            return trackPress(rv);
        }
    }

    // Chargen "Talente" main panel — 2D feat-tree chart navigation. Not
    // a listbox-shaped surface (the chart is a single chart-control
    // child of feats_listbox), so it has its own dispatcher rather than
    // a listbox spec entry. See menus_chargen_feats.h for the design.
    {
        int rv = 0;
        if (acc::menus::chargen_feats::HandleInput(
                n, thisPtr, activePanel, param_1, param_2, rv)) {
            return rv;
        }
    }

    // Level-up "Kr�fte" sub-panel (CSWGuiPowersLevelUp). The .gui calls it
    // a listbox at id 6 but its rows are CSWGuiSkillFlow tree-rows with up
    // to 3 cells per row (base / improved / master variant), so it needs
    // the chargen_feats-style 2D nav rather than a flat listbox spec. See
    // menus_powers_levelup.h for the design — also handles chargen Powers.
    {
        int rv = 0;
        if (acc::menus::powers_levelup::HandleInput(
                n, thisPtr, activePanel, param_1, param_2, rv)) {
            return rv;
        }
    }


    // Pillar 4 cycle keys (`,` `.` `Shift+,` `Shift+.` `-` `Shift+-`) — Phase 2
    // lay-off 3. Routed first because cycle is in-game-only and the handler
    // self-gates on GetPlayerPosition; in menus / chargen / dialog it returns
    // false and the key falls through to the normal menu logic below.
    if (acc::cycle_input::TryHandleEvent(param_1, param_2)) {
        consumed = true;
    }

    // Pazaak board game — dedicated arrow-zone navigator (your hand / your
    // board / opponent board / actions), same model as the deck builder. Owns
    // arrows + Enter for the board so the generic chain can't double-fire on
    // its Weiter/Halten buttons; letter shortcuts (s/e/c/t/Shift+C) are
    // Win32-polled in pazaak::Tick.
    {
        int rv = 0;
        if (acc::pazaak::TryHandleInput(activePanel, param_1, param_2, rv)) {
            return trackPress(rv);
        }
    }

    // Pazaak side-deck builder — dedicated 3-row arrow navigator (collection /
    // deck slots / controls). Owns Up/Down/Left/Right/Enter for this panel, so
    // it runs before the generic 1-D chain handlers below and consumes the key.
    {
        int rv = 0;
        if (acc::menus::pazaakdeck::TryHandleInput(activePanel, param_1, param_2, rv)) {
            return trackPress(rv);
        }
    }

    // Galaxy / star-map travel screen — dedicated single-axis handler. Owns
    // Up/Down (cycle revealed planets via the engine, which skips hidden /
    // unreachable ones), Enter (travel), Esc (cancel), and the remaining nav
    // keys (consumed as no-ops). Runs before the generic chain so the unnamed
    // planet buttons never leak into Up/Down nav as "control N". Shift+Up/Down
    // is handled earlier by peek_description (LBL_DESC read).
    {
        int rv = 0;
        if (acc::menus::galaxymap::TryHandleInput(activePanel, param_1,
                                                  param_2, rv)) {
            return trackPress(rv);
        }
    }

    // Keyboard-mapping screen — dedicated two-level submenu (tab level:
    // categories + OK/Cancel/Default; list level: browse + arm rebind) plus
    // pass-through while a key capture is armed. Runs before the generic chain
    // so it owns every nav key, mirroring the Fähigkeiten handler above. See
    // menus_keymap.h.
    {
        int rv = 0;
        if (acc::menus::keymap::HandleInput(activePanel, param_1, param_2, rv)) {
            return trackPress(rv);
        }
    }

    // Enter on the focused chain entry — picks the right activation primitive
    // (direct OnEnterSlot for equip/workbench slot, click-sim for tab buttons,
    // store-item-activate / journal-description for those rows, re-announce
    // for text-only entries, vtable[15] FireActivate otherwise). Lazy-rebinds
    // the chain if focus crossed panels so engine-pushed modals activate
    // without arrow-key priming. Logic in menus_chain::HandleEnterActivation.
    acc::menus::chain::HandleEnterActivation(activePanel, param_1, param_2,
                                             consumed);

    // Arrow keys + Home/End: flat chain navigation (announce + chargen sync +
    // cursor warp + per-row suffixes). Logic in menus_chain::HandleNavStep.
    acc::menus::chain::HandleNavStep(activePanel, param_1, param_2, consumed);

    // Left/Right dispatch (slider in/decrement or cycle-arrow flanker
    // activation, with portrait-panel override). Logic in
    // menus_chain::HandleLeftRight.
    acc::menus::chain::HandleLeftRight(activePanel, param_1, param_2, consumed);

    // Drill-Esc handler removed 2026-05-10 after the wrapper LEA-ESP fix
    // (extension to PR-4 in framework wrapper_x86_win32.cpp). Pre-fix the
    // engine's case 0x28 → HideSWInGameGui path on InGameOptions was
    // silently misrouted (selective POPAD's ADD ESP,4 clobbered ZF, manager
    // took press path on releases AND release path on presses), so we
    // synthesised "close" via PrevSWInGameGui. Post-fix the engine's
    // vanilla Esc-closes-sub-screen behaviour works correctly for all
    // sub-screens, AND PrevSWInGameGui turned out to actually CYCLE
    // through sub-screens rather than exit (function name was misleading).
    // Pass Esc through to the engine — vanilla closes pause cleanly.
    //
    // The drill flag still auto-clears via the existing branch in the
    // foreground-resolution block above (when fg becomes the InGameMenu
    // strip with no sub-screen alive in panels[]).

    // Esc dispatch (store override → workbench-upgrade override → generic
    // sub-dialog/modal close). Logic moved to menus_chain::HandleEsc so the
    // dispatcher reads as the linear stage list it always was.
    acc::menus::chain::HandleEsc(activePanel, param_1, param_2, consumed);

    int translated = acc::engine::ManagerTranslateCode(param_1);
    const char* tag = consumed ? " CONSUMED" : "";
    if (translated != param_1) {
        acclog::Write("Menus.Input", "#%d seq=%u this=%p key=logical(%d) -> %s(%d) val=%d%s",
                      n, seq, thisPtr, param_1,
                      acc::engine::InputIndexName(translated), translated, param_2, tag);
    } else {
        acclog::Write("Menus.Input", "#%d seq=%u this=%p key=%s(%d) val=%d%s",
                      n, seq, thisPtr, acc::engine::InputIndexName(param_1), param_1, param_2, tag);
    }
    return trackPress(consumed ? 1 : 0);
}
