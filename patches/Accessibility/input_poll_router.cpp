// per-tick Win32 hotkey router - see input_poll_router.h for what this is
// and why it is not merged into input_pipeline.cpp.
//
// Split out of interact_hotkey.cpp (now interact_dispatch.cpp) by the
// Phase-1 structure pass (refactoring candidate 19). The function body is
// verbatim; only the namespace changed, from acc::interact to
// acc::input_poll, because routing for a dozen subsystems was never
// interact-specific.
//
// The gate ORDER below is behaviour. Do not reorder without an in-game
// input pass.

#include "input_poll_router.h"
#include "interact_internal.h"
#include "interact_dispatch.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cmath>

#include "combat_query.h"
#include "combat_queue.h"
#include "engine_area.h"
#include "examine_view.h"
#include "engine_input.h"
#include "engine_levelup.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_picker.h"
#include "engine_player.h"
#include "floor_puzzle.h"
#include "hotkeys.h"
#include "input_pipeline.h"
#include "log.h"
#include "strings.h"
#include "prism.h"
#include "unified_action_menu.h"
#include "view_mode.h"

namespace acc::input_poll {

using namespace acc::interact;

void PollHotkey() {
    // Every rising-edge below comes from the central hotkey registry —
    // see hotkeys.h / hotkeys.cpp for the binding table. The registry
    // tracks per-Action `last` state internally so this function no
    // longer carries its own `s_prev*` statics.
    namespace hk = acc::hotkeys;

    bool risingEnterPlain = hk::Pressed(hk::Action::InteractTarget);
    // Shift+Enter and Shift+R are two independent keys for the same
    // force-radial action — either one arms it.
    bool risingEnterForce = hk::Pressed(hk::Action::InteractForceRadial) ||
                            hk::Pressed(hk::Action::InteractForceRadialSecondary);
    bool risingEnter      = risingEnterPlain || risingEnterForce;
    bool risingUp    = hk::Pressed(hk::Action::NavUp);
    bool risingDown  = hk::Pressed(hk::Action::NavDown);
    bool risingLeft  = hk::Pressed(hk::Action::NavLeft);
    bool risingRight = hk::Pressed(hk::Action::NavRight);
    bool risingHome  = hk::Pressed(hk::Action::NavHome);
    bool risingEnd   = hk::Pressed(hk::Action::NavEnd);
    bool risingK1    = hk::Pressed(hk::Action::TargetKey1);
    bool risingK2    = hk::Pressed(hk::Action::TargetKey2);
    bool risingK3    = hk::Pressed(hk::Action::TargetKey3);
    bool risingK4    = hk::Pressed(hk::Action::PersonalKey1);
    bool risingK5    = hk::Pressed(hk::Action::PersonalKey2);
    bool risingK6    = hk::Pressed(hk::Action::PersonalKey3);
    bool risingK7    = hk::Pressed(hk::Action::PersonalKey4);
    bool risingK8    = hk::Pressed(hk::Action::PersonalKey5);
    bool risingOpen1 = hk::Pressed(hk::Action::ActionBarOpen1);
    bool risingOpen2 = hk::Pressed(hk::Action::ActionBarOpen2);
    bool risingOpen3 = hk::Pressed(hk::Action::ActionBarOpen3);
    bool risingOpen4 = hk::Pressed(hk::Action::ActionBarOpen4);
    bool risingOpen5 = hk::Pressed(hk::Action::ActionBarOpen5);
    bool risingOpenT1 = hk::Pressed(hk::Action::TargetActionOpen1);
    bool risingOpenT2 = hk::Pressed(hk::Action::TargetActionOpen2);
    bool risingOpenT3 = hk::Pressed(hk::Action::TargetActionOpen3);
    bool risingL     = hk::Pressed(hk::Action::LevelUpOpen);
    bool risingEsc   = hk::Pressed(hk::Action::SubmenuEsc);

    // Pressed() already self-gates on foreground; if every action is
    // false we can still need to fall through to combat_query /
    // combat_queue (those self-gate). So don't early-return on no edges.

    // Menu-switch parity. The game's built-in menu hotkeys (J, I, M, …) switch
    // directly from one in-game menu screen to another. The action-menu openers
    // (Shift+Enter / Shift+1..7) now do the same: pressing one while the in-game
    // menu is open closes that menu back to the world first — the very close
    // every tab's own Escape runs (HideSWInGameGui(0) to drop the strip +
    // drilled sub-screen and unpause, then SetInputClass(0,1) to restore
    // in-world input; see CloseInGameMenuToWorld) — so the open logic below and
    // the Enter-dispatch gate then run in-world and arm the menu this same tick.
    // GetPlayerPosition reads the player server object (not the GUI status) so it
    // stays true across the close, and the one-shot opener edge isn't lost. Only
    // the in-game menu is switchable; message boxes / dialogs / stores stay hard
    // blockers where the openers still refuse — parity with the engine, whose
    // menu hotkeys don't switch out of those either.
    const bool openerPressed =
        risingEnterForce ||
        risingOpen1 || risingOpen2 || risingOpen3 || risingOpen4 ||
        risingOpenT1 || risingOpenT2 || risingOpenT3;
    if (openerPressed && ShouldSwitchFromInGameMenu()) {
        acclog::Write("Interact",
            "action-menu opener over in-game menu — closing to world (switch)");
        acc::engine::CloseInGameMenuToWorld();
    }

    Vector unused;
    bool inWorld = acc::engine::GetPlayerPosition(unused);

    // Panel-stack integration for the unified action menu. The menu holds a
    // world pause and routes nav / Enter / Esc through this Win32 poll
    // WITHOUT owning an engine GUI panel. When the engine pushes a real
    // blocking panel over the armed menu — a hotkey-opened sub-screen (Map,
    // Journal, …) or a MessageBox (quit-confirm, save-overwrite) — that panel
    // takes the foreground and input, yet the menu would otherwise stay armed
    // and keep consuming the same arrow / Enter keys, so the modal AND our
    // menu both react to one keypress (patch-20260609-111933.log — quit-confirm
    // nav double-spoke "Abbrechen" / "OK" alongside UnifiedMenu entries).
    //
    // Notify the menu of the current foreground-blocked state every tick. It
    // SUSPENDS (stops owning input, keeps its state + pause) while a blocker is
    // up and RESUMES at the same position when the blocker closes — matching
    // how native engine menus restore focus under a dismissed popup, rather
    // than closing outright. Same IsForegroundUiBlocking predicate the Enter-
    // dispatch gate below uses, so suspend/resume and Enter gating stay
    // consistent. The arm-time half of the gate lives in the menu's Open*
    // entry points (they refuse to arm over a blocker).
    acc::unified_menu::SetForegroundBlocked(
        acc::engine::IsForegroundUiBlocking());

    // Action-bar submenu — Shift+4..Shift+7 opens the column's variant
    // submenu (drives column up_button/down_button widgets via vtable[15]
    // activate). Tested before the radial-active block because the action
    // bar lives independently from the radial: a user could in principle
    // open the action-bar submenu while the radial is still armed. We
    // keep the routes distinct by always letting Shift+N take precedence —
    // pressing it while in the radial closes the radial gate (action-bar
    // open path doesn't disarm radial; the radial's own Tick() handles
    // the next disarm via "rows-empty"). Bare 4..7 fall straight through
    // to the engine-native fast-fire path.
    if (inWorld) {
        // Menu-switch: a Shift+number opener pressed while the combat queue
        // (Shift+H) is open switches cleanly to the action menu — close the
        // queue first so it stops shadowing input, then the OpenPersonal /
        // OpenTarget calls below arm / re-point the unified menu this same tick.
        // The owner-tracked overlay pause keeps the world frozen across the
        // switch: the queue's EndOverlayPause clears only its own owner bit, and
        // the menu re-holds (or already holds) the pause in the same input frame,
        // so no world time passes. Mirrors the game's own J / I / M hotkeys
        // switching between screens — the queue, like those, has no engine panel
        // to defer to, so this precedence is purely ours to set.
        const bool numberOpener =
            risingOpen1 || risingOpen2 || risingOpen3 || risingOpen4 ||
            risingOpen5 ||
            risingOpenT1 || risingOpenT2 || risingOpenT3;
        if (numberOpener && acc::combat::queue::IsActive()) {
            acclog::Write("Interact",
                "Shift+number over combat queue — closing queue, switching to "
                "action menu");
            acc::combat::queue::ForceDisarm("switch-to-action-menu");
        }

        // Slot mapping is LINEAR — key N drives column N-4:
        //   key 4 / Shift+4 → slot 0  Friendly Force
        //   key 5 / Shift+5 → slot 1  Medical
        //   key 6 / Shift+6 → slot 2  Misc (Sonstiges)
        //   key 7 / Shift+7 → slot 3  Explosives (Sprengstoffe)
        // This matches the engine's own DoPersonalAction dispatch, proven
        // from a clean seabed log (patch-20260615-010243): bare 6 fired the
        // Schallgenerator in Sonstiges while bare 7 hit the empty Explosives
        // column. The earlier "engine swaps 6↔7" belief was wrong — it had
        // been read off our own announce, not a real `benutzt` line — and it
        // left the announce/menu pointing at the opposite column from what
        // the engine actually fired (press 7 for Sonstiges, get Explosives).
        if (risingOpen1) acc::unified_menu::OpenPersonal(0);
        if (risingOpen2) acc::unified_menu::OpenPersonal(1);
        if (risingOpen3) acc::unified_menu::OpenPersonal(2);
        if (risingOpen4) acc::unified_menu::OpenPersonal(3);
        // Shift+8 — KOTOR 2's fifth personal column (combat behaviour). It has
        // no engine twin to stay in step with: neither game binds an
        // action-bar action to key 8 (1..9 are the dialogue-reply keys), so
        // this opener is purely the mod's. On KOTOR 1 the column is never
        // populated, so the open simply declines.
        if (risingOpen5) acc::unified_menu::OpenPersonal(4);

        // Shift+1..3 — open the unified menu on a target-action row. Direct
        // row mapping (1→row 0, 2→row 1, 3→row 2); the engine routes target
        // keys linearly via DoTargetAction.
        if (risingOpenT1) acc::unified_menu::OpenTarget(0);
        if (risingOpenT2) acc::unified_menu::OpenTarget(1);
        if (risingOpenT3) acc::unified_menu::OpenTarget(2);

        // Shift+L — open the engine's level-up panel directly
        // (CGuiInGame::ShowLevelUpGUI). First-version escape hatch for
        // the tutorial level: navigating into the Charakterblatt and
        // hitting btn_levelup is the vanilla path, but currently the
        // user's chain-walker Enter on the InGameAbilities Powers tab
        // crashes (CSWGuiInGameAbilities::OnEnterPower null deref) —
        // see logs/swkotor.exe.7848.dmp. Bypassing navigation via the
        // engine surface lets the user reach the level-up panel
        // regardless of which screen they're on. The level-up panel
        // itself enumerates as a normal CSWGuiPanel so the existing
        // chain walker handles its child controls once it opens.
        if (risingL) {
            // Dedupe: ShowLevelUpGUI allocates a fresh CSWGuiLevelUpPanel
            // on every dispatch with no engine-side "already open" check,
            // so key-repeat / fast double-tap stacks duplicate modals on
            // CSWGuiManager.modal_stack that the user can't unwind (Esc
            // only pops one at a time; each underlying instance still
            // owns the foreground). See patch-20260530-112606.log — twelve
            // Shift+L presses in four seconds pushed panels.size 3 → 25.
            if (acc::engine::HasActiveLevelUpPanel()) {
                prism::Speak(
                    acc::strings::Get(acc::strings::Id::LevelUpAlreadyOpen),
                    /*interrupt=*/true);
                acclog::Write("Interact",
                    "Shift+L -> already-open guard, skipping dispatch");
            } else if (!acc::engine_levelup::PlayerCanLevelUp()) {
                // Respect the engine's btn_levelup enabled state: the leader
                // hasn't earned the next level (or is level-capped). Without
                // this the forced level_up_mode=1 opened the wizard anyway,
                // letting the player level up endlessly.
                prism::Speak(
                    acc::strings::Get(acc::strings::Id::LevelUpNotReady),
                    /*interrupt=*/true);
                acclog::Write("Interact",
                    "Shift+L -> CanLevelUp=0, refusing (not enough XP / capped)");
            } else {
                const char* opener = acc::strings::Get(
                    acc::strings::Id::LevelUpOpen);
                prism::Speak(opener, /*interrupt=*/true);
                bool ok = acc::engine_levelup::TriggerLevelUp();
                acclog::Write("Interact", "Shift+L -> [%s] level-up dispatch ok=%d",
                    opener, ok ? 1 : 0);
                if (!ok) {
                    prism::Speak(
                        acc::strings::Get(acc::strings::Id::LevelUpFailed),
                        /*interrupt=*/true);
                }
            }
        }
    }

    // Bare 1..3 (target action menu) and bare 4..7 (player action bar)
    // announce path. The engine fires the action through its DirectInput
    // handler regardless of what we do; this branch only adds the
    // screen-reader announcement so the user knows what they fired
    // ("Medikit eingesetzt", "Sicherheit eingesetzt"). No engine-side
    // suppression — both paths run in parallel, same arrangement as
    // passive_narrate alongside Q/E target cycles.
    //
    // We don't gate on the panel-blocker / dialog-panel check used by
    // Enter dispatch. The engine itself filters bare 1..7 in those
    // contexts (e.g. inside a menu screen) — when it doesn't fire, our
    // announce reads a stale label or speaks the empty-column phrase,
    // both of which are recoverable. Matching the gate exactly would
    // require tracking the engine's own input-mode flags, which we don't
    // currently expose.
    //
    // We deliberately do NOT gate this on unified_menu::IsActive(). The
    // unified action menu stays open as a persistent, paused queueing
    // surface: a common workflow is to open it, fire one action via Enter,
    // then spam bare 1 a few times to stack default attacks before Esc'ing
    // out (and repeat per party member). Those bare presses reach the engine
    // dispatch unconditionally (input_pipeline's bare-key prep has no menu
    // gate) and queue normally — but the menu only speaks on Enter, never on
    // number keys, so without announcing here the queued action lands
    // silently. There's no double-announce risk: the menu's HandleInputEvent
    // is never fed number keys (interact_hotkey forwards only Enter / arrows /
    // Home / End / Esc to it), so this poll path is the sole announcer for
    // bare 1..7 whether or not the menu is open.
    //
    // Dialog gate: when a dialog reply listbox is foreground the number
    // keys belong to the reply selection, not the action bar. The engine's
    // combat dispatch is suppressed in that context (see input_pipeline.cpp
    // OnClientHandleInputEvent's matching HasActiveDialogPanel guard), so
    // announcing "X eingesetzt" here would be a phantom cue for an action
    // that never fired. Skip the announce while a dialog owns the keys.
    if (inWorld && !acc::engine::HasActiveDialogPanel()) {
        if (risingK1) AnnounceBareTargetKey(0);
        if (risingK2) AnnounceBareTargetKey(1);
        if (risingK3) AnnounceBareTargetKey(2);
        // Linear slot mapping (key N → column N-4) — matches the engine's
        // bare-key dispatch; see the Open mapping block above.
        if (risingK4) AnnounceBarePersonalKey(0);
        if (risingK5) AnnounceBarePersonalKey(1);
        if (risingK6) AnnounceBarePersonalKey(2);
        if (risingK7) AnnounceBarePersonalKey(3);
        // Bare 8 FIRES rather than announces. 4..7 report what the engine
        // just dispatched; nothing dispatches on 8, so the mod has to do it
        // (see unified_menu::FirePersonal).
        if (risingK8) acc::unified_menu::FirePersonal(4);
    }

    // Combat system, Phase 2C — Ö opens the navigable examine view
    // (synthetic in-DLL listbox). Toggle: pressing again while open closes.
    acc::examine_view::PollWin32Hotkey();

    // Combat system, Phase 3A — Shift+H opens the action-queue submenu.
    // The Open path also self-gates internally; route the submenu's
    // input dispatch below so Up/Down/Enter/Esc reach it while armed.
    acc::combat::queue::PollWin32Hotkey();

    // Bare H — quick HP / effects / equipped-weapon readout for the
    // currently-controlled leader. Self-gates on player-loaded and
    // UI-block (matching Tab leader-announce).
    acc::combat::query::PollWin32SelfStatusHotkey();

    // Examine view input routing — runs FIRST so an open examine view
    // wins arrow / Enter / Esc keys over any other in-world consumer.
    if (inWorld && acc::examine_view::IsActive()) {
        if (risingEnter) {
            acc::examine_view::HandleInputEvent(kInputEnter1, /*value=*/1);
        }
        if (risingUp) {
            acc::examine_view::HandleInputEvent(kInputNavUp, 1);
        }
        if (risingDown) {
            acc::examine_view::HandleInputEvent(kInputNavDown, 1);
        }
        if (risingEsc) {
            acc::examine_view::HandleInputEvent(kInputEsc1, 1);
            acc::input::NoteOverlayEscClosed();
        }
        return;
    }

    // Combat-queue submenu input routing — runs BEFORE actionbar so the
    // queue submenu wins ties (it's a more recent context). Mirrors the
    // actionbar route: Up/Down/Enter/Esc are translated into engine
    // logical input codes and dispatched at the gate handler.
    if (inWorld && acc::combat::queue::IsActive()) {
        if (risingEnter) {
            acc::combat::queue::HandleInputEvent(kInputEnter1, /*value=*/1);
        }
        if (risingUp) {
            acc::combat::queue::HandleInputEvent(kInputNavUp, 1);
        }
        if (risingDown) {
            acc::combat::queue::HandleInputEvent(kInputNavDown, 1);
        }
        if (risingEsc) {
            acc::combat::queue::HandleInputEvent(kInputEsc1, 1);
            acc::input::NoteOverlayEscClosed();
        }
        return;
    }

    // Unified action menu (Shift+Enter / Shift+1..7) — route every nav +
    // dispatch key here while it's armed. In-world Enter / arrows / Home /
    // End bypass CSWGuiManager (the engine keymap drops these unbound
    // scancodes per memory project_inworld_input_pipeline), so we translate
    // the Win32 edges directly into the menu's logical vocabulary. Esc is
    // caught here too (the menu pauses the world via an overlay hold; the
    // engine-pause-menu open is separately suppressed in input_pipeline).
    // Ctrl+Home / Ctrl+End map to the category-jump codes; plain Home / End
    // jump within the current category.
    if (inWorld && acc::unified_menu::IsActive() &&
        !acc::unified_menu::IsSuspended()) {
        const bool ctrl = acc::hotkeys::CtrlHeld();
        if (risingEnter) acc::unified_menu::HandleInputEvent(kInputEnter1, 1);
        if (risingUp)    acc::unified_menu::HandleInputEvent(kInputNavUp, 1);
        if (risingDown)  acc::unified_menu::HandleInputEvent(kInputNavDown, 1);
        if (risingLeft)  acc::unified_menu::HandleInputEvent(kInputNavLeft, 1);
        if (risingRight) acc::unified_menu::HandleInputEvent(kInputNavRight, 1);
        if (risingHome)  acc::unified_menu::HandleInputEvent(
                             ctrl ? kInputCatFirst : kInputHome, 1);
        if (risingEnd)   acc::unified_menu::HandleInputEvent(
                             ctrl ? kInputCatLast : kInputEnd, 1);
        if (risingEsc) {
            acc::unified_menu::HandleInputEvent(kInputEsc1, 1);
            acc::input::NoteOverlayEscClosed();
        }
        return;
    }

    // ---- Bare R: narrate the native default action -----------------------
    // Vanilla R ("default action on current target", engine case 0xef) still
    // fires in-world on its own — the engine dispatches it against last_target.
    // We don't reimplement that (Enter already covers the narrated-target set);
    // we only add the spoken pre-roll so an R-presser hears what R just did.
    // Read-only: ReadCurrent snapshots the engine's live default-action
    // descriptor without driving anything, so there's no double-dispatch.
    // Skipped where another consumer owns bare R (dialog reply-repeat via
    // dialog_speech, puzzle board readout via floor_puzzle) or where native R
    // can't act (a UI panel is foreground). Reaching here already means no mod
    // overlay is armed (their routing blocks returned above).
    if (inWorld && hk::Pressed(hk::Action::DialogRepeatLine) &&
        !acc::engine::HasActiveDialogPanel() &&
        !acc::floor_puzzle::IsActive() &&
        !acc::engine::IsForegroundUiBlocking()) {
        acc::picker::ActionSnapshot snap = {};
        if (acc::picker::ReadCurrent(&snap) && snap.valid && snap.label[0]) {
            char name[128] = "";
            void* obj = acc::engine::ResolveClientObjectHandle(snap.target_id);
            if (obj) acc::engine::GetObjectName(obj, name, sizeof(name));
            char msg[192];
            if (name[0]) {
                std::snprintf(msg, sizeof(msg),
                    acc::strings::Get(acc::strings::Id::FmtInteractEngine),
                    snap.label, name);
            } else {
                std::snprintf(msg, sizeof(msg), "%s", snap.label);
            }
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("Interact", "R native-default announce -> [%s] "
                "action=0x%x target=0x%08x", msg, snap.action_id,
                snap.target_id);
        } else {
            acclog::Write("Interact", "R native-default announce — no valid "
                "descriptor (snap.valid=%d); silent", snap.valid ? 1 : 0);
        }
        // Not a return: R never overlaps Enter, and nothing below acts on the
        // R edge — fall through so a same-tick Enter (impossible on one key,
        // but cheap to allow) still routes normally.
    }

    // Non-radial path: Enter (with optional Shift) drives interact.
    if (!risingEnter) return;
    if (!inWorld) return;

    // View mode owns Enter / Shift+Enter routing while active — its hover
    // channel is the truth for what should be acted on, not cycle_state /
    // engine LastTarget. View_mode::Tick polls VK_RETURN itself and
    // dispatches into `DispatchInteract` (or `WalkTo` for empty cursor)
    // earlier in the OnUpdate ordering.
    //
    // Two cases to skip:
    //  1. View mode currently active (rare for Enter to reach here, but
    //     possible if PollEnter's foreground / active gates dropped
    //     somehow).
    //  2. View mode handed Enter off this tick (PollEnter exited view
    //     mode before dispatching, so IsActive() is now false even
    //     though view_mode owns this press). ConsumedEnterThisTick auto-
    //     clears so the flag can't outlive the tick. Verified failure
    //     mode if not gated: WalkTo dispatched then immediately
    //     preempted by OnInteract's stale-LastTarget Dialog action
    //     (patch-20260506-142103.log).
    if (acc::view_mode::IsActive() || acc::view_mode::ConsumedEnterThisTick()) {
        acclog::Write("Interact", "Enter rising — view mode owns this press, "
            "deferring to view_mode::Tick");
        return;
    }

    // The registry split the rising-edge into plain Enter vs Shift+Enter
    // up front (Action::InteractTarget vs Action::InteractForceRadial),
    // so `risingEnterForce` is authoritative — no need to re-poll Shift.
    bool forceRadial = risingEnterForce;
    const char* keyTag = forceRadial ? "Shift+Enter" : "Enter";

    // Gate on "no true-blocker panel is foreground". GetPlayerPosition only
    // confirms we're in-world; it doesn't tell us whether a UI panel is
    // routing input. Shared predicate IsForegroundUiBlocking() lives in
    // engine_panels — same blacklist used by party_leader_announce's Tab
    // gate so the two stay in sync.
    acc::engine::UiBlockState ui;
    if (acc::engine::IsForegroundUiBlocking(&ui)) {
        switch (ui.reason) {
        case acc::engine::UiBlockReason::DialogInStack:
            acclog::Write("Interact",
                          "%s gate -- BLOCKED, dialog panel in stack",
                          keyTag);
            break;
        case acc::engine::UiBlockReason::ForegroundModal:
            acclog::Write("Interact",
                          "%s gate -- BLOCKED, fg=%p kind=%s "
                          "(modal_stack[%d] top)",
                          keyTag, ui.fgPanel,
                          acc::engine::PanelKindName(ui.fgKind),
                          ui.modalStackTop);
            break;
        case acc::engine::UiBlockReason::ForegroundBlockingKind:
            acclog::Write("Interact",
                          "%s gate -- BLOCKED, fg=%p kind=%s",
                          keyTag, ui.fgPanel,
                          acc::engine::PanelKindName(ui.fgKind));
            break;
        default:
            break;
        }
        return;
    }
    if (ui.fgPanel) {
        acclog::Write("Interact", "%s gate -- ALLOW, fg=%p kind=%s",
                      keyTag, ui.fgPanel,
                      acc::engine::PanelKindName(ui.fgKind));
    }

    // Swallow the Enter that just confirmed a save-name editbox. That popup is
    // foreground but classifies as kind=Unknown, so the blocking gate above
    // lets Enter through; the editbox monitor (menus.TickMonitors, earlier this
    // same tick) set the latch when it saw the confirm Enter. Without this the
    // single confirm Enter also queues an ActionInitiateDialog on the narrated
    // target, which fires when the world unpauses on menu-exit. Self-expiring +
    // single-shot, so a genuine later Enter is unaffected.
    if (acc::input::ConsumeEditboxSubmitLatch()) {
        acclog::Write("Interact", "%s swallowed -- editbox submit closed a modal "
                      "this tick (save-name confirm); not dispatching interact",
                      keyTag);
        return;
    }

    OnInteract(forceRadial);
}

}  // namespace acc::input_poll
