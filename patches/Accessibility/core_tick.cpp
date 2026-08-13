// Per-tick dispatcher — see core_tick.h.
#include <windows.h>

#include "core_tick.h"

#include "announce_degrees.h"
#include "audio_footstep_suppress.h"
#include "bringup_announce.h"
#include "camera_announce.h"
#include "camera_orient.h"
#include "camera_spin_guard.h"
#include "combat.h"
#include "combat_diag.h"
#include "combat_query.h"
#include "combat_queue.h"
#include "combat_special_watch.h"
#include "cycle_input.h"
#include "engine_keymap.h"  // FirstMovementKeyHeld — dead-keyboard watchdog probe
#include "focus_guard.h"
#include "dialog_speech.h"
#include "discovery.h"
#include "endar_softlock.h"
#include "door_announce.h"
#include "engine_area.h"
#include "engine_player.h"
#include "engine_subscreen.h"
#include "examine_view.h"
#include "floor_puzzle.h"
#include "guidance_approach.h"
#include "guidance_autowalk.h"
#include "guidance_beacon.h"
#include "help.h"
#include "hotkeys.h"
#include "interact_dispatch.h"
#include "input_poll_router.h"
#include "engine_levelup.h"  // TickLevelUpPause — release the wizard overlay pause
#include "locked_recall.h"
#include "log.h"
#include "map_ui_cursor.h"
#include "map_user_markers.h"
#include "menus.h"
#include "menus_modsettings.h"
#include "menus_pazaakdeck.h"
#include "pad_actionmenu.h"
#include "pad_input.h"
#include "pad_movement.h"
#include "pad_quickmenu.h"
#include "party_leader_announce.h"
#include "passive_narrate.h"
#include "minigame_pazaak.h"
#include "probe_audio_frame.h"
#include "probe_camera_distance.h"
#include "probe_camera_state.h"
#include "engine_input.h"
#include "spatial_change_detector.h"
#include "speech_clipboard.h"
#include "stealth_watch.h"
#include "weapon_set_watch.h"
#include "minigame_swoop_race.h"
#include "trap_watch.h"
#include "minigame_turret.h"
#include "transitions.h"
#include "update_checker.h"
#include "view_mode.h"
#include "engine_game.h"

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

// Timestamp of the tick currently being dispatched, in ms (see NowMs).
DWORD g_tickNowMs = 0;

inline LARGE_INTEGER QpcNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t;
}

