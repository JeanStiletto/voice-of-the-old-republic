// Swoop race minigame accessibility — see swoop_race.h for design.
//
// Phase 1 lay-off (current): entry/exit announce + keybind cheat sheet,
// best-effort gear-shift announce derived from CSWTrackFollower.speed
// jumps, per-tick diagnostic snapshot (speed + tunnel offset). Latches
// the CSWMiniGame pointer on first detect because the area chain
// (GetCurrentArea → GetClientArea → mini_game) churns during the
// race-start transition and stops resolving the minigame even while
// the race is still running — verified live by patch-20260524-163552.log
// where ENTER fired at T+0 and a spurious EXIT fired at T+125 ms even
// though the actual race continued for 40 more seconds.

#include "swoop_race.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"      // GetClientArea + map-pin chain (back-pointer)
#include "engine_offsets.h"   // Vector
#include "engine_player.h"    // kAddrAppManagerPtr (server-app chain for the
                              //   real race-timer read — see TickRaceTimer)
#include "log.h"
#include "audio_bus.h"        // PlayCue — non-positional "you can shift now" cue
#include "prism.h"            // SpeakUrgent — entry/exit/gear must beat
                              //              NVDA's typed-character cancel
                              //              (race input is held Space/Enter)
#include "strings.h"          // Get(SwoopRaceStarted/Controls/Ended) +
                              //     FmtSwoopRaceGear
#include "swoop_spatial_audio.h"  // TickSpatialAudio + ResetSpatialAudio

