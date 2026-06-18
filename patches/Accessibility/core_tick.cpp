// Per-tick dispatcher — see core_tick.h.
#include <windows.h>

#include "core_tick.h"

#include "announce_degrees.h"
#include "audio_footstep_suppress.h"
#include "bringup_announce.h"
#include "camera_announce.h"
#include "camera_orient.h"
#include "camera_spin_diag.h"
#include "combat.h"
#include "combat_diag.h"
#include "combat_query.h"
#include "combat_queue.h"
#include "combat_special_watch.h"
#include "cycle_input.h"
#include "diag_focus.h"
#include "dialog_speech.h"
#include "discovery.h"
#include "engine_player.h"
#include "engine_subscreen.h"
#include "examine_view.h"
#include "guidance_approach.h"
#include "guidance_autowalk.h"
#include "guidance_beacon.h"
#include "help.h"
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
#include "engine_input.h"
#include "spatial_change_detector.h"
#include "swoop_race.h"
#include "turret_game.h"
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

// ---------------------------------------------------------------------------
// Cold-start DirectInput reacquire retry
// ---------------------------------------------------------------------------
// On a fresh launch the engine can reach the main menu with its DirectInput
// keyboard unacquired, so menu input is dead until the user alt-tabs (which
// forces a SetActive(0)->(1) edge). menus.cpp fires one ForceReacquireInput at
// MainMenu first-sight, but that single shot is not enough on every machine:
// in tester kenny's en-US 0.5.1 log the edge fired at first-sight yet the
// keyboard stayed dead for ~29 s — the mouse worked the whole time (cursor
// events flowed) but no keypress reached a panel until he physically clicked
// into the window. The likeliest cause is the engine recreating its Render
// Window shortly after first-sight and silently dropping the acquire we just
// drove; with no retry, nothing re-establishes it.
//
// This re-drives the edge every tick until input is provably live, then never
// runs again for the session. Guards:
//   * Stops permanently once acc::bringup_announce::IsInputPumpLive() — the
//     pump has delivered an event to a panel. Immune to the mislabeled
//     cursor-position channel that floods the input hook (it doesn't drive
//     SetActiveControl, so it never trips the latch).
//   * Only while the game owns the foreground — never acquire under another
//     app, so input keeps mirroring foreground (no nav-key bleed into the
//     background game while a screen reader / other window is in front).
//   * Throttled to kReacquireRetryMs between edges, so a key that is mid-
//     delivery isn't shredded by a (0) phase. The pump-live latch fires within
//     a frame or two of a successful acquire (well under the throttle), so we
//     stop before ever cycling over a now-working keyboard.
//   * Bounded to kReacquireMaxAttempts so a genuinely stuck keyboard (e.g.
//     another process holding it exclusive) doesn't cycle forever — we log a
//     give-up line and fall silent.
constexpr ULONGLONG kReacquireRetryMs    = 200;
constexpr int       kReacquireMaxAttempts = 50;  // ~10 s at 200 ms cadence

void RetryColdStartReacquire() {
    static bool      s_done     = false;
    static int       s_attempts = 0;
    static ULONGLONG s_lastMs   = 0;

    if (s_done) return;

    if (acc::bringup_announce::IsInputPumpLive()) {
        if (s_attempts > 0) {
            acclog::Write("EngineInput",
                "cold-start reacquire: input pump live after %d retr%s — done",
                s_attempts, s_attempts == 1 ? "y" : "ies");
        }
        s_done = true;
        return;
    }

    if (!acc::diag::focus::GameOwnsForeground()) return;

    ULONGLONG nowMs = GetTickCount64();
    if (s_lastMs != 0 && nowMs - s_lastMs < kReacquireRetryMs) return;
    s_lastMs = nowMs;

    if (s_attempts >= kReacquireMaxAttempts) {
        acclog::Write("EngineInput",
            "cold-start reacquire: gave up after %d attempts — keyboard still "
            "not live while foreground (another app may hold it exclusive)",
            s_attempts);
        s_done = true;
        return;
    }

    ++s_attempts;
    acc::engine::ForceReacquireInput();
}

}  // namespace

