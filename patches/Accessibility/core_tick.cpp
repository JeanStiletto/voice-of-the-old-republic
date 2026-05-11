// KOTOR Accessibility — mod-wide per-tick dispatcher.
//
// See core_tick.h for design overview. This TU owns the OnUpdate hook
// entry and the Dispatch() function that calls every subsystem's
// per-tick callback in canonical order.

#include <windows.h>

#include "core_tick.h"

#include "announce_degrees.h"
#include "audio_footstep_suppress.h"
#include "camera_announce.h"
#include "combat.h"
#include "combat_query.h"
#include "combat_queue.h"
#include "cycle_input.h"
#include "diag_engine_select.h"
#include "dialog_speech.h"
#include "engine_player.h"
#include "engine_subscreen.h"
#include "guidance_autowalk.h"
#include "interact_hotkey.h"
#include "menus.h"
#include "passive_narrate.h"
#include "probe_mouselook.h"
#include "probe_world_hover.h"
#include "radial_menu.h"
#include "spatial_change_detector.h"
#include "transitions.h"
#include "turn_announce.h"
#include "view_mode.h"

namespace acc::tick {

void Dispatch() {
    // Defensive — drop stale pointers before any handler dereferences them.
    acc::menus::ValidatePanels();

    // Menu-side change-detection monitors (focused control text, panel
    // contents, dialog/container/equip listbox selection) and the
    // container give-mode Win32 G-key poll.
    acc::menus::TickMonitors();

    // Pillar 4 cycle keys via Win32 polling. Stock kotor.ini doesn't bind
    // `,/./-`, so OnHandleInputEvent never sees them in-world (the engine's
    // keymap drops unbound scancodes before our manager-side hook).
    // GetAsyncKeyState reads OS-level keyboard state directly, edge-detects
    // rising edges, and self-gates on GetPlayerPosition. Verified
    // 2026-05-04 from patch-20260503-215023.log: 86 events captured at the
    // manager hook, zero with codes 103/104/105.
    acc::cycle_input::PollWin32();

    // Pillar 2 sub-feature D — AltGr speaks exact compass heading. Same
    // Win32-polling rationale as the cycle keys: AltGr is unbound in
    // stock kotor.ini, so the engine keymap drops the scancode before our
    // manager hook sees it.
    acc::announce_degrees::PollWin32();

    // Phase 4 lay-off 2 view-mode probe — Shift+AltGr toggles the engine
    // CClientOptions.mouse_look bit and announces the new state, so we
    // can observe whether forcing Mouse Look ON gives us a free virtual
    // cursor for view mode. On a toggle-to-ON, kicks off a synthetic
    // mouse-sweep state machine driven by TickSweep; the user listens
    // for spatial-audio pan to determine whether the engine reacts.
    // Diagnostic-only; rebinds or goes away when view-mode design is
    // locked.
    acc::probe_mouselook::PollWin32();
    acc::probe_mouselook::TickSweep();

    // Phase 4 lay-off 3 — view-mode skeleton. B toggles the "stop and
    // look around without budging the character" mode (lifecycle only;
    // keyboard-driven camera input lands in lay-off 4). Shift+B fires
    // the camera-behavior probe — snapshot CClientOptions for diffing
    // before / after a manual Caps Lock press, to find where the engine
    // stores Free Look state. Same Win32-polling rationale as the cycle
    // keys: B is unbound in stock kotor.ini.
    acc::view_mode::PollWin32();

    // Autowalk progress watchdog. No-op when no recent WalkTo dispatch;
    // emits at most two log lines (t+1s, t+3s) per dispatch to detect
    // "engine accepted but didn't move" — the canonical autowalk failure
    // mode (e.g. tutorial-locked sections, queue blocked by higher-priority
    // action). Permanent instrumentation; reused by every guidance caller.
    acc::guidance::TickProgressWatchdog();

    // Auto-restore player input ~3s after a guidance / interact dispatch
    // disabled it. Idle when no disable session is active. See
    // engine_player.h SetPlayerInputEnabled doc + memory entry
    // project_player_control_toggle.md for the why.
    acc::engine::TickPlayerInputRestore();

    // Phase 2 lay-off 9a — passive-selection narration loop. Reads engine
    // LastTarget per tick; on change to a nav-relevant target, speaks the
    // localised name + plays the per-category 3D cue at the object's
    // position. Independent of the Pillar 4 cycle channel — both can fire
    // on the same object; recency-suppress to be added if double-narration
    // proves disruptive. Self-gates on player-loaded.
    acc::passive_narrate::Tick();

    // Phase 2 ad-hoc — octagonal direction-on-turn announcement (Pillar 2
    // sub-feature C, pulled forward to give the user feedback that A/D /
    // Q/E are turning the character vs. only the camera). Speaks "north" /
    // "north-east" etc. on sector change with 5° hysteresis.
    acc::turn_announce::Tick();

    // Phase 2 lay-off 7 — Pillar 2 area + room transition announcements.
    // Per-tick area-pointer + room-index delta detection; speaks "Bereich:
    // {name}" on area change and "Raum: {name}" on room change. First
    // observation after player-load also announces (gives orientation cue
    // on game-load). Self-gates on player+area resolved.
    acc::transitions::Tick();

    // ----- ORDER LOAD-BEARING -----
    // camera_announce → spatial::change_detector → view_mode.
    //   * camera_announce dead-reckons camera yaw from A/D held state;
    //     anchors the estimate on character-yaw changes.
    //   * change_detector reads camera yaw via
    //     camera_announce::TryGetCameraEngineYawDegrees for direction-
    //     aware filtering, and rebuilds the per-area wall cache.
    //   * view_mode reads BOTH the camera yaw and the wall cache (cursor
    //     collision tests).
    // Do not reorder these three without revisiting their consumers.
    acc::camera_announce::Tick();
    acc::spatial::change_detector::Tick();
    acc::view_mode::Tick();

    // Phase 3 lay-off 5 — Pillar 1 stuck-detection. Per-tick
    // displacement check seeds g_was_stuck for the OnPlayFootstep handler;
    // when the character has movement intent (engine drives the walk
    // anim → PlayFootstep fires) but didn't actually move, audio + visual
    // footprint + rumble are suppressed for that step.
    acc::audio::footstep_suppress::Tick();

    // Phase 2 diagnostic — Q/E/Tab logging. Engine has its own
    // target-cycle on Q/E (CClientExoAppInternal::SelectNearestObject
    // @0x005fb050) per investigation Q6 + verified web sources. Logs
    // keypresses with current LastTarget so we can correlate against
    // passive_narrate's `LastTarget changed` lines and decide whether to
    // delegate our `,`/`.` cycle to the engine's primitive or keep our
    // own filter. Removable in one commit once decided.
    acc::diag::engine_select::Tick();

    // Radial action menu — verify the engine still has at least one
    // populated row; auto-disarm when it's been cleared (action dispatched,
    // target lost, etc.). Cheap (chain walk + 3 reads); idle when our gate
    // is already disarmed.
    acc::radial_menu::Tick();

    // Combat system, Phase 1A/1B — combat-mode entry/exit announce + live
    // combat-log narration. Both are poll-based; cheap (one chain walk
    // each) and silent when no combat is active / no Messages panel
    // is mounted.
    acc::combat::TickCombatMode();
    acc::combat::TickCombatLog();

    // Combat system, Phase 4A — per-attack callout. Walks the player
    // creature's combat_round.attacks_list[7] each tick; announces on
    // attack_result transition from pending to resolved.
    acc::combat::TickAttackResolutions();

    // Combat system, Phase 4B — saving-throw callout. Skeleton no-op
    // until the SavingThrowRoll hook lands; see combat.cpp.
    acc::combat::TickSavingThrows();

    // Combat system, Phase 2A — auto-announce on party-leader change.
    // Also services the user-triggered Shift+H Examine panel monitor.
    acc::combat::query::TickLeaderChangeAutoAnnounce();
    acc::combat::query::TickExaminePanel();

    // Combat system, Phase 3A — action-queue submenu auto-disarm probe.
    acc::combat::queue::Tick();

    // Combat system, Phase 1D — live dialog screen narration. Polls
    // active CSWGuiDialog* panels for new NPC lines / reply count
    // changes and the BarkBubble for new bark text.
    acc::dialog_speech::Tick();

    // Phase 2 lay-off 9b — combined autowalk+interact hotkey (Enter).
    // Resolves cycle focus first / engine LastTarget fallback, speaks
    // localised pre-roll ("Sprich mit X" / "Öffne X" / "Hebe X auf"),
    // then routes through the engine's native click pipeline:
    // SetLastClickedOnTarget(handle) + HandleMouseClickInWorld. Engine
    // walks the player + dispatches kind-appropriate interaction.
    // Side-channel test of the parked autowalk blocker — if this path
    // moves the player when raw AddMoveToPointAction doesn't, the engine
    // click pipeline is the missing layer.
    acc::interact::PollHotkey();

    // Phase 2 lay-off 9-probe — in-world cursor-warp / passive-selection
    // monitor. Probe RESOLVED 2026-05-04 — see investigation Q6 §"RE —
    // does MoveMouseToPosition trigger world-hover?". Layer A viable
    // (LastTarget populates organically); Layer C dropped. Probe stays in
    // tree until 9a's narration loop is verified working in production
    // against the same handle stream the probe logged; deletable in a
    // single commit thereafter.
    acc::probe::world_hover::TickMonitor();
    acc::probe::world_hover::PollHotkey();

    // input_class re-assert. Edge-triggered on menu→in-world transition.
    // Compensates for the MessageBoxModal close path not calling
    // SetInputClass(client, 0, 1) the way CSWGuiInGameOptions does on
    // case 0x28 — without this, Alt+F4 quit-confirm Esc-dismiss leaves
    // input_class stuck at 2 and walking gated off. Idempotent for menus
    // that already reset on close.
    acc::engine::TickInputClassReassert();
    acc::engine::PollPauseToggleHotkey();

    // Pending-op drain runs LAST: every queued action was queued by an
    // input handler this tick or the previous one, and is dispatched only
    // after all monitors have run so no monitor sees a partially-applied
    // state.
    acc::menus::TickPendingOps();
}

}  // namespace acc::tick

// CSWGuiManager::Update — hooked mid-function at 0x40ce76. Per-frame tick run
// once after input dispatch by CClientExoAppInternal::MainLoop. Used as a safe
// callback site for the deferred MoveMouseToPosition triggered by chain
// navigation: the engine's input pipeline is NOT mid-flight here, so cursor
// updates can recurse through HandleMouseMove without re-entrancy.
//
// The cut byte is `mov eax, [ebp+0x8c]` (a panel-list field load); EBP is the
// manager pointer (the engine's `mov ebp, ecx` at 0x40ce74 happens before our
// hook). We pass EBP as the parameter for clarity even though we also have
// the global at 0x7A39F4 — both resolve to the same singleton.
extern "C" void __cdecl OnUpdate(void* /*gmFromEbp*/) {
    acc::tick::Dispatch();
}
