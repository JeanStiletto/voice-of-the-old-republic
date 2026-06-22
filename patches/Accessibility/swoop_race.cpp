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
#include "log.h"
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
    // (Side-wall collision state moved to swoop_spatial_audio.cpp — see the
    // note where the old detector used to live.)
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
        g_state.gear           += 1;
        g_state.last_max_speed  = maxSpeed;

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

void AnnounceExit() {
    const char* msg = acc::strings::Get(acc::strings::Id::SwoopRaceEnded);
    if (msg && *msg) prism::SpeakUrgent(msg);
    acclog::Write("SwoopRace", "spoke exit: [%s]", msg ? msg : "");
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
    AnnounceExit();
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
    TickSpatialAudio(g_state.latched_mini_game);
    EmitDiagSnapshot(g_state.latched_mini_game);
}

}  // namespace acc::swoop_race