namespace acc::swoop_race {

namespace {

// ============================================================================
// Engine struct offsets (from docs/llm-docs/re/swkotor.exe.h).
// ============================================================================
//
// CSWCArea.mini_game (verified line 8726, after rooms@+0x260):
constexpr size_t kClientAreaMiniGameOffset      = 0x264;

// CSWMiniGame (line 9309). type/counts confirmed by name; obstacle data
// pointer at +0x44 + count at +0x48 confirmed from the patch-
// 20260524-163552.log first-fire byte dump (obstacles_ptr=0x1721AA08,
// count=22, the resref "m03mg" sits at the +0x0c CResRef as expected).
constexpr size_t kMiniGameVtableOffset          = 0x00;  // pointer
constexpr size_t kMiniGameResrefOffset          = 0x0c;  // CResRef[16]
constexpr size_t kMiniGamePlayerOffset          = 0x24;
constexpr size_t kMiniGameEnemyCountOffset      = 0x30;
constexpr size_t kMiniGameObstacleDataOffset    = 0x44;  // CSWMiniGameObject** (verified live)
constexpr size_t kMiniGameObstacleCountOffset   = 0x48;
constexpr size_t kMiniGameTypeOffset            = 0x80;  // 1=swoop, 2=turret
                                                        // (CSWMiniGame::Load sets
                                                        // type ONLY to 1 or 2; was
                                                        // mis-read at +0x84=axis_x,
                                                        // matching 0/3 by coincidence)
constexpr uint32_t kMiniGameTypeSwoop           = 1;

// CSWTrackFollower (line 15028) — base of CSWMiniPlayer. The byte-by-byte
// walk lines up with the swkotor.exe.h definition:
//   +0x00..0x5f  CSWMiniGameObject (60 bytes)
//   +0x60        mini_game ptr
//   +0x68..0x73  CExoArrayList models
//   +0x80        looping
//   +0x98        speed (float)
constexpr size_t kFollowerSpeedOffset           = 0x98;

// CSWMiniPlayer (line 15382, extends CSWTrackFollower size 0x1a4).
// Tunnel-frame offset (X=lane, Y=forward, Z=vertical) and the speed
// envelope.
constexpr size_t kMiniPlayerOffsetVectorOffset  = 0x1c4;
constexpr size_t kMiniPlayerMinSpeedOffset      = 0x1d8;
constexpr size_t kMiniPlayerMaxSpeedOffset      = 0x1dc;

// CSWMGObstacle / CSWMiniEnemy position-resolution offsets, vtable
// downcast slots, and the global CSWMiniGameObjectArray layout were
// split into swoop_spatial_audio.cpp on 2026-05-27 because they're
// used only by the per-tick obstacle / accelpad spatial-audio sweeps.
// See swoop_spatial_audio.cpp for the layout commentary, vtable
// downcast pattern, and pool-split rationale (obstacles vs enemies).

// ----- (removed) Acceleration progress "speedometer" tick -----
//
// A re-fired tick whose cadence scaled with (speed-min)/(max-min) used to
// model a filling throttle bar. Removed 2026-06-20: the decompile of
// CSWMiniPlayer::Control + the onfire.ncs gear script proved there is NO
// analog held-throttle in swoop. The bike auto-accelerates to max_speed on
// its own (engine, always-on), and each accelerate press is a DISCRETE shift
// event (onfire raises max_speed one notch: 35→60→100→150→210 and plays the
// native Shift1/2/3 + Engine0N sounds). So a "bar filling" cue modelled a
// mechanic that doesn't exist. See the session report / camera-and-swoop.md.

// ============================================================================
// Tunable behaviour parameters.
// ============================================================================

// Minimum jump in max_speed that counts as a "shift up" event.
//
// The engine itself has NO gear concept (Ghidra-confirmed 2026-05-24
// against CSWMiniPlayer::Control @0x66d640): the per-tick movement
// function only clamps speed against max_speed, and max_speed is
// settable only via the NWScript SWMG_SetPlayerMaxSpeed dispatcher
// (CSWMiniPlayer::SetMaxSpeed @0x66cf70 has exactly one caller,
// ExecuteCommandSWMG_SetPlayerFloatInfo @0x5cce70). So the entire
// gear ladder lives in the per-track NWScript — different swoop
// tracks may have different gear counts and different speed bands.
//
// Empirical floor from the 2026-05-24 race (tar_m03mg, gear 1 max=70,
// gear 2 max=120, delta 50; same-gear jitter < 1 m/s). 5 m/s is well
// above the jitter and well below the smallest plausible shift.
constexpr float kGearShiftMaxDeltaMs      = 5.0f;

// ----- "You can shift now" cue (Option A — exact onfire.ncs gate) -----
//
// onfire.ncs (the manual-accelerate StartingConditional, decompiled from
// build/swoop-rim/onfire.ncs 2026-06-22) only upshifts from gear N when the
// CURRENT speed already exceeds gear N+1's min-speed; below that the accelerate
// press is a SILENT no-op. So the earliest legal shift moment is the frame
// speed first crosses that gate. We fire a one-shot cue there so the player can
// shift as early as possible — the gate always sits BELOW the gear's own
// max_speed, so you never have to redline to upshift.
//
// The gate ladder lives in per-track NWScript, but all three shipped swoop
// tracks share it (Taris/Tatooine/Manaan: gear min-speeds 35/60/100/150/210;
// only the per-track *_SWOOP_ACCEL accel rate differs). Indexed by the CURRENT
// gear; the value is the speed you must EXCEED to upshift out of it. 0 = no
// gate (gear 0 launch is unconditional; gear 5 is top). Gears 1..4 are gated.
constexpr float kShiftGateSpeed[]         = { 0.0f, 60.0f, 100.0f, 150.0f,
                                              210.0f, 0.0f };
constexpr int   kFirstGatedGear           = 1;
constexpr int   kLastGatedGear            = 4;

// Reuse the turret minigame's "entered killable range" cue — same meaning to
// the player (a window just opened), already mixed and volume-grouped. See
// turret_game.cpp kRangeCueResref.
constexpr const char* kShiftReadyCueResref = "c_drdastro_atk1";

// Exit-debounce. After we lose the latched pointer, hold off announcing
// EXIT until the loss persists for this many consecutive ticks. The
// race-start transition flips the area chain in < 5 ticks, so 60 ticks
// (~2 s at 30 fps) is comfortably past that without making true exits
// feel laggy.
constexpr int   kExitDebounceTicks        = 60;

// Per-tick diagnostic snapshot cadence. 1 Hz keeps the log usable
// while still capturing the speed curve.
constexpr ULONGLONG kDiagLogIntervalMs    = 1000;

// Continuous obstacle + accelpad proximity cue parameters now live in
// swoop_spatial_audio.cpp alongside the per-tick MGO walk.

// ----- Side-wall collision detection -----
//
// MOVED to swoop_spatial_audio.cpp (2026-06-22). The reactive stall-based
// detector that used to live here (EMA of lateral dx → "was moving, now
// stalled at the edge" → fire) was replaced by a *predictive* overshoot cue in
// the co-pilot: it fires the same wall-impact sound ~0.1s BEFORE the bike pins,
// and only on a genuine overshoot (moving away from the target pad toward the
// wall). That cue lives next to the co-pilot because the overshoot guard needs
// the target-pad direction, and folding it in let us delete this module's
// duplicate lateral-velocity tracker. See TickAccelpadCues.

// ============================================================================
// Module state. Single-threaded under the engine OnUpdate tick.
// ============================================================================

struct State {
    // True iff we've announced ENTER and not yet announced EXIT.
    bool          active                  = false;

    // Latched CSWMiniGame pointer. Once we see a non-null mini_game via
    // the area chain we cache it here and verify per-tick by re-reading
    // its vtable; this is the only way to stay locked through the
    // race-start transition shuffle.
    void*         latched_mini_game       = nullptr;
    void*         latched_vtable          = nullptr;

    ULONGLONG     entered_at_ms           = 0;
    bool          full_diagnostic_emitted = false;
    ULONGLONG     last_diag_log_ms        = 0;

    // Exit debounce counter. Ticks since latched vtable became invalid;
    // resets when it re-validates. Announce EXIT only when this
    // crosses kExitDebounceTicks.
    int           ticks_since_lost        = 0;

