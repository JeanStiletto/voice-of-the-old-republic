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
#include "combat_special_watch.h"
#include "cycle_input.h"
#include "dialog_speech.h"
#include "engine_player.h"
#include "engine_subscreen.h"
#include "guidance_autowalk.h"
#include "guidance_beacon.h"
#include "hotkeys.h"
#include "interact_hotkey.h"
#include "map_ui_cursor.h"
#include "map_user_markers.h"
#include "menus.h"
#include "party_leader_announce.h"
#include "passive_narrate.h"
#include "probe_audio_frame.h"
#include "probe_priority_groups.h"
#include "probe_camera_distance.h"
#include "probe_camera_state.h"
#include "probe_mouselook.h"
#include "probe_pathfind.h"
#include "radial_menu.h"
#include "spatial_change_detector.h"
#include "transitions.h"
#include "turn_announce.h"
#include "view_mode.h"

namespace acc::tick {

void Dispatch() {
    // Snapshot every registered hotkey's down-state once per tick so all
    // pollers see a consistent view of "did the edge fire this frame".
    // EndTick at the bottom shifts `now` into `last`.
    acc::hotkeys::BeginTick();

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

    // Phase 6 lay-off 3 — Shift+Q in map context drops a saved marker
    // at the cursor's current world position. Self-gates on
    // HasActiveMapPanel + cursor active; in-world Shift+Q stays silent.
    acc::map_user_markers::PollWin32();

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

    // Phase 5 lay-off 1 — path-data RE probe. F9 dispatches WalkTo to a
    // point 10m ahead and dumps CSWSCreature+0x340 region plus the area's
    // path_points / path_connections triples across a tick cascade
    // (pre / +100ms / +500ms / +1500ms / +3500ms) so we can locate the
    // computed-path waypoint list. Diagnostic-only; goes away once the
    // result locks the design fork (read engine solution vs re-solve A*).
    acc::probe_pathfind::PollWin32();
    acc::probe_pathfind::Tick();

    // Audio-frame diagnostic — F10 emits a 3D cue 5m from the player in
    // the next compass sector (N → NE → … → NW → repeat) along with a
    // spoken direction name, so the user can verify whether the
    // engine's audio pan matches our compass convention. Used to
    // characterise the listener-frame mismatch the user reported at
    // Phase 5 lay-off 4 in-game testing.
    acc::probe_audio_frame::PollWin32();

    // Camera-state probe — F12 dumps the engine's cached camera yaw
    // (CSWCModule + 0x98, written by AcclTurnCamera) so we can compare
    // it to our dead-reckoned compass estimate and the player's
    // character yaw. Used to calibrate units and frame for a
    // production camera_yaw reader (replaces dead-reckoning).
    acc::probe_camera_state::PollWin32();

    // Option-B camera-distance probe — Ctrl+F12 snapshots the active
    // camera behavior's target-distance field; Ctrl+F11 cycles per-tick
    // clamp modes (off/0.0/0.5/2.0 m) and logs whether the engine's
    // per-frame recompute stomps the clamp. Feasibility check for
    // "collapse camera distance to 0 → listener ends up at character"
    // — see probe_camera_distance.h for the engine surfaces.
    acc::probe_camera_distance::Tick();

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

    // Phase 5 Pillar 3 Mode B — audio beacon driver. Reads player
    // position once per tick, emits BeaconActive heartbeat / Reached /
    // DestinationReached cues as the user walks the path Ctrl+- armed.
    // Self-gates on `IsActive()`; idle cost is one bool check.
    acc::guidance::beacon::Tick();

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

    // Tab leader announce — Win32-polled Tab rising edge speaks the
    // controlled creature's name (after the engine has cycled to it).
    // Gates: foreground-window, player-loaded, IsForegroundUiBlocking
    // (skip while drilled in a panel — manager-side Tab handles panel
    // cycling there). Repetition intentional (solo-mode confirmation).
    acc::party_leader_announce::Tick();

    // Phase 2 ad-hoc — octagonal direction-on-turn announcement (Pillar 2
    // sub-feature C, pulled forward to give the user feedback that A/D /
    // Q/E are turning the character vs. only the camera). Speaks "north" /
    // "north-east" etc. on sector change with 5° hysteresis.
    acc::turn_announce::Tick();

    // ----- ORDER LOAD-BEARING -----
    // camera_announce → spatial::change_detector → transitions → view_mode.
    //   * camera_announce dead-reckons camera yaw from A/D held state;
    //     anchors the estimate on character-yaw changes.
    //   * change_detector reads camera yaw via
    //     camera_announce::TryGetCameraEngineYawDegrees for direction-
    //     aware filtering, and rebuilds the per-area wall cache.
    //   * transitions builds the region_classifier + wall_topology
    //     caches that depend on the wall cache. Must run AFTER
    //     change_detector or it reads stale walls from the previous
    //     area on the first tick of an area change — confirmed by
    //     patch-20260513-054417 (Oberstadt got built with Apartments
    //     walls, edge counts matched exactly).
    //   * view_mode reads BOTH the camera yaw and the wall cache (cursor
    //     collision tests) and the region/landmark caches (cursor
    //     announce). Must run AFTER transitions for the latter.
    // Do not reorder these four without revisiting their consumers.
    acc::camera_announce::Tick();
    acc::spatial::change_detector::Tick();

    // Phase 2 lay-off 7 — Pillar 2 area + room transition announcements.
    // Per-tick area-pointer + room-index delta detection; speaks "Bereich:
    // {name}" on area change and "Raum: {name}" on room change. First
    // observation after player-load also announces (gives orientation cue
    // on game-load). Self-gates on player+area resolved.
    acc::transitions::Tick();

    acc::view_mode::Tick();

    // Phase 5 lay-off 6 — virtual cursor on the in-game area-map UI.
    // Self-gates on PanelKind::InGameMap being the foreground panel.
    // Movement keys W/A/S/D translate the cursor in map-pixel space;
    // hover-pause speaks the nearest explored map note. Runs AFTER the
    // menu monitors have built the foreground-panel snapshot.
    acc::map_ui_cursor::Tick();

    // Phase 3 lay-off 5 — Pillar 1 stuck-detection. Per-tick
    // displacement check seeds g_was_stuck for the OnPlayFootstep handler;
    // when the character has movement intent (engine drives the walk
    // anim → PlayFootstep fires) but didn't actually move, audio + visual
    // footprint + rumble are suppressed for that step.
    acc::audio::footstep_suppress::Tick();

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

    // Combat system, Phase 3B — specials-empty heartbeat. Peripheral
    // "you can act now" cue when the party-wide queue holds only
    // routine auto-attacks. Edge-triggered immediate + 6s repeat,
    // first-round gated so "Kampf beginnt" gets clean air.
    acc::combat::special_watch::Tick();

    // One-shot probe: dump CExoSoundInternal.priority_groups[N] so we
    // can identify the loudest bus. Self-disarms after one dump.
    acc::probe::priority_groups::Tick();

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

    // MessageBoxModal-close cleanup. Edge-triggered on modal_stack
    // non-zero → 0. The engine's MessageBoxModal close path (Alt+F4
    // quit-confirm, save-overwrite, dialog-skip, …) never calls
    // CServerExoApp::SetPauseState(2, 0) or CExoSoundInternal::SetSoundMode(0),
    // so the world stays paused and audio stays muted after popup
    // dismiss. We dispatch both directly. Idempotent — calling on
    // already-clean state is a no-op. See engine_subscreen.h for the
    // full iteration history and Esc-menu vs Alt+F4 close-path
    // comparison.
    acc::engine::TickInputClassReassert();

    // Pending-op drain runs LAST: every queued action was queued by an
    // input handler this tick or the previous one, and is dispatched only
    // after all monitors have run so no monitor sees a partially-applied
    // state.
    acc::menus::TickPendingOps();

    // Hotkey edge-state commit. Every Pressed() query above is now done;
    // shift `now` into `last` so next tick's rising-edge math sees the
    // correct prior state.
    acc::hotkeys::EndTick();
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
