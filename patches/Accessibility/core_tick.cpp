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
#include "log.h"
#include "map_ui_cursor.h"
#include "map_user_markers.h"
#include "menus.h"
#include "menus_modsettings.h"
#include "menus_pazaakdeck.h"
#include "party_leader_announce.h"
#include "passive_narrate.h"
#include "pazaak.h"
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

namespace {

// ---------------------------------------------------------------------------
// Tick watchdog
// ---------------------------------------------------------------------------
// Intermittent multi-second freezes (sound cuts out, then resumes) mean the
// game's main thread blocked — and a blocked thread leaves only a silent gap
// in the log. This watchdog turns that gap into one labelled line, then stays
// silent. It measures two independent things and logs ONLY past a threshold,
// so steady-state output is zero (a normal frame is well under 16 ms):
//
//   * Per-phase Dispatch time. Each subsystem call is wrapped in PHASE();
//     if the whole Dispatch overruns kSlowDispatchMs we log the total plus
//     the single slowest phase — naming the culprit when the stall is in
//     OUR code.
//   * Inter-tick wall gap. Time from the end of the previous Dispatch to the
//     start of this one. A large gap with a SMALL Dispatch total means the
//     stall was OUTSIDE our Dispatch (engine code, or one of our hooks firing
//     between frames) — the watchdog still catches it and says so.
//
// A genuine freeze is a single blocked tick, so it yields a single line, not
// a flood. Area loads can also trip the gap check — that is expected and
// informative, not spam (loads are rare and the line says "gap").

constexpr double kSlowDispatchMs = 200.0;   // our per-tick work over this → log
constexpr double kSlowGapMs      = 750.0;   // wall gap between ticks over this → log

LARGE_INTEGER g_qpcFreq    = {};
LARGE_INTEGER g_lastEnd    = {};
bool          g_haveLast   = false;

// Per-tick worst-phase accumulator (reset each Dispatch).
const char* g_worstName = nullptr;
double      g_worstMs   = 0.0;
double      g_totalMs   = 0.0;

inline LARGE_INTEGER QpcNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t;
}

inline double QpcMs(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
    if (g_qpcFreq.QuadPart == 0) return 0.0;
    return static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 /
           static_cast<double>(g_qpcFreq.QuadPart);
}

inline void PhaseMark(const char* name, double ms) {
    g_totalMs += ms;
    if (ms > g_worstMs) { g_worstMs = ms; g_worstName = name; }
}

// Time one Dispatch phase and fold it into the per-tick accumulators. The
// call runs exactly once, in place; only the surrounding timing is added.
#define PHASE(name, call)                          \
    do {                                           \
        LARGE_INTEGER _a = QpcNow();               \
        call;                                      \
        PhaseMark(name, QpcMs(_a, QpcNow()));      \
    } while (0)

// Reset accumulators + check the inter-tick gap. Returns the tick-start stamp.
LARGE_INTEGER WatchdogBeginTick() {
    if (g_qpcFreq.QuadPart == 0) QueryPerformanceFrequency(&g_qpcFreq);
    g_worstName = nullptr;
    g_worstMs   = 0.0;
    g_totalMs   = 0.0;

    LARGE_INTEGER now = QpcNow();
    if (g_haveLast) {
        double gap = QpcMs(g_lastEnd, now);
        if (gap >= kSlowGapMs) {
            acclog::Write("Watchdog",
                          "SLOW FRAME gap=%.0fms since previous tick "
                          "(stall outside our Dispatch — engine load or a "
                          "between-frame hook; an area load can also show here)",
                          gap);
        }
    }
    return now;
}

// Log the total if our Dispatch overran, naming the slowest phase.
void WatchdogEndTick(const LARGE_INTEGER& tickStart) {
    LARGE_INTEGER now = QpcNow();
    double total = QpcMs(tickStart, now);
    if (total >= kSlowDispatchMs) {
        acclog::Write("Watchdog",
                      "SLOW TICK total=%.0fms — slowest phase '%s' %.0fms",
                      total, g_worstName ? g_worstName : "?", g_worstMs);
    }
    g_lastEnd  = now;
    g_haveLast = true;
}

}  // namespace