void Dispatch() {
    LARGE_INTEGER tickStart = WatchdogBeginTick();

    // Snapshot hotkey state for the whole tick — EndTick at the bottom
    // shifts now→last for next tick's rising-edge math.
    acc::hotkeys::BeginTick();

    // If a focus regain / render-window recreation flagged a DirectInput
    // reacquire (windowed-mode external focus theft kills engine input
    // otherwise — see engine_input.h RequestInputReacquire), do it now on a
    // clean tick, before any handler samples keyboard state.
    acc::engine::DrainPendingReacquire();

    // Cold-start safety net: the one-shot reacquire at MainMenu first-sight
    // doesn't always take (the engine can drop it on a Render Window
    // recreation moments later), leaving the keyboard dead for tens of
    // seconds. Re-drive the edge until the pump is provably live, then stop.
    RetryColdStartReacquire();

    // Speak the "Steam Big Picture is eating your keypresses" warning if the
    // focus-probe poll thread queued one (windowed-mode focus theft — the
    // user's keys never reach the engine, so menus look dead). Throttled at
    // the poll thread; this just drains + speaks on a safe main-thread tick.
    acc::diag::focus::DrainInputBlockedWarning();

    // Defensive — drop stale panel pointers before any handler touches them.
    PHASE("menus.ValidatePanels", acc::menus::ValidatePanels());

    // Pazaak minigame board — runs ahead of TickMonitors and the in-world /
    // menu pollers so it can Consume() the shared keys (Tab / Enter / arrows /
    // Esc) on its own tick before those pollers sample them.
    PHASE("pazaak", acc::pazaak::Tick());

    // Help system — F1 toggles the global keybind list, Ctrl+F1 reads the
    // current screen's keys. Runs ahead of the menu/cycle/interact pollers so
    // that while the list is open it claims the nav edges (Up/Down/Home/End/
    // Enter/Esc) before those consumers sample them. After pazaak so the
    // minigame keeps its own keys.
    PHASE("help", acc::help::PollWin32());

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

    // W/S/A/D/C/Y panic-cancel + beacon driver.
    PHASE("guidance.cancel", acc::guidance::PollMovementKeysCancel());
    PHASE("guidance.beacon", acc::guidance::beacon::Tick());

    // Restore player input once the handed-off engine action drains from
    // the queue (or a ceiling backstop fires).
    PHASE("engine.inputRestore", acc::engine::TickPlayerInputRestore());

    // Unified walk-to-act approach tracker — both the Shift+- autowalk and the
    // Enter-interact (loot/talk/door) dispatches arm it; it disarms quietly on
    // success and announces "way blocked" on a walkmesh-blocked stall.
    PHASE("guidance.approach", acc::guidance::TickApproach());

    // Diagnostic: log player action-queue depth changes (delta only).
    PHASE("engine.actionQueueDiag", acc::engine::TickActionQueueDiag());

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
    PHASE("camera_spin_diag", acc::camera_spin_diag::Tick());
    PHASE("spatial.change_detector", acc::spatial::change_detector::Tick());

    // Swoop race entry/exit cues. Gated to CSWMiniGame.type==0.
    PHASE("swoop_race", acc::swoop_race::Tick());

    // Turret / space-combat gunner minigame — shares CSWMiniGame with the
    // swoop race but reports type==3. Entry/exit announce + reticle diag.
    PHASE("turret_game", acc::turret_game::Tick());

    // Area + room transition announces.
    PHASE("transitions", acc::transitions::Tick());

    // Discovery-tier deferred load (runs after transitions has set the area).
    PHASE("discovery", acc::discovery::Tick());

    PHASE("view_mode", acc::view_mode::Tick());

    // Map UI cursor — gates on PanelKind::InGameMap.
    PHASE("map_ui_cursor", acc::map_ui_cursor::Tick());

    // Stuck-detection — feeds g_was_stuck for OnPlayFootstep.
    PHASE("footstep_suppress", acc::audio::footstep_suppress::Tick());

    // Combat — mode entry/exit, log narration, attack resolution, saves,
    // leader-change announce, examine panel monitor, queue submenu,
    // examine view, specials heartbeat.
    PHASE("combat.mode", acc::combat::TickCombatMode());
    PHASE("combat.log", acc::combat::TickCombatLog());
    PHASE("combat.absorb", acc::combat::TickCombatAbsorb());
    PHASE("combat.effects", acc::combat::TickCombatEffects());
    PHASE("combat.leaderChange", acc::combat::query::TickLeaderChangeAutoAnnounce());
    PHASE("combat.queue", acc::combat::queue::Tick());
    PHASE("combat_diag", acc::combat_diag::Tick());
    PHASE("examine_view", acc::examine_view::Tick());
    PHASE("help.tick", acc::help::Tick());
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