// QPC → ms, wrapping at 2^32 exactly like GetTickCount so every existing
// `now - then >= window` comparison stays valid unsigned arithmetic. Split
// into whole seconds + remainder rather than multiplying the raw counter by
// 1000, which would overflow int64 after ~29 years of uptime at a 10 MHz
// counter.
inline DWORD QpcToMs(const LARGE_INTEGER& t) {
    if (g_qpcFreq.QuadPart == 0) return GetTickCount();
    LONGLONG secs = t.QuadPart / g_qpcFreq.QuadPart;
    LONGLONG rem  = t.QuadPart % g_qpcFreq.QuadPart;
    return static_cast<DWORD>(secs * 1000 + (rem * 1000) / g_qpcFreq.QuadPart);
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
    // Publish this tick's timestamp before anything in the fan-out runs, so
    // every subsystem that asks sees the same "now" for the whole tick.
    g_tickNowMs = QpcToMs(now);
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

    if (!acc::focus_guard::GameOwnsForeground()) return;

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

// ---------------------------------------------------------------------------
// Dead-keyboard watchdog (mid-session)
// ---------------------------------------------------------------------------
// RetryColdStartReacquire above latches permanently once the pump goes live, so
// it only ever protects the launch window. The same failure recurs mid-session:
// an alt-tab out and back makes the engine recreate its Render Window, the
// focus-gain path fires ONE SetActive(0)->(1) cycle, and when that single shot
// doesn't take the engine's keyboard stays dead for the rest of the session.
//
// Witnessed in patch-20260809-090145.log: window recreated at 09:03:23, the
// focus-gain reacquire fired at 09:03:26, and then ZERO key presses reached the
// engine (no Diag.ClientHIE val=128, no Menus.Input) until a second alt-tab at
// 09:03:59. The engine's quit-confirm popup was up, correctly announced, with
// OK and Abbrechen focusable — and unreachable, because no keystroke could get
// to it. Cold start needed 13 retries that same session; one shot was never
// going to be enough.
//
// Detection is the asymmetry itself: mod hotkeys are polled through
// GetAsyncKeyState and keep working when the engine's DirectInput is dead. So
// "an ENGINE-BOUND key is held according to Win32, while the engine has not
// seen a key press for a good while" means the keyboard died, and nothing else
// produces that combination. Keys the engine does not bind are excluded — the
// mod's own cycle keys (`,` `.` `-`) legitimately never reach the engine, and
// treating those as evidence would fire the recovery constantly.
constexpr ULONGLONG kDeadKeyboardMs   = 1500;  // silence that counts as dead
constexpr ULONGLONG kDeadRecoveryMs   = 700;   // between recovery cycles

// Engine-bound keys a player actually presses when nothing is responding. All
// are real engine binds (GUI confirm/cancel/nav, leader cycle, movement), so a
// healthy engine answers them within a frame and the watchdog never arms.
int HeldEngineBoundKey() {
    static const int kProbe[] = {
        VK_RETURN, VK_ESCAPE, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_TAB, VK_SPACE
    };
    for (int vk : kProbe) {
        if (GetAsyncKeyState(vk) & 0x8000) return vk;
    }
    return acc::engine_keymap::FirstMovementKeyHeld();
}

void WatchDeadKeyboard() {
    static ULONGLONG s_heldSince = 0;
    static ULONGLONG s_lastCycle = 0;
    static int       s_cycles    = 0;

    // Never acquire under another app — same rule as the cold-start retry.
    if (!acc::focus_guard::GameOwnsForeground()) {
        s_heldSince = 0;
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (HeldEngineBoundKey() == 0) {
        s_heldSince = 0;
        s_cycles    = 0;   // released: a fresh episode starts its count over
        return;
    }
    if (s_heldSince == 0) s_heldSince = now;

    // The engine is hearing keys — nothing wrong, whatever else is going on.
    const ULONGLONG lastKey = acc::engine::LastEngineKeyEventMs();
    if (lastKey != 0 && now - lastKey < kDeadKeyboardMs) return;

    // Give the press time to land before calling it dead.
    if (now - s_heldSince < kDeadKeyboardMs) return;
    if (s_lastCycle != 0 && now - s_lastCycle < kDeadRecoveryMs) return;
    s_lastCycle = now;

    ++s_cycles;
    acclog::Write("EngineInput",
        "dead-keyboard watchdog: vk=0x%02x held %llums, engine silent %llums "
        "— driving recovery cycle #%d",
        HeldEngineBoundKey(), now - s_heldSince,
        lastKey ? now - lastKey : 0ull, s_cycles);
    acc::engine::ForceReacquireInput();
}

}  // namespace

DWORD NowMs() {
    // Before the first tick (bring-up, or a hook that fires ahead of any
    // Update) there is nothing published yet — fall back to the coarse clock
    // rather than handing out 0, which would read as "a very long time ago".
    return g_tickNowMs != 0 ? g_tickNowMs : GetTickCount();
}

void Dispatch() {
    LARGE_INTEGER tickStart = WatchdogBeginTick();

    // KOTOR 2 (Batch 1, GUI spine) runs the MENU-SYSTEM phases only: panel
    // validation, help overlay, the menu monitors (whose TickGeneralMonitors
    // is the sole consumer of the pending-announce slot), Home/End synthesis,
    // mod settings, the updater, and the pending-op drain. Every other phase
    // reads engine state its batch has not ported (world, combat, camera,
    // minigames, dialog, in-game panels) and is gated to KOTOR 1 below —
    // handler_chain_audit.py counts 300+ unresolved-constant reads across
    // those subsystems. Later batches clear their own phases from this gate.
    const bool k1 = acc::game::IsKotor1();

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

    // Mid-session counterpart: the cold-start net latches off once the pump has
    // ever been live, and the engine's keyboard can die again on any later
    // focus regain. Fires only on the dead-keyboard fingerprint — see above.
    WatchDeadKeyboard();

    // The Endar Spire opening holds the global fade at alpha=1.0 (engine
    // re-asserts it every frame) after the fade finishes, while the player still
    // has world control. That gates MainLoop's DoPassiveSelection off, freezing
    // the Q/E candidate halo and passive narration (root cause of the second-
    // door "Q/E can't find the door in front of me"). Since the fade can't be
    // cleared, drive DoPassiveSelection ourselves while that's the only blocker.
    // No-op in normal play. See engine_area.cpp. Both games since the K2
    // systems pass (2026-08-02): the whole gate chain is witnessed in the K2
    // MainLoop twin — K2's scripted holds (Peragus opening) pin the fade the
    // same way the Endar Spire does.
    acc::engine::MaybeDrivePassiveSelection();

    // Speak the "Steam Big Picture is eating your keypresses" warning if the
    // focus-probe poll thread queued one (windowed-mode focus theft — the
    // user's keys never reach the engine, so menus look dead). Throttled at
    // the poll thread; this just drains + speaks on a safe main-thread tick.
    acc::focus_guard::DrainInputBlockedWarning();

    // Gamepad SAMPLE, both games. Runs before every input consumer: the
    // trigger state it reads is the modifier layer the pad's D-Pad bindings
    // consult, on KOTOR 2 the pad events themselves arrive asynchronously at
    // the GUI-manager hook, and on KOTOR 1 everything downstream (the button
    // dispatch, the movement driver) reads the snapshot it leaves behind. One
    // XInput call, throttled to a 2 s rescan while no pad answers.
    PHASE("pad_input", acc::pad::Tick());

    // Defensive — drop stale panel pointers before any handler touches them.
    PHASE("menus.ValidatePanels", acc::menus::ValidatePanels());

    // Pazaak minigame board — runs ahead of TickMonitors and the in-world /
    // menu pollers so it can Consume() the shared keys (Tab / Enter / arrows /
    // Esc) on its own tick before those pollers sample them.
    if (k1) PHASE("pazaak", acc::pazaak::Tick());

    // Help system — F1 toggles the global keybind list, Ctrl+F1 reads the
    // current screen's keys. Runs ahead of the menu/cycle/interact pollers so
    // that while the list is open it claims the nav edges (Up/Down/Home/End/
    // Enter/Esc) before those consumers sample them. After pazaak so the
    // minigame keeps its own keys.
    PHASE("help", acc::help::PollWin32());

    // Ctrl+R copies the last spoken text to the clipboard. Position in the
    // phase order doesn't matter — Ctrl+R is bound to nothing else, so there is no
    // shared edge to arbitrate — but it belongs with the other always-on mod
    // services rather than with the state-gated world/menu pollers.
    PHASE("speech_clipboard", acc::speech_clipboard::PollWin32());

    // Menu monitors (focus/contents/listbox change detection, give-mode poll).
    PHASE("menus.TickMonitors", acc::menus::TickMonitors());

    // Home/End in menus — engine drops the scancodes; we synth-dispatch.
    PHASE("menus.PollHomeEnd", acc::menus::PollHomeEndKeys());

    // Shift+N drops a map marker; in-world Shift+N stays silent. Both games
    // since the map batch: the whole pin-creation chain (operator new, pin
    // ctor, note assign, AddMapPin) and the CSWCMapPin layout are witnessed
    // against K2's own script pin-creator.
    PHASE("map_user_markers", acc::map_user_markers::PollWin32());

    if (k1) {
        // Camera diagnostic probes (F12 camera state, Ctrl+F12 camera
        // distance). Stay KOTOR 1: pure dev diagnostics on 6 unresolved
        // camera/behavior vtable offsets — port when a K2 camera
        // investigation actually needs them, not before.
        PHASE("probe_camera_state", acc::probe_camera_state::PollWin32());
        PHASE("probe_camera_distance", acc::probe_camera_distance::Tick());
    }

    // Audio-frame probe (F10) — engine-constant-free (player position +
    // sector math only). Both games since the K2 systems pass (2026-08-02).
    PHASE("probe_audio_frame", acc::probe_audio_frame::PollWin32());

    // Both games since the K2 systems pass (2026-08-02): view_mode.cpp's
    // whole closure is resolved — its one Todo (CClientOptions bitfield)
    // was witnessed off the K2 exe (chain and mouse-look identical; the
    // autopause field/bit MOVED and is Pick'd). Map-pin offsets are Same/
    // Pick'd since the map batch, so pins in the narrated-target slot read
    // correctly on both games.
    PHASE("view_mode.poll", acc::view_mode::PollWin32());

    // ----- Batch 3c (KOTOR 2 port): interaction -----
    // These run on BOTH games. The whole subsystem closure was resolved for
    // KOTOR 2 offline (92 constants, sole remaining Todo is the DESIGNED
    // kClientObjectServerObjectOffset whose consumers branch to the engine
    // resolver) — see docs/kotor2-port.md Batch 3c.

    // Cycle keys (`,`/`.`/`-`) and AltGr — engine drops unbound scancodes.
    PHASE("cycle_input", acc::cycle_input::PollWin32());
    PHASE("announce_degrees", acc::announce_degrees::PollWin32());

    // Beacon driver.
    PHASE("guidance.beacon", acc::guidance::beacon::Tick());

    // Restore player input once the handed-off engine action drains from
    // the queue (or a ceiling backstop fires).
    PHASE("engine.inputRestore", acc::engine::TickPlayerInputRestore());

    // Unified walk-to-act approach tracker — both the Shift+- autowalk and
    // the Enter-interact (loot/talk/door) dispatches arm it; it disarms
    // quietly on success and announces "way blocked" on a walkmesh-blocked
    // stall.
    PHASE("guidance.approach", acc::guidance::TickApproach());

    // W/S/A/D/C/Y movement-cancel. Runs AFTER TickApproach so that if a
    // dialog/loot/cutscene result surfaced this tick, the tracker has
    // already disarmed and the cancel sees nothing in flight — it can't
    // clobber a freshly-queued scripted move on the same tick the scene
    // takes over.
    PHASE("guidance.cancel", acc::guidance::PollMovementKeysCancel());

    // Diagnostic: log player action-queue depth changes (delta only).
    PHASE("engine.actionQueueDiag", acc::engine::TickActionQueueDiag());

    // ----- Batch 3 (KOTOR 2 port): world / area / transitions -----
    // These five run on BOTH games. Their whole read/call chains were
    // resolved for KOTOR 2 offline (camera pos/yaw Pick'd, LocalToWorld /
    // GetArea / client GetGameObject / GetNPCObject / GetObjectName all
    // Pick'd) — see docs/kotor2-port.md Batch 3.

    // Drain deferred Q/E reannounce.
    PHASE("passive_narrate", acc::passive_narrate::Tick());

    // Both games since the K2 systems pass (2026-08-02): its whole surface
    // (GetClientLeader / GetActiveLeaderName / GetPlayerPosition / hotkeys)
    // was already Pick'd and live on KOTOR 2 via other callers.
    PHASE("party_leader_announce", acc::party_leader_announce::Tick());

    // ----- ORDER LOAD-BEARING -----
    // camera_announce → camera_orient → spatial::change_detector →
    // transitions → view_mode.
    //   camera_announce derives camera yaw from positions.
    //   camera_orient reads it for closed-loop arrival (same frame).
    //   spatial::change_detector reads camera yaw + rebuilds wall cache.
    //   transitions builds room_topology that depends on the wall cache —
    //     must run AFTER change_detector or the first tick of an area
    //     change uses stale walls from the previous area.
    //   view_mode reads camera yaw + walls + region/landmark caches.
    PHASE("camera_announce", acc::camera_announce::Tick());
    // Door-open facing readout — after camera_announce so its same-sector
    // dedup sees this frame's last-spoken direction. Cheap when idle.
    PHASE("door_announce", acc::door_announce::Tick());

    // Both games since the K2 systems pass (2026-08-02).
    // locked_recall: TLK lookup + narrated-target slot only, both ported.
    // camera_orient + spin guard ported TOGETHER (the guard is a workaround
    // for the active edge-turn driver — never split them): their surface is
    // camera pos/yaw (Pick'd, live via camera_announce), TurnScancode
    // (engine-constant-free — INI + DIK fallback) and GetMouseLook, whose
    // CClientOptions chain was witnessed on the K2 exe this pass.
    PHASE("locked_recall", acc::locked_recall::Tick());
    PHASE("camera_orient", acc::camera_orient::Tick());
    PHASE("camera_spin_diag", acc::camera_spin_guard::Tick());

    PHASE("spatial.change_detector", acc::spatial::change_detector::Tick());

    if (k1) {
        // Swoop race entry/exit cues. Gated to CSWMiniGame.type==0.
        PHASE("swoop_race", acc::swoop_race::Tick());

        // Turret / space-combat gunner minigame — shares CSWMiniGame with the
        // swoop race but reports type==3. Entry/exit announce + reticle diag.
        PHASE("turret_game", acc::turret_game::Tick());
    }

    // Area + room transition announces.
    PHASE("transitions", acc::transitions::Tick());

    // Both games since the K2 systems pass (2026-08-02): discovery's whole
    // closure was already Pick'd (ScriptVarTable 0x100->0x104, the four var
    // accessors, CExoString ctor/dtor — resolved in the Batch 3c address
    // rounds); only this gate was still standing.
    PHASE("discovery", acc::discovery::Tick());

    if (k1) {
        // Endar Spire room-5 softlock guard + plot-state diagnostic. Inert
        // outside END_M01AA (self-gated); flushes queued door hints every
        // frame.
        PHASE("endar_softlock", acc::endar::Tick());

        // Temple floor-plate puzzle assist — inert outside unk_m44ab.
        // Internally throttled to 150ms scans.
        PHASE("floor_puzzle", acc::floor_puzzle::Tick());
    }

    // Both games since the K2 systems pass (2026-08-02): the three per-kind
    // trap detected-by list offsets are witnessed in the K2 UpdateMineCheck
    // twin (see engine_area.h). Mirrors the engine's per-trap detected-by
    // lists for sighted-parity trap warnings; 250ms internal throttle.
    PHASE("trap_watch", acc::trap_watch::Tick());

    // Both games since the K2 systems pass (2026-08-02) — see the
    // view_mode.poll note above. Same slot as before (after floor_puzzle,
    // before map_ui_cursor) so cursor/listener ordering is unchanged.
    PHASE("view_mode", acc::view_mode::Tick());

    // Map UI cursor — gates on PanelKind::InGameMap. Both games since the
    // map batch: the transform block is Same (K2 SetMapData witness), the
    // hider/waypoint-list offsets and the fog + rotate entry points are
    // Pick'd from the K2 map ctor and fog-method decompiles.
    PHASE("map_ui_cursor", acc::map_ui_cursor::Tick());

    // KOTOR 2 pad action menu: close the engine's own and open ours in its
    // place. Self-gates to KOTOR 2 and is one global read when idle. Placed
    // with the other in-world surfaces, before the combat block it queues
    // into. The Y-button Quick Menu needs nothing here — it enumerates as an
    // ordinary panel and the navigation chain reads it (see
    // PanelKind::GamepadQuickMenu).
    PHASE("pad_actionmenu", acc::pad::actionmenu::Tick());

    // Stuck-detection — feeds g_was_stuck for OnPlayFootstep. Both games
    // since the footstep port (2026-08-02): the whole chain was already
    // Pick'd (player position, area iteration, cached walls, combat mode);
    // only the K2 hook itself was missing (twin 0x00765E90, found after
    // the Batch-5 candidate 0x0077D390 was refuted as the class-selection
    // idle-animation driver).
    PHASE("footstep_suppress", acc::audio::footstep_suppress::Tick());

    // Combat — mode entry/exit, log narration, attack resolution, saves,
    // leader-change announce, examine panel monitor, queue submenu,
    // examine view, specials heartbeat. Both games since Batch 4: the
    // combat-round/action structs are witnessed on KOTOR 2 (action struct
    // byte-identical, round block at +0x150, creature→round at +0x10dc),
    // the msg listbox / pause / combat-mode accessors are Pick'd, and the
    // few still-unresolved diagnostics (effect icons, faction id, stealth)
    // decline under their own guards.
    PHASE("combat.mode", acc::combat::TickCombatMode());
    PHASE("combat.log", acc::combat::TickCombatLog());
    PHASE("combat.absorb", acc::combat::TickCombatAbsorb());
    PHASE("combat.deflect", acc::combat::TickCombatDeflect());
    PHASE("combat.effects", acc::combat::TickCombatEffects());
    PHASE("combat.leaderChange",
          acc::combat::query::TickLeaderChangeAutoAnnounce());
    PHASE("combat.queue", acc::combat::queue::Tick());
    PHASE("combat_diag", acc::combat_diag::Tick());
    PHASE("examine_view", acc::examine_view::Tick());

    // Help overlay lifecycle (self-disarm on world teardown, list state).
    // Engine-free apart from the in-world pause hold, whose engine calls are
    // guarded — and menus never take the hold. Runs on both games, in its
    // original slot between examine_view and special_watch.
    PHASE("help.tick", acc::help::Tick());

    // Both games since Batch 4 — the special-watch chain (combat round,
    // action struct, attack feat) is fully witnessed on KOTOR 2.
    PHASE("combat.special_watch", acc::combat::special_watch::Tick());

    // Stealth distance readout — while the leader is stealthed and a
    // hostile creature is the narrated-target focus, speak the closing
    // distance every ~2 m. Inert otherwise (no stealth / no hostile
    // focus). Both games since the K2 systems pass (2026-08-02):
    // stealth_mode witnessed at K2 creature+0x511 (see stealth_watch.cpp).
    PHASE("stealth_watch", acc::stealth_watch::Tick());

    // Weapon-set swap announcement — KOTOR 2 only (gated inside; KOTOR 1
    // has no second weapon pair). Detects the cross-exchange of the four
    // weapon-slot handles (the engine stores no active-set flag), so the
    // SwitchWeaps key, the equip screen's swap button and scripted swaps
    // all announce what the leader now holds.
    PHASE("weapon_set_watch", acc::weapon_set_watch::Tick());

    // Dialog screen + bark bubble narration. Both games since Batch 3b —
    // its whole constant closure is resolved for KOTOR 2 (12/12; the reply
    // block re-derived past the message-ring collision) and its panels come
    // through the ported GUI spine.
    PHASE("dialog_speech", acc::dialog_speech::Tick());

    // Enter (interact) — engine click pipeline with localised pre-roll.
    // Both games since Batch 3c (picker chain + action-queue primitives
    // fully Pick'd; the KOTOR 2 in-world input hook carries the
    // modifier-space reservation this shares keys with).
    PHASE("interact", acc::input_poll::PollHotkey());

    // KOTOR 1 gamepad buttons. Deliberately HERE and not in pad_input's early
    // sample phase: these presses open the unified action menu and dispatch
    // interactions, which is exactly what the keyboard poll above does from
    // exactly this point. Arming that menu from the early tick is what
    // produced the phantom-confirm bug the K2 action-menu watcher records.
    // No-op on KOTOR 2, whose engine delivers the same presses as events.
    PHASE("pad_buttons", acc::pad::PollButtons());

    // The one button the engine's pad path cannot deliver in a menu: Start,
    // which is the mod's H key (own status; with the left trigger, the action
    // queue). Both games, read from XInput, and in this slot for the same
    // reason the buttons above are — it opens an overlay.
    PHASE("pad_xinput_buttons", acc::pad::PollXInputButtons());

    // The trigger layer's solo jobs, BOTH games — the left trigger opens and
    // closes the unified action menu, so it needs this slot for the same
    // reason the buttons above do. After them, so a menu opened this tick
    // isn't also fired into by an A press sampled in the same tick.
    PHASE("pad_triggers", acc::pad::PollTriggers());

    // Verdict on any action fire that produced no queue add. After every input
    // poll above, so an add caused by this tick's dispatch has already been
    // counted; the watch itself only speaks once its window has expired.
    PHASE("queue_noop", acc::combat::queue::TickNoOpWatch());

    // KOTOR 1 gamepad sticks -> movement and camera. After the buttons so a
    // press that just opened a menu has already stood the stick down this
    // tick, rather than walking one frame into it.
    PHASE("pad_movement", acc::pad::movement::Tick());

    // KOTOR 1 quick menu (Y): self-disarm only — its navigation comes from the
    // pad seam. Cheap when closed.
    PHASE("pad_quickmenu", acc::pad::quickmenu::Tick());

    // Release the level-up wizard's overlay pause once the panel closes
    // (the wizard's own Accept/Back buttons close it, so there's no close
    // site to call EndOverlayPause). After PollHotkey so a wizard opened
    // this frame is already live when we check. Both games since the
    // level-up batch: the whole Shift+L chain (CanLevelUp, SetLevelUpMode,
    // both ShowLevelUpGUI variants, the wizard vtable) is Pick'd with
    // decompile witnesses.
    PHASE("levelup_pause", acc::engine_levelup::TickLevelUpPause());

    // MessageBoxModal close cleanup — engine's close path doesn't unpause
    // or unmute on its own. Both games since Batch 4/5: SetPauseState,
    // SetSoundMode (per-game signature) and the ExoSound global are all
    // Pick'd with byte-level witnesses.
    PHASE("engine.inputReassert", acc::engine::TickInputClassReassert());

    // Audio-glossary delayed-playback timer.
    PHASE("modsettings", acc::menus::modsettings::Tick());

    // In-game auto-updater: F5 poll + background-check announce + handoff
    // batch spawn on download completion. Cheap when idle (one atomic load).
    PHASE("update_checker", acc::update_checker::Tick());

    // Drain the Pazaak deck-builder's staged add/remove/play before the generic
    // pending-op drain (a Play queues an Activate that drains in TickPendingOps).
    if (k1) PHASE("pazaakdeck", acc::menus::pazaakdeck::Tick());

    // Drain queued actions LAST — monitors above must see consistent state.
    PHASE("menus.TickPendingOps", acc::menus::TickPendingOps());

    acc::hotkeys::EndTick();

    WatchdogEndTick(tickStart);
}

#undef PHASE

}  // namespace acc::tick

// CSWGuiManager::Update detour — @0x40ce76 on KOTOR 1 (argument = the manager,
// from EBP), @0x4113a9 on KOTOR 2 (argument = the frame pointer). Per-frame,
// post-input. Safe callback site for deferred cursor moves — input pipeline
// isn't mid-flight. The argument is ignored on both games, which is what makes
// one handler serve both hook shapes; Dispatch() itself decides per-game which
// phases run (Batch 1: KOTOR 2 gets the menu spine only).
extern "C" void __cdecl OnUpdate(void* /*unused*/) {
    acc::tick::Dispatch();
}