void Dispatch() {
    LARGE_INTEGER tickStart = WatchdogBeginTick();

    // Snapshot hotkey state for the whole tick — EndTick at the bottom
    // shifts now→last for next tick's rising-edge math.
    acc::hotkeys::BeginTick();

    // Defensive — drop stale panel pointers before any handler touches them.
    PHASE("menus.ValidatePanels", acc::menus::ValidatePanels());

    // Pazaak minigame board — runs ahead of TickMonitors and the in-world /
    // menu pollers so it can Consume() the shared keys (Tab / Enter / arrows /
    // Esc) on its own tick before those pollers sample them.
    PHASE("pazaak", acc::pazaak::Tick());

    // Menu monitors (focus/contents/listbox change detection, give-mode poll).
    PHASE("menus.TickMonitors", acc::menus::TickMonitors());

    // Home/End in menus — engine drops the scancodes; we synth-dispatch.
    PHASE("menus.PollHomeEnd", acc::menus::PollHomeEndKeys());

    // Cycle keys (`,`/`.`/`-`) and AltGr — engine drops unbound scancodes.
    PHASE("cycle_input", acc::cycle_input::PollWin32());
    PHASE("announce_degrees", acc::announce_degrees::PollWin32());

    // Shift+N drops a map marker; in-world Shift+N stays silent.
    PHASE("map_user_markers", acc::map_user_markers::PollWin32());

    // Diagnostic probes (Shift+AltGr Mouse Look, F9 pathfind, F10 audio
    // frame, F12 camera state, Ctrl+F12 camera distance, B view mode).
    PHASE("probe_mouselook.poll", acc::probe_mouselook::PollWin32());
    PHASE("probe_mouselook.sweep", acc::probe_mouselook::TickSweep());
    PHASE("probe_pathfind.poll", acc::probe_pathfind::PollWin32());
    PHASE("probe_pathfind.tick", acc::probe_pathfind::Tick());
    PHASE("probe_audio_frame", acc::probe_audio_frame::PollWin32());
    PHASE("probe_camera_state", acc::probe_camera_state::PollWin32());
    PHASE("probe_camera_distance", acc::probe_camera_distance::Tick());
    PHASE("view_mode.poll", acc::view_mode::PollWin32());

    // Autowalk watchdog + W/S/A/D/C/Y panic-cancel + beacon driver.
    PHASE("guidance.progress", acc::guidance::TickProgressWatchdog());
    PHASE("guidance.cancel", acc::guidance::PollMovementKeysCancel());
    PHASE("guidance.beacon", acc::guidance::beacon::Tick());

    // Auto-restore player input ~3s after a guidance/interact disable.
    PHASE("engine.inputRestore", acc::engine::TickPlayerInputRestore());

    // Drain deferred Q/E reannounce.
    PHASE("passive_narrate", acc::passive_narrate::Tick());

    // Tab speaks the new leader's name.
    PHASE("party_leader_announce", acc::party_leader_announce::Tick());

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
    PHASE("camera_announce", acc::camera_announce::Tick());
    PHASE("camera_orient", acc::camera_orient::Tick());
    PHASE("spatial.change_detector", acc::spatial::change_detector::Tick());

    // Swoop race entry/exit cues.
    PHASE("swoop_race", acc::swoop_race::Tick());

    // Area + room transition announces.
    PHASE("transitions", acc::transitions::Tick());

    PHASE("view_mode", acc::view_mode::Tick());

    // Map UI cursor — gates on PanelKind::InGameMap.
    PHASE("map_ui_cursor", acc::map_ui_cursor::Tick());

    // Stuck-detection — feeds g_was_stuck for OnPlayFootstep.
    PHASE("footstep_suppress", acc::audio::footstep_suppress::Tick());

    // Radial menu auto-disarm.
    PHASE("radial_menu", acc::radial_menu::Tick());

    // Combat — mode entry/exit, log narration, attack resolution, saves,
    // leader-change announce, examine panel monitor, queue submenu,
    // examine view, specials heartbeat.
    PHASE("combat.mode", acc::combat::TickCombatMode());
    PHASE("combat.log", acc::combat::TickCombatLog());
    PHASE("combat.leaderChange", acc::combat::query::TickLeaderChangeAutoAnnounce());
    PHASE("combat.queue", acc::combat::queue::Tick());
    PHASE("combat_diag", acc::combat_diag::Tick());
    PHASE("examine_view", acc::examine_view::Tick());
    PHASE("combat.special_watch", acc::combat::special_watch::Tick());

    // One-shot priority-group dump.
    PHASE("probe.priority_groups", acc::probe::priority_groups::Tick());

    // Dialog screen + bark bubble narration.
    PHASE("dialog_speech", acc::dialog_speech::Tick());

    // Enter (interact) — engine click pipeline with localised pre-roll.
    PHASE("interact", acc::interact::PollHotkey());

    // MessageBoxModal close cleanup — engine's close path doesn't unpause
    // or unmute on its own.
    PHASE("engine.inputReassert", acc::engine::TickInputClassReassert());

    // Audio-glossary delayed-playback timer.
    PHASE("modsettings", acc::menus::modsettings::Tick());

    // In-game auto-updater: F5 poll + background-check announce + handoff
    // batch spawn on download completion. Cheap when idle (one atomic load).
    PHASE("update_checker", acc::update_checker::Tick());

    // Drain the Pazaak deck-builder's staged add/remove/play before the generic
    // pending-op drain (a Play queues an Activate that drains in TickPendingOps).
    PHASE("pazaakdeck", acc::menus::pazaakdeck::Tick());

    // Drain queued actions LAST — monitors above must see consistent state.
    PHASE("menus.TickPendingOps", acc::menus::TickPendingOps());

    acc::hotkeys::EndTick();

    WatchdogEndTick(tickStart);
}

#undef PHASE

}  // namespace acc::tick

// CSWGuiManager::Update detour @0x40ce76. Per-frame, post-input. Safe
// callback site for deferred cursor moves — input pipeline isn't mid-flight.
extern "C" void __cdecl OnUpdate(void* /*gmFromEbp*/) {
    acc::tick::Dispatch();
}
