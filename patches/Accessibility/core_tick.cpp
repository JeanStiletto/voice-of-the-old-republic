// Per-tick dispatcher — see core_tick.h.
#include <windows.h>

#include "core_tick.h"

#include "announce_degrees.h"
#include "audio_footstep_suppress.h"
#include "camera_announce.h"
#include "camera_orient.h"
#include "combat.h"
#include "combat_diag.h"
#include "combat_query.h"
#include "combat_queue.h"
#include "combat_special_watch.h"
#include "cycle_input.h"
#include "dialog_speech.h"
#include "engine_player.h"
#include "engine_subscreen.h"
#include "examine_view.h"
#include "guidance_autowalk.h"
#include "guidance_beacon.h"
#include "hotkeys.h"
#include "interact_hotkey.h"
#include "map_ui_cursor.h"
#include "map_user_markers.h"
#include "menus.h"
#include "menus_modsettings.h"
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
#include "swoop_race.h"
#include "transitions.h"
#include "update_checker.h"
#include "view_mode.h"

namespace acc::tick {

void Dispatch() {
    // Snapshot hotkey state for the whole tick — EndTick at the bottom
    // shifts now→last for next tick's rising-edge math.
    acc::hotkeys::BeginTick();

    // Defensive — drop stale panel pointers before any handler touches them.
    acc::menus::ValidatePanels();

    // Menu monitors (focus/contents/listbox change detection, give-mode poll).
    acc::menus::TickMonitors();

    // Home/End in menus — engine drops the scancodes; we synth-dispatch.
    acc::menus::PollHomeEndKeys();

    // Cycle keys (`,`/`.`/`-`) and AltGr — engine drops unbound scancodes.
    acc::cycle_input::PollWin32();
    acc::announce_degrees::PollWin32();

    // Shift+N drops a map marker; in-world Shift+N stays silent.
    acc::map_user_markers::PollWin32();

    // Diagnostic probes (Shift+AltGr Mouse Look, F9 pathfind, F10 audio
    // frame, F12 camera state, Ctrl+F12 camera distance, B view mode).
    acc::probe_mouselook::PollWin32();
    acc::probe_mouselook::TickSweep();
    acc::probe_pathfind::PollWin32();
    acc::probe_pathfind::Tick();
    acc::probe_audio_frame::PollWin32();
    acc::probe_camera_state::PollWin32();
    acc::probe_camera_distance::Tick();
    acc::view_mode::PollWin32();

    // Autowalk watchdog + W/S/A/D/C/Y panic-cancel + beacon driver.
    acc::guidance::TickProgressWatchdog();
    acc::guidance::PollMovementKeysCancel();
    acc::guidance::beacon::Tick();

    // Auto-restore player input ~3s after a guidance/interact disable.
    acc::engine::TickPlayerInputRestore();

    // Drain deferred Q/E reannounce.
    acc::passive_narrate::Tick();

    // Tab speaks the new leader's name.
    acc::party_leader_announce::Tick();

    // ----- ORDER LOAD-BEARING -----
    // camera_announce → camera_orient → spatial::change_detector →
    // transitions → view_mode.
    //   camera_announce derives camera yaw from positions.
    //   camera_orient reads it for closed-loop arrival (same frame).
    //   spatial::change_detector reads camera yaw + rebuilds wall cache.
    //   transitions builds wall_topology that depends on the wall cache —
    //     must run AFTER change_detector or the first tick of an area
    //     change uses stale walls from the previous area.
    //   view_mode reads camera yaw + walls + region/landmark caches.
    acc::camera_announce::Tick();
    acc::camera_orient::Tick();
    acc::spatial::change_detector::Tick();

    // Swoop race entry/exit cues.
    acc::swoop_race::Tick();

    // Area + room transition announces.
    acc::transitions::Tick();

    acc::view_mode::Tick();

    // Map UI cursor — gates on PanelKind::InGameMap.
    acc::map_ui_cursor::Tick();

    // Stuck-detection — feeds g_was_stuck for OnPlayFootstep.
    acc::audio::footstep_suppress::Tick();

    // Radial menu auto-disarm.
    acc::radial_menu::Tick();

    // Combat — mode entry/exit, log narration, attack resolution, saves,
    // leader-change announce, examine panel monitor, queue submenu,
    // examine view, specials heartbeat.
    acc::combat::TickCombatMode();
    acc::combat::TickCombatLog();
    acc::combat::query::TickLeaderChangeAutoAnnounce();
    acc::combat::queue::Tick();
    acc::combat_diag::Tick();
    acc::examine_view::Tick();
    acc::combat::special_watch::Tick();

    // One-shot priority-group dump.
    acc::probe::priority_groups::Tick();

    // Dialog screen + bark bubble narration.
    acc::dialog_speech::Tick();

    // Enter (interact) — engine click pipeline with localised pre-roll.
    acc::interact::PollHotkey();

    // MessageBoxModal close cleanup — engine's close path doesn't unpause
    // or unmute on its own.
    acc::engine::TickInputClassReassert();

    // Audio-glossary delayed-playback timer.
    acc::menus::modsettings::Tick();

    // In-game auto-updater: F5 poll + background-check announce + handoff
    // batch spawn on download completion. Cheap when idle (one atomic load).
    acc::update_checker::Tick();

    // Drain queued actions LAST — monitors above must see consistent state.
    acc::menus::TickPendingOps();

    acc::hotkeys::EndTick();
}

}  // namespace acc::tick

// CSWGuiManager::Update detour @0x40ce76. Per-frame, post-input. Safe
// callback site for deferred cursor moves — input pipeline isn't mid-flight.
extern "C" void __cdecl OnUpdate(void* /*gmFromEbp*/) {
    acc::tick::Dispatch();
}