    // Gear-tracking state. Reset on every fresh ENTER.
    //
    // Engine model (live-confirmed in patch-20260524-165828.log):
    // each manual shift WIDENS the speed envelope (gear 1 = [35..70],
    // gear 2 = [60..120], …). max_speed only moves UP and only on a
    // real shift event — within a gear, the bike accelerates from
    // min_speed toward max_speed without changing either bound. So
    // the true gear count is "number of distinct max_speed plateaus
    // observed so far", not "speed bucket within current band". A
    // jump of >= kGearShiftMaxDeltaM/s in max_speed counts as a new
    // gear.
    int           gear                    = 0;
    float         last_max_speed          = 0.0f;

    // "You can shift now" cue latch. True once we've fired the cue for the
    // CURRENT gear; re-armed by TickGearWatch on every detected shift, so the
    // cue fires at most once per gear. See kShiftGateSpeed / TickShiftReady.
    bool          shift_ready_announced   = false;
    // (Side-wall collision state moved to swoop_spatial_audio.cpp — see the
    // note where the old detector used to live.)

    // ---- Real race-timer state (see TickRaceTimer). ----
    // race_start_ms is the engine world-clock ms-of-day stamped at the "Go!"
    // signal, cached once from the MIN_TIME_* NWScript globals (constant for
    // the whole race). race_time_seconds is (world clock − start) frozen at
    // the finish-line crossing — the value announced on EXIT.
    bool          have_start_ms           = false;
    uint32_t      race_start_ms           = 0;
    uint32_t      race_mph                = 2;     // world calendar minutes/hour
    float         race_max_speed          = 0.0f;  // running top envelope speed
                                                   //   (envelope-finish "did we race" ref)
    float         race_peak_speed         = 0.0f;  // running top ACTUAL speed
                                                   //   (coast-fallback "did we race" ref —
                                                   //   must NOT use the envelope, which is
                                                   //   already 70 at launch and would let
                                                   //   the 1->10 launch ramp false-trigger)
    bool          have_race_time          = false;
    float         race_time_seconds       = 0.0f;
    // Idempotency latch: the race-end cue is spoken once — by whichever fires
    // first, the terminal-stop detector in TickRaceTimer (preferred: lands in
    // the clear air before the post-race heading narration) or HandleExit
    // (fallback for an abnormal end where the bike never decelerated).
    bool          race_time_announced     = false;
};

State g_state;

// ============================================================================
// SEH-guarded primitive reads. Same pattern as the rest of engine_*.
// ============================================================================

void* SafeReadPtr(void* base, size_t off) {
    if (!base) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(base) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

uint32_t SafeReadU32(void* base, size_t off) {
    if (!base) return 0;
    __try {
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(base) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

float SafeReadFloat(void* base, size_t off) {
    if (!base) return 0.0f;
    __try {
        return *reinterpret_cast<float*>(
            reinterpret_cast<unsigned char*>(base) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

bool SafeReadVector(void* base, size_t off, Vector& out) {
    if (!base) return false;
    __try {
        Vector* v = reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(base) + off);
        out = *v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Read CSWCArea.mini_game via the player-area chain. Source of truth at
// the moment of detection; we latch the result so churn in this chain
// during transitions doesn't drop us mid-race.
void* ReadMiniGameViaArea() {
    void* serverArea = acc::engine::GetCurrentArea();
    if (!serverArea) return nullptr;
    void* clientArea = acc::engine::GetClientArea(serverArea);
    return SafeReadPtr(clientArea, kClientAreaMiniGameOffset);
}

// Validate a latched CSWMiniGame pointer by re-reading its vtable and
// comparing against the value captured at latch time. Returns true iff
// the pointer is still readable and the vtable hasn't been overwritten
// (engine frees CSWMiniGame at race-end and the heap slot tends to get
// reused — vtable mismatch is a reliable "no longer the same struct"
// signal).
bool LatchedStillValid() {
    if (!g_state.latched_mini_game || !g_state.latched_vtable) return false;
    void* vt = SafeReadPtr(g_state.latched_mini_game, kMiniGameVtableOffset);
    return vt == g_state.latched_vtable;
}

// ============================================================================
// Real race timer (engine world clock − NWScript start-stamp globals).
// ============================================================================
//
// KOTOR's swoop timer is NWScript-driven: each track's heartbeat stamps the
// race START time into the global-number table (MIN_TIME_HOUR/MIN/SEC/MIL,
// the last in centiseconds) at the "Go!" signal, then the displayed clock is
// (current world time − that stamp). No engine struct holds elapsed race time
// (CSWMiniGame / CSWMiniPlayer are pure geometry), so we reproduce the script:
//
//   server  = *AppManager + 0x8                          (CServerExoApp)
//   timer   = CServerExoApp::GetWorldTimer(server)        (CWorldTimer)
//   GetWorldTime(timer,&day,&nowMs)                       nowMs = ms-of-day
//   mph     = timer->minutes_per_hour  (+0x38, byte)      (compressed calendar)
//   table   = CServerExoApp::GetGlobalVariableTable(server)
//   sH/sM/sS/sC = GetValueNumber(table,"MIN_TIME_*")      start-stamp parts
//   startMs = (((sH*mph + sM)*60 + sS)*1000) + sC*10
//   elapsedMs = nowMs − startMs
//
// GetWorldTimeHour/Minute divide by minutes_per_hour, so the H/M/S split is
// NOT a normal clock — we reassemble through mph. Addresses decompiled
// 2026-06-23 (ExecuteCommandGetTimeHour / GetTimeMillisecond / GetGlobalNumber
// and the CWorldTimer / CSWGlobalVariableTable accessors). The start stamp is
// constant for the race, so it's cached on first valid read.
constexpr uintptr_t kAddrCServerExoAppGetWorldTimer     = 0x004aede0;
constexpr uintptr_t kAddrCWorldTimerGetWorldTime        = 0x004ade40;
constexpr uintptr_t kAddrCServerExoAppGetGlobalVarTable = 0x004aee60;
constexpr uintptr_t kAddrGlobalVarTableGetValueNumber   = 0x00529240;
constexpr size_t    kWorldTimerMinutesPerHourOffset     = 0x38;  // byte
// AppManager (global ptr kAddrAppManagerPtr) + 0x8 → CServerExoApp.
constexpr size_t    kAppManagerServerExoAppOffset       = 0x8;

typedef void* (__thiscall* PFN_GetWorldTimer)(void* server);
typedef void  (__thiscall* PFN_GetWorldTime)(void* timer, uint32_t* outDay,
                                             uint32_t* outMs);
typedef void* (__thiscall* PFN_GetGlobalVarTable)(void* server);
typedef void  (__thiscall* PFN_GetValueNumber)(void* table, void* nameExoStr,
                                               int* outValue);

// Matches CExoString { char* c_string; ulong length; }. GetValueNumber only
// reads it (hash + compare), never frees — a stack literal is safe.
struct EngineExoString { const char* c_string; uint32_t length; };

void* GetServerApp() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerServerExoAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// One global NUMBER (engine stores it as a byte) by name. -1 on failure; the
// engine writes 0 for an unknown name.
int ReadGlobalNumber(void* server, const char* name) {
    if (!server || !name) return -1;
    __try {
        auto getTable = reinterpret_cast<PFN_GetGlobalVarTable>(
            kAddrCServerExoAppGetGlobalVarTable);
        void* table = getTable(server);
        if (!table) return -1;
        EngineExoString nameStr{ name, static_cast<uint32_t>(std::strlen(name)) };
        int value = 0;  // GetValueNumber writes only the low byte
        auto getNum = reinterpret_cast<PFN_GetValueNumber>(
            kAddrGlobalVarTableGetValueNumber);
        getNum(table, &nameStr, &value);
        return value & 0xff;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Current world-clock time-of-day in ms (the value GetTime* read) + the
// calendar's minutes-per-hour. False on any null link / SEH fault.
bool ReadWorldClockMs(void* server, uint32_t& outMs, uint32_t& outMph) {
    if (!server) return false;
    __try {
        auto getTimer = reinterpret_cast<PFN_GetWorldTimer>(
            kAddrCServerExoAppGetWorldTimer);
        void* timer = getTimer(server);
        if (!timer) return false;
        uint32_t day = 0, ms = 0;
        auto getTime = reinterpret_cast<PFN_GetWorldTime>(
            kAddrCWorldTimerGetWorldTime);
        getTime(timer, &day, &ms);
        uint32_t mph = *(reinterpret_cast<unsigned char*>(timer) +
                         kWorldTimerMinutesPerHourOffset);
        if (mph == 0) mph = 2;  // KOTOR default; guards a bad read
        outMs  = ms;
        outMph = mph;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Finish detection. The engine ZEROES the speed envelope (min_speed = max_speed
// = 0) the instant the bike crosses the finish line and race control ends —
// observed live: the per-tick snapshot flips from e.g. [210..420] to [0..0]
// while the bike is still coasting at ~120 u/s. So the last tick with a live
// envelope (max_speed > 0) IS the finish crossing; we update the elapsed every
// active tick and let the zero-transition freeze it there.
//
// Superseded: a "speed within 0.9 * running-max" band, which actually froze the
// clock at the speed PEAK (mid-race), not the finish — so a slow run that peaked
// early reported the same (or a better) time than a fast one. patch-20260623-
// 111743: a gear-4 run and a gear-5 run both announced ~24.1 s despite a ~4 s
// real-clock gap, because both peaked around the same world-time.
constexpr float kRaceTimeMaxSeconds = 600.0f;   // implausible-read ceiling
constexpr long  kRaceStartFreshMaxMs = 5000;    // start-stamp freshness window
// Race-end announce: fire once the finish is crossed (envelope zeroed) on a bike
// that actually raced (running envelope cleared kRaceMinTopSpeed). kTerminalStop
// is a fallback trigger for an abnormal end where the envelope never zeroes and
// the bike just coasts below it. Rock hits reset speed to the gear minimum
// (>=35), never this low, so the fallback won't trip mid-race.
constexpr float kTerminalStopSpeed  = 10.0f;
constexpr float kRaceMinTopSpeed    = 40.0f;

// Speaks the race-end cue once per race (forward-declared: TickRaceTimer fires
// it on the terminal stop; the definition with the HandleExit fallback is
// further down).
void SpeakRaceEndMessage();

// Per-tick: cache the start stamp once, then freeze the elapsed at each
// finish-band tick. Called only while the latch is alive.
void TickRaceTimer(void* miniGame) {
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) return;
    const float speed    = SafeReadFloat(player, kFollowerSpeedOffset);
    const float maxSpeed = SafeReadFloat(player, kMiniPlayerMaxSpeedOffset);

    // Finish crossing = the speed envelope just zeroed (maxSpeed -> 0) on a bike
    // that reached a real gear; see the kRaceTimeMaxSeconds block. Announce here,
    // in the clear air right after the finish while the bike coasts, using
    // race_time_seconds frozen at the last live-envelope tick.
    //
    // Fallback: an abnormal end where the envelope never zeroes but the bike has
    // actually RACED and coasts below kTerminalStopSpeed. CRITICAL: the fallback
    // is gated on peak ACTUAL speed, not the envelope peak. The envelope is
    // already 70 the instant gear 1 engages at launch, so gating the fallback on
    // it would let the launch ramp (speed climbing through [1..10]) fire the
    // speed<10 trigger and end the race at ~0 s — the launch-ramp bug. Actual
    // peak speed only clears kRaceMinTopSpeed once the bike is genuinely fast, by
    // which point speed is well above kTerminalStopSpeed. (Checked before the
    // launch guard below because at the finish speed is still high but maxSpeed
    // is already 0.)
    if (g_state.have_race_time && !g_state.race_time_announced) {
        const bool envelope_finished =
            g_state.race_max_speed > kRaceMinTopSpeed && maxSpeed <= 0.0f;
        const bool coasted_to_stop =
            g_state.race_peak_speed > kRaceMinTopSpeed && speed < kTerminalStopSpeed;
        if (envelope_finished || coasted_to_stop) {
            SpeakRaceEndMessage();
            return;
        }
    }

    if (speed <= 1.0f) return;     // countdown / not launched yet
    if (maxSpeed <= 0.0f) return;  // race already ended — envelope cleared

    void* server = GetServerApp();
    if (!server) return;

    // Cache the fixed start stamp once the heartbeat has written THIS race's
    // value. MIN_TIME_* persists between races, so a stale previous-race stamp
    // would read as a huge initial elapsed — defer until it's fresh (a
    // just-stamped start gives a sub-second elapsed at launch).
    if (!g_state.have_start_ms) {
        uint32_t nowMs = 0, mph = 2;
        if (!ReadWorldClockMs(server, nowMs, mph)) return;
        const int sH = ReadGlobalNumber(server, "MIN_TIME_HOUR");
        const int sM = ReadGlobalNumber(server, "MIN_TIME_MIN");
        const int sS = ReadGlobalNumber(server, "MIN_TIME_SEC");
        const int sC = ReadGlobalNumber(server, "MIN_TIME_MIL");  // centiseconds
        if (sH < 0 || sM < 0 || sS < 0 || sC < 0) return;
        const uint32_t startMs = static_cast<uint32_t>(
            (((sH * static_cast<int>(mph) + sM) * 60 + sS) * 1000) + sC * 10);
        const long initialElapsed =
            static_cast<long>(nowMs) - static_cast<long>(startMs);
        if (initialElapsed < 0 || initialElapsed > kRaceStartFreshMaxMs) return;
        g_state.race_start_ms = startMs;
        g_state.race_mph      = mph;
        g_state.have_start_ms = true;
        acclog::Write("SwoopRace",
                      "race timer start: %d:%d:%d.%02d mph=%u startMs=%u "
                      "nowMs=%u initElapsed=%ldms",
                      sH, sM, sS, sC, mph, startMs, nowMs, initialElapsed);
    }

    // Running top ENVELOPE speed — "did we race" ref for the envelope-zero finish
    // (a real gear pushes max_speed past kRaceMinTopSpeed). race_peak_speed is the
    // running top ACTUAL speed — the separate ref for the coast fallback (see the
    // finish guard above for why the two must not be conflated).
    if (maxSpeed > g_state.race_max_speed) g_state.race_max_speed = maxSpeed;
    if (speed    > g_state.race_peak_speed) g_state.race_peak_speed = speed;

    // Elapsed, every active tick. No speed-band gate: the envelope-zero finish
    // detector (above) freezes this at the crossing, so a slow run that never
    // reaches top gear still counts its full duration to the finish line.
    uint32_t nowMs = 0, mph = g_state.race_mph;
    if (!ReadWorldClockMs(server, nowMs, mph)) return;
    const long elapsedMs =
        static_cast<long>(nowMs) - static_cast<long>(g_state.race_start_ms);
    if (elapsedMs < 0) return;                       // clock wrap — skip tick
    const float seconds = elapsedMs / 1000.0f;
    if (seconds > kRaceTimeMaxSeconds) return;       // implausible — reject
    g_state.race_time_seconds = seconds;
    g_state.have_race_time    = true;
}

// ============================================================================
// Gear heuristic (max_speed-plateau model — see kGearShiftMaxDeltaMs).
// ============================================================================
//
// Each manual shift raises max_speed by tens of m/s; within a gear,
// max_speed is constant. Counting upward jumps over the noise floor
// gives the true gear number.

// ============================================================================
// Diagnostic dump (process-once). Captures CSWMiniGame + CSWMiniPlayer
// bytes so the obstacle-array offset and the (still-unconfirmed) gear
// field can be locked offline. Subsequent races skip the dump to avoid
// the per-entry log-flush stall that contributed to the false-EXIT
// race condition.
// ============================================================================

void EmitDiagnosticDump(void* miniGame) {
    if (!miniGame) return;
    uint32_t type      = SafeReadU32(miniGame, kMiniGameTypeOffset);
    uint32_t enemies   = SafeReadU32(miniGame, kMiniGameEnemyCountOffset);
    uint32_t obstacles = SafeReadU32(miniGame, kMiniGameObstacleCountOffset);
    void*    obstData  = SafeReadPtr(miniGame, kMiniGameObstacleDataOffset);
    void*    player    = SafeReadPtr(miniGame, kMiniGamePlayerOffset);

    acclog::Write("SwoopRace",
                  "diag: mini_game=%p type=%u enemies=%u obstacles=%u "
                  "obstacles_ptr=%p player=%p",
                  miniGame, type, enemies, obstacles, obstData, player);

    __try {
        acclog::WriteHex("SwoopRace", "mini_game bytes (0x100)",
                         miniGame, 0x100);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("SwoopRace", "diag: mini_game bytes dump faulted");
    }

    if (player) {
        __try {
            acclog::WriteHex("SwoopRace", "player bytes (0x250)",
                             player, 0x250);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("SwoopRace", "diag: player bytes dump faulted");
        }
    }
}

// Brief per-entry summary line. Always logged on ENTER, regardless of
// whether the full byte dump fired. One line, no WriteHex stall.
void EmitEntrySummary(void* miniGame) {
    uint32_t type      = SafeReadU32(miniGame, kMiniGameTypeOffset);
    uint32_t enemies   = SafeReadU32(miniGame, kMiniGameEnemyCountOffset);
    uint32_t obstacles = SafeReadU32(miniGame, kMiniGameObstacleCountOffset);
    void*    obstData  = SafeReadPtr(miniGame, kMiniGameObstacleDataOffset);
    void*    player    = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    void*    vtable    = SafeReadPtr(miniGame, kMiniGameVtableOffset);
    acclog::Write("SwoopRace",
                  "entry summary: mini_game=%p vtable=%p type=%u "
                  "enemies=%u obstacles=%u obstacles_ptr=%p player=%p",
                  miniGame, vtable, type, enemies, obstacles, obstData, player);
}

// ============================================================================
// Per-tick diagnostic snapshot.
// ============================================================================

void EmitDiagSnapshot(void* miniGame) {
    ULONGLONG now = GetTickCount64();
    if (now - g_state.last_diag_log_ms < kDiagLogIntervalMs) return;
    g_state.last_diag_log_ms = now;

    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) {
        acclog::Write("SwoopRace", "snapshot: player ptr null");
        return;
    }
    float speed    = SafeReadFloat(player, kFollowerSpeedOffset);
    float minSpeed = SafeReadFloat(player, kMiniPlayerMinSpeedOffset);
    float maxSpeed = SafeReadFloat(player, kMiniPlayerMaxSpeedOffset);
    Vector off;
    const bool offOk = SafeReadVector(player, kMiniPlayerOffsetVectorOffset, off);
    acclog::Write("SwoopRace",
                  "snapshot speed=%.2f [%.2f..%.2f] gear=%d "
                  "tunnel=%s (%.2f,%.2f,%.2f)",
                  speed, minSpeed, maxSpeed, g_state.gear,
                  offOk ? "ok" : "FAULT", off.x, off.y, off.z);
}

// ============================================================================
// Gear-change detection.
// ============================================================================

void TickGearWatch(void* miniGame) {
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) return;

    float maxSpeed = SafeReadFloat(player, kMiniPlayerMaxSpeedOffset);
    if (maxSpeed <= 0.0f) return;  // not yet initialised

    // Shift-up event: max_speed jumped by at least the noise floor.
    if (maxSpeed > g_state.last_max_speed + kGearShiftMaxDeltaMs) {
        g_state.gear                  += 1;
        g_state.last_max_speed         = maxSpeed;
        // Re-arm the shift-ready cue for the gear we just entered.
        g_state.shift_ready_announced  = false;

        char buf[64];
        const char* fmt = acc::strings::Get(acc::strings::Id::FmtSwoopRaceGear);
        if (fmt && *fmt) {
            std::snprintf(buf, sizeof(buf), fmt, g_state.gear);
            prism::SpeakUrgent(buf);
            acclog::Write("SwoopRace",
                          "gear shift -> %d (max=%.2f → was %.2f)",
                          g_state.gear, maxSpeed,
                          g_state.last_max_speed);
        }
    }
}

// ============================================================================
// "You can shift now" cue (Option A — exact onfire.ncs gate, see
// kShiftGateSpeed).
// ============================================================================
//
// Fires a one-shot cue the frame current speed first crosses the gate for the
// current gear, i.e. the earliest frame a manual shift would actually take.
// Latched per gear (TickGearWatch re-arms it on each real shift). Self-guards
// against a non-stock gate ladder: never announces a gate that sits at or above
// the gear's own max_speed (an unreachable gate the bike can't climb to), so a
// mod track with a different ladder degrades to silence, never a false cue.
void TickShiftReady(void* miniGame) {
    if (g_state.shift_ready_announced) return;

    const int gear = g_state.gear;
    if (gear < kFirstGatedGear || gear > kLastGatedGear) return;  // launch / top gear: no gate
    const float gate = kShiftGateSpeed[gear];
    if (gate <= 0.0f) return;

    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) return;

    const float maxSpeed = SafeReadFloat(player, kMiniPlayerMaxSpeedOffset);
    if (maxSpeed <= 0.0f || gate >= maxSpeed) return;  // self-guard: gate unreachable

    const float speed = SafeReadFloat(player, kFollowerSpeedOffset);
    if (speed <= gate) return;

    g_state.shift_ready_announced = true;
    acc::audio::PlayCue(kShiftReadyCueResref);
    acclog::Write("SwoopRace",
                  "shift-ready cue: gear=%d speed=%.2f > gate=%.2f (max=%.2f)",
                  gear, speed, gate, maxSpeed);
}

// Side-wall collision cue moved to swoop_spatial_audio.cpp (predictive overshoot
// cue in TickAccelpadCues — see the note above where the old detector lived).

// Continuous obstacle + accelpad proximity cues moved to
// swoop_spatial_audio.cpp on 2026-05-27 (large-file-handling split).
// That TU owns the per-tick MGO array walk, the AsObstacle / AsEnemy
// downcasts, and the LoopSource lifecycles. Race lifecycle below
// just calls TickSpatialAudio() / ResetSpatialAudio().


// ============================================================================
// Speech.
//
// Combine opener + keybind cheat sheet into ONE urgent utterance so the
// two halves can't preempt each other (every SpeakUrgent uses
// interrupt=true — see prism::SpeakUrgent). Without this combine, the
// observed live behaviour was: opener cancelled by controls cancelled
// by the spurious EXIT 125 ms later, so the user heard nothing
// coherent.
// ============================================================================

void AnnounceEntry() {
    const char* opener   = acc::strings::Get(acc::strings::Id::SwoopRaceStarted);
    const char* controls = acc::strings::Get(acc::strings::Id::SwoopRaceControls);
    if ((!opener || !*opener) && (!controls || !*controls)) return;

    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s %s",
                  opener   ? opener   : "",
                  controls ? controls : "");
    prism::SpeakUrgent(buf);
    acclog::Write("SwoopRace", "spoke entry: [%s]", buf);
}

void SpeakRaceEndMessage() {
    if (g_state.race_time_announced) return;  // idempotent — first call wins
    g_state.race_time_announced = true;

    char buf[160];
    const char* msg = nullptr;
    // If we captured a finish-line time, speak the time-bearing variant (it
    // leads with the number and includes the "race ended" phrase); otherwise
    // the plain exit cue.
    if (g_state.have_race_time) {
        const char* fmt = acc::strings::Get(acc::strings::Id::FmtSwoopRaceTime);
        if (fmt && *fmt) {
            int whole = static_cast<int>(g_state.race_time_seconds);
            int centi = static_cast<int>(
                (g_state.race_time_seconds - static_cast<float>(whole)) * 100.0f
                + 0.5f);
            if (centi >= 100) { centi = 0; ++whole; }
            std::snprintf(buf, sizeof(buf), fmt, whole, centi);
            msg = buf;
        }
    }
    if (!msg) msg = acc::strings::Get(acc::strings::Id::SwoopRaceEnded);
    if (msg && *msg) prism::SpeakUrgent(msg);
    acclog::Write("SwoopRace",
                  "spoke race end: [%s] (raceTime=%.2f have=%d)",
                  msg ? msg : "", g_state.race_time_seconds,
                  g_state.have_race_time ? 1 : 0);
}

// ============================================================================
// State transitions.
// ============================================================================

void HandleEnter(void* mg) {
    g_state.active              = true;
    g_state.latched_mini_game   = mg;
    g_state.latched_vtable      = SafeReadPtr(mg, kMiniGameVtableOffset);
    g_state.entered_at_ms       = GetTickCount64();
    g_state.last_diag_log_ms    = 0;
    g_state.ticks_since_lost    = 0;
    g_state.gear                = 0;
    g_state.last_max_speed      = 0.0f;
    g_state.shift_ready_announced = false;
    g_state.have_start_ms       = false;
    g_state.race_start_ms       = 0;
    g_state.race_mph            = 2;
    g_state.race_max_speed      = 0.0f;
    g_state.race_peak_speed     = 0.0f;
    g_state.have_race_time      = false;
    g_state.race_time_seconds   = 0.0f;
    g_state.race_time_announced = false;
    // Defensive cleanup for obstacle + accelpad loops — any active
    // loop from a previous race must not survive into this one.
    ResetSpatialAudio();

    acclog::Write("SwoopRace",
                  "ENTER mini_game=%p vtable=%p",
                  mg, g_state.latched_vtable);

    // Speak FIRST, dump SECOND. The full byte dump fires a couple dozen
    // log writes which take real wall-time; doing them before
    // SpeakUrgent risks the engine ticking again before we queue the
    // utterance, in which case TickGearWatch / EmitDiagSnapshot would
    // pile up on the same audio frame.
    AnnounceEntry();
    EmitEntrySummary(mg);
    if (!g_state.full_diagnostic_emitted) {
        EmitDiagnosticDump(mg);
        g_state.full_diagnostic_emitted = true;
    }
}

void HandleExit() {
    ULONGLONG dur = GetTickCount64() - g_state.entered_at_ms;
    acclog::Write("SwoopRace",
                  "EXIT after %llu ms (debounced %d ticks)",
                  dur, kExitDebounceTicks);
    // Stop every obstacle + accelpad loop — some may still be active
    // if the race ended while objects were in range.
    ResetSpatialAudio();
    g_state.active              = false;
    g_state.latched_mini_game   = nullptr;
    g_state.latched_vtable      = nullptr;
    g_state.ticks_since_lost    = 0;
    g_state.gear                = 0;
    g_state.last_max_speed      = 0.0f;
    g_state.shift_ready_announced = false;
    // Fallback: if the terminal-stop detector already spoke the time (the
    // common path), this is a no-op; otherwise speak it now.
    SpeakRaceEndMessage();
}

}  // namespace

bool IsActive() { return g_state.active; }

void Tick() {
    // ---- Re-derive from area chain (ground truth when it works). ----------
    void* mgArea = ReadMiniGameViaArea();

    if (!g_state.active) {
        // Idle state. Fire ENTER only for an actual swoop race
        // (CSWMiniGame.type==1). The turret / space-combat gunner
        // minigame shares this exact struct (same vtable) but reports
        // type==2 and is handled by turret_game.cpp — entering here
        // would mis-announce it as a swoop race (and run the gear /
        // accel / wall heuristics, which are meaningless for a turret).
        // type is populated by the time the area chain exposes mini_game.
        // (Engine-confirmed: CSWMiniGame::Load only ever sets type to 1 or 2.
        // We previously read +0x84=axis_x and matched 0/3 by coincidence.)
        if (mgArea && SafeReadU32(mgArea, kMiniGameTypeOffset) == kMiniGameTypeSwoop) {
            HandleEnter(mgArea);
        }
        return;
    }

    // ---- We're active. Verify the latch is still alive. ------------------
    //
    // Two truth sources:
    //   1. Area chain still reports a mini_game (might be the same
    //      pointer, might be a fresh one for a new race).
    //   2. Latched pointer's vtable still reads the expected value.
    //
    // EXIT only when BOTH say "gone" continuously for kExitDebounceTicks.

    if (mgArea && mgArea != g_state.latched_mini_game) {
        // Area says minigame exists but at a different address. This
        // shouldn't happen mid-race, but if it does (engine swap, mod-
        // injected reset) re-latch onto the fresh struct so subsequent
        // reads track it.
        void* vt = SafeReadPtr(mgArea, kMiniGameVtableOffset);
        acclog::Write("SwoopRace",
                      "re-latch: old=%p new=%p vtable=%p",
                      g_state.latched_mini_game, mgArea, vt);
        g_state.latched_mini_game = mgArea;
        g_state.latched_vtable    = vt;
        g_state.ticks_since_lost  = 0;
    } else if (mgArea && mgArea == g_state.latched_mini_game) {
        // Area chain agrees with latch. Definitely alive.
        g_state.ticks_since_lost = 0;
    } else if (!mgArea && LatchedStillValid()) {
        // Area chain dropped to null (we lost GetCurrentArea visibility
        // — the race-start transition does this), but the latched
        // struct's vtable still reads as the same address it had at
        // ENTER, so the struct itself hasn't been freed. Stay locked.
        g_state.ticks_since_lost = 0;
    } else {
        // Both sources say gone. Hold off the announcement in case
        // it's a transient cross-tick race condition (engine briefly
        // overwriting / reallocating struct between area-side reads).
        ++g_state.ticks_since_lost;
        if (g_state.ticks_since_lost >= kExitDebounceTicks) {
            HandleExit();
            return;
        }
    }

    // ---- In-race observation (only when the latch is alive). -------------
    if (!g_state.latched_mini_game) return;

    TickGearWatch(g_state.latched_mini_game);
    TickShiftReady(g_state.latched_mini_game);
    TickSpatialAudio(g_state.latched_mini_game);
    TickRaceTimer(g_state.latched_mini_game);
    EmitDiagSnapshot(g_state.latched_mini_game);
}

}  // namespace acc::swoop_race
