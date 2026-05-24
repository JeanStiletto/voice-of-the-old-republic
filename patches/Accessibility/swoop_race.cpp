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

#include "audio_bus.h"        // PlayCue3D — reserved for the obstacle/booster
                              //             continuous-cue pass once the
                              //             obstacle array offset is locked
#include "engine_area.h"      // GetClientArea + map-pin chain (back-pointer)
#include "engine_offsets.h"   // Vector
#include "engine_player.h"    // GetCameraPosition (primary listener anchor
                              //                    during race — the engine
                              //                    swaps the player creature
                              //                    so GetPlayerPosition can't
                              //                    be relied on here),
                              //                    GetPlayerPosition (fallback)
#include "log.h"
#include "prism.h"            // SpeakUrgent — entry/exit/gear must beat
                              //              NVDA's typed-character cancel
                              //              (race input is held Space/Enter)
#include "strings.h"          // Get(SwoopRaceStarted/Controls/Ended) +
                              //     FmtSwoopRaceGear

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
constexpr size_t kMiniGameTypeOffset            = 0x84;

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

// CSWMGObstacle (line 17015):
//   +0x00..+0x5f  CSWMiniGameObject base (60 bytes)
//   +0x60         CAurObject *field1_0x60   (the 3D model)
//   +0x64         CResRef scripts[5]
// CAurObject wraps Gob; Gob.position (Vector) sits at +0x78 per the
// Gob struct layout (vtable + small fields + model + scene + Part +
// 2 undef + position).
constexpr size_t kObstacleAurObjectOffset       = 0x60;
constexpr size_t kAurObjectPositionOffset       = 0x78;

// ----- Global minigame-object-array layout (Ghidra-confirmed 2026-05-24).
//
// The first pass mistakenly treated CSWMiniGame+0x44 as a pointer to a
// flat obstacle array; live read returned obstacle[1]=0x00000001 and
// crashed on dereference. The correct path comes from decompiling
// CSWVirtualMachineCommands::ExecuteCommandSWMG_GetFollowerPosition
// @0x5cc280 + CSWMiniGameObjectArray::GetMiniGameObject @0x66bf30:
//
//   AppManager (*0x7a39fc) +0x4
//     -> CClientExoApp +0x4
//       -> CClientExoAppInternal +0x0
//         -> CSWMiniGameObjectArray (255 ptr slots, handle-indexed)
//             +0x00  ulong index (next-free hint)
//             +0x04  CSWMiniGameObject *objects[255]
//
// Each slot is a CSWMiniGameObject* (or null). To classify by type we
// invoke the vtable slot:
//     vtable[0x14] = AsFollower
//     vtable[0x18] = AsPlayer
//     vtable[0x1c] = AsEnemy
//     vtable[0x20] = AsObstacle
// Each returns the same `this` pointer downcast to the requested
// concrete subclass, or null if it isn't that type. So enumerating
// "all obstacles" = walk 255 slots, call vtable[0x20] thiscall on each
// non-null slot, keep the non-null returns.
constexpr size_t kClientInternalMgoArrayOffset  = 0x0;
constexpr size_t kMgoArrayObjectsOffset         = 0x4;
constexpr int   kMgoArraySlotCount              = 255;
constexpr size_t kVtableSlotAsObstacle          = 0x20;

// ----- Obstacle subtype routing -----
//
// All "obstacles" in the global MGO array share the CSWMGObstacle vtable
// (0x0075287C, verified live 2026-05-24). The engine doesn't subclass
// rocks vs accelerator pads at the C++ type level; the only per-instance
// distinguisher is the model name carried by the obstacle's CAurObject.
// CSWMGObstacle::GetName @0x66dc80 reads that name by calling
// CAurObject's vtable[0xc] (typed `char *(*)(void)`).
//
// Naming convention in K1 modules: accelerator pads typically have model
// names starting with "swp_acc" / "accpad" / similar. The list below is
// substring-matched (case-insensitive prefix) against the name; matches
// route to the accelerator cue, everything else to the warning cue.
constexpr size_t kAurVtableSlotGetName          = 0xc;
constexpr const char* kBoosterCueResref         = "mgs_accelpad";
// Substrings checked against the obstacle name (lower-cased, prefix
// match). Add new patterns here as we discover the names other tracks
// use for booster pads.
constexpr const char* kBoosterNamePatterns[]    = {
    "acc",     // catches "accpad", "accelpad", "swp_acc_*"
    "boost",
};

// ----- Acceleration progress cue -----
//
// External description of the bike's HUD (player-written game manual
// quote, cited 2026-05-24 by the user during this session):
//   "As you hold down the accelerator, your speed marker increases at
//    the bottom of the screen. When the bar is full, you need to tap
//    the acceleration button to shift gears. You can then immediately
//    hold down the acceleration button again to gain more speed."
//
// Sighted players see a horizontal speed bar that fills from
// (speed=min_speed) to (speed=max_speed); when it fills they tap to
// shift. Blind equivalent: a short tick sound whose REPETITION RATE
// scales with `(speed-min_speed)/(max_speed-min_speed)` — slow ticks
// at the bottom of the gear, very fast ticks at the top, telling
// the player "your bar is filling, get ready to tap". Tap shifts the
// envelope up so the next tick is back near the bottom; the rate
// resets to slow and starts climbing again. Same mental model as the
// visual bar, delivered as audio.
//
// The KOTOR engine has loop-capable engine samples (mgs_engine_NNl)
// but our audio_bus only exposes one-shot Play3DOneShot, so we
// simulate "continuous" via short re-fires at a variable interval.
constexpr const char* kAccelTickResref          = "mgs_shift_01";

// Tick interval bounds: slow at 0% throttle, fast at 100% throttle.
// Same 80 ms floor the Pillar 1 wall change-detector uses for closest-
// edge heartbeats — comfortably audible without sample-overlap chatter.
constexpr ULONGLONG kAccelTickIntervalMinMs     = 80;    // bar full
constexpr ULONGLONG kAccelTickIntervalMaxMs     = 600;   // bar empty

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

// Continuous obstacle-proximity cue parameters.
//
//   kObstacleCueRangeM  — only obstacles within this 3D distance from
//                         the listener fire cues. Sized for ~1-1.3 s
//                         of advance warning at the top observed bike
//                         speed (~190 units/s in gear 3 on Taris), so
//                         the user has time to choose a lane before
//                         the obstacle arrives. Earlier 30 m was sized
//                         like the in-world Pillar-1 walls and gave
//                         only ~150 ms warning — confirmed too late by
//                         the user 2026-05-24.
//
//   kObstacleCueRepeatMs— per-obstacle re-fire interval. The engine
//                         play primitive is one-shot, so we re-arm
//                         each obstacle's cue at this cadence to
//                         simulate a continuous tone. 250 ms = 4 Hz,
//                         the same rate the existing Pillar-1 wall
//                         change-detector uses for its closest-edge
//                         heartbeat (audible without being staccato).
//
//   kObstacleCueResref  — engine sample played for each obstacle. The
//                         minigame's own warning sound carries the
//                         right "watch out" timbre and is short
//                         enough (<200 ms) to fit a 250-ms loop with
//                         a small gap so successive cues read as
//                         distinct pulses rather than slurring into
//                         a drone.
//
//   kObstacleForwardMargin — obstacles whose Y (forward axis on the
//                         tunnel track — the bike moves from low Y to
//                         high Y, confirmed live from the 22-obstacle
//                         dump where slot 0 sat at Y=3742 and the bike
//                         reached Y=4031 at race-end) are dropped if
//                         they sit behind the listener by more than
//                         this margin. Small negative margin allows
//                         immediately-adjacent obstacles (mid-pass) to
//                         keep cueing for one tick so the spatial
//                         pan completes its sweep.
constexpr float       kObstacleCueRangeM     = 200.0f;
constexpr float       kObstacleForwardMargin = 10.0f;
constexpr const char* kObstacleCueResref     = "mgs_warnbust";

// ----- Source-position rescaling (engine audibility) -----
//
// The engine's 3D audio attenuation curve is tuned for the 5-20 m
// range Pillar-1 nav cues + footsteps live in; at 100-200 m the source
// is below audible threshold (live-confirmed 2026-05-24,
// patch-20260524-215240.log).
//
// First pass clamped every distant obstacle onto a fixed 8 m sphere,
// which kept them audible but made all obstacles equally loud — the
// volume/pan distance cue was dead and only the cadence ramp carried
// "how close". Revised approach: linear 1:9 compression so loudness
// AND pan rotation both encode distance naturally. A 180 m obstacle
// renders at 20 m (engine edge, just audible), 90 m at 10 m (clearly
// audible), 9 m at 1 m (right on top). At bike speed ~50 m/s the
// compressed source closes at ~5.5 m/s, which the engine's curve
// resolves into an audible swelling approach + pass.
//
// Floor at 1 m to avoid sub-meter / inside-the-head pan singularity
// during the close-pass moment (real <9 m). Direction stays correct;
// the source just stops getting closer once it would otherwise fall
// inside the floor.
constexpr float       kObstacleDistanceCompression = 1.0f / 9.0f;
constexpr float       kObstacleMinSourceDistanceM  = 1.0f;
// SUPERSEDED — fixed-radius sphere kept commented for A/B revert:
// constexpr float       kObstacleSourceRadiusM = 8.0f;

// ----- Fixed re-fire cadence -----
//
// With the 1:9 compression above, loudness + pan rotation already
// encode distance. A steady tick rate gives a recognisable "object
// approaching + passing" rhythm rather than a variable staccato.
// 250 ms = 4 Hz, same heartbeat the Pillar-1 wall change-detector
// uses.
constexpr ULONGLONG   kObstacleCueIntervalMs = 250;
// SUPERSEDED — distance-modulated cadence kept commented for A/B revert:
// constexpr ULONGLONG   kObstacleCueIntervalFarMs  = 700;
// constexpr ULONGLONG   kObstacleCueIntervalNearMs = 90;

// ----- Side-wall collision detection -----
//
// Sighted players see+hear a brief bounce-off animation when the bike
// scrapes the lane wall. Heuristic detection (no engine collision hook
// yet identified): watch CSWMiniPlayer.offset_vector.x (the tunnel-
// frame lane coordinate). When the player has had non-trivial lateral
// motion over the recent EMA window and the per-tick delta then
// collapses to ~0, the lane shift has stalled — the bike is pinned
// against a wall in the direction it was moving.
//
// Side is the sign of the recent SIGNED lateral EMA: positive = was
// moving +X (right wall hit), negative = -X (left wall hit). The cue
// is fired as a 3D one-shot offset by ±kCollisionPanOffsetM in the
// listener's local X so the engine pans hard left or right; the
// camera follows the bike with +Y forward orientation during the
// race, so world +X aligns with listener-local right.
//
// Single-shot per impact: the recent-EMA accumulators reset to zero
// after a fire so a sustained wall-hug doesn't machine-gun the cue.
// kCollisionDebounceMs is a belt-and-braces floor for the same reason.
//
// Threshold units are world-X per OnUpdate tick. The 2026-05-24 1 Hz
// snapshot showed lateral excursions of 0.5..8 units/s at the
// listener; per-tick (~30 Hz) that's 0.02..0.27 units/tick. Movement
// floor 0.10 and stall ceiling 0.02 sit comfortably either side of
// the noise floor without triggering on slow drift.
constexpr float       kCollisionMovingThreshold = 0.10f;  // units/tick
constexpr float       kCollisionStalledThreshold = 0.02f; // units/tick
constexpr float       kCollisionEmaAlpha         = 0.30f;
// Pan offset: 5 m is the lower end of the engine's well-tuned 5-20 m
// curve (audio_bus.h comment). 3 m was inaudible in
// patch-20260524-223840.log — the engine's near-field (<5 m)
// attenuates oddly.
constexpr float       kCollisionPanOffsetM       = 5.0f;
constexpr ULONGLONG   kCollisionDebounceMs       = 500;

// Wall-impact sample. gui_invdrop (NavCue::Collision) was inaudible in
// the 2026-05-24 swoop test — it's a short UI click, easily masked.
// mgs_sith_hit1 is the heavier "ship took a hit" sample from the
// space combat minigame; contextually fits "your bike just thumped a
// wall" and is loud enough to cut through the 4 Hz obstacle ticks.
constexpr const char* kCollisionCueResref        = "mgs_sith_hit1";

// ----- Sustained wall-scrape -----
//
// The initial-hit detector only fires on the "lateral motion stalls"
// transition — it can't re-fire while the bike is pinned against the
// wall (dx = 0 for the entire scrape, so the moving-EMA never re-
// arms). Without a sustained cue the player hears one thump at the
// moment of impact, then has no way to tell they're still grinding
// the wall.
//
// Pinned detection: after an initial hit, store the lane-X at impact.
// Each subsequent tick within kCollisionPinnedReleaseDelta of that X
// is "still pinned" — re-fire the cue every kCollisionScrapeMs. Once
// the bike has moved more than the release delta toward centre we
// consider it released and rearm the initial-hit detector.
constexpr float       kCollisionPinnedReleaseDelta = 3.0f;  // lane units
constexpr ULONGLONG   kCollisionScrapeMs           = 600;   // 1.67 Hz

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

    // Per-slot last-cue timestamp, indexed directly by the global MGO
    // array slot (0..254). Re-armed every kObstacleCueRepeatMs while
    // the obstacle stays in range, so the user hears a steady pulse
    // train per nearby hazard with the engine's 3D listener panning
    // each pulse from the obstacle's actual world position. Cleared
    // on ENTER. Sized to the MGO array, not the obstacle-count, so we
    // can key on the slot without a parallel index map.
    ULONGLONG     obstacle_last_cue_ms[kMgoArraySlotCount] = {};

    // Diagnostic guard for the first-obstacle byte dump (separate from
    // the mini_game one). Lets us identify obstacle subtypes (debris vs
    // accelerator pad, distinguished by vtable) without spamming the
    // log on subsequent races.
    bool          obstacle_diag_emitted   = false;

    // Last-fire timestamp for the acceleration progress tick. See
    // kAccelTickResref / kAccelTickInterval{Min,Max}Ms for the cadence
    // model.
    ULONGLONG     last_accel_tick_ms      = 0;

    // Side-wall collision detector state. See the kCollision* constants
    // above for the stall-detection model. last_player_x_valid is false
    // until we've sampled the first tick post-ENTER so the very first
    // dx isn't a garbage "this-X minus zero" of ~100 units.
    bool          last_player_x_valid     = false;
    float         last_player_x           = 0.0f;
    float         lateral_ema_abs         = 0.0f;
    float         lateral_ema_signed      = 0.0f;
    ULONGLONG     last_collision_ms       = 0;

    // Pin-state for the sustained wall-scrape cue. wall_pinned_side is
    // 0 when clear, +1 when pinned against the right wall, -1 left.
    // wall_pinned_at_x snapshots the lane-X at the moment of impact so
    // we can detect release without hardcoding the per-track lane
    // width (m03mg edge is ±20 but other swoop tracks may differ).
    int           wall_pinned_side        = 0;
    float         wall_pinned_at_x        = 0.0f;
    ULONGLONG     last_wall_scrape_ms     = 0;
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

// ============================================================================
// Acceleration progress tick.
//
// Replaces the visual speed bar for blind play. The cadence of the
// re-fired short cue tracks how full the bar is — slow at the bottom
// of the current gear, very fast at the top. When the user taps shift
// the bar resets visually; here, max_speed widens, so the same `speed`
// value maps to a smaller fraction of the new gear's range, and the
// tick rate naturally drops back down. No extra "shift detected"
// branch needed — the rate change does that.
// ============================================================================

void TickAccelerationCue(void* miniGame) {
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) return;

    float speed    = SafeReadFloat(player, kFollowerSpeedOffset);
    float minSpeed = SafeReadFloat(player, kMiniPlayerMinSpeedOffset);
    float maxSpeed = SafeReadFloat(player, kMiniPlayerMaxSpeedOffset);

    // Race not yet armed (engine hasn't loaded the envelope) — stay
    // silent rather than spamming ticks at a degenerate denominator.
    if (maxSpeed <= 0.0f) return;
    if (maxSpeed <= minSpeed) return;

    float norm = (speed - minSpeed) / (maxSpeed - minSpeed);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    // Linear interpolation: norm=0 → max interval (slow), norm=1 → min
    // interval (fast). Same shape as the engine's gauge bar.
    ULONGLONG interval = static_cast<ULONGLONG>(
        kAccelTickIntervalMaxMs +
        (kAccelTickIntervalMinMs - kAccelTickIntervalMaxMs) *
        static_cast<ULONGLONG>(norm * 1000) / 1000);

    const ULONGLONG now = GetTickCount64();
    if (now - g_state.last_accel_tick_ms < interval) return;
    g_state.last_accel_tick_ms = now;

    // 2D cue (centered, no spatial position) — this is an instrument
    // panel, not a world event. Default priority group.
    acc::audio::PlayCue(kAccelTickResref);
}

// ============================================================================
// Side-wall collision cue.
//
// Stall-detection heuristic — see kCollision* constants for the model.
// Sample player lane X each tick, maintain a short EMA of dx (both
// magnitude and signed). When magnitude EMA crosses the "was moving"
// threshold and the latest dx falls below the "now stopped" threshold,
// the bike has just pinned against a wall in the direction of the
// signed EMA. Fire the standard Collision cue offset to that side of
// the listener so the engine pans hard left/right.
// ============================================================================

// Fire a side-panned wall cue. Returns true on engine accept. side > 0
// pans right (+X from listener), side < 0 pans left. Skips silently if
// neither camera nor player position is resolvable.
bool FireWallCue(int side, const char* label, float laneX) {
    Vector listener_pos;
    if (!acc::engine::GetCameraPosition(listener_pos) &&
        !acc::engine::GetPlayerPosition(listener_pos)) {
        acclog::Write("SwoopRace",
                      "wall %s: no listener anchor, skip pan", label);
        return false;
    }

    // Camera is chase-cam behind the bike along +Y, so world +X aligns
    // with listener-local "right" during the race. Offset the cue
    // source by ±kCollisionPanOffsetM on world X for a hard pan.
    Vector cue_pos = listener_pos;
    cue_pos.x += (side > 0) ? +kCollisionPanOffsetM : -kCollisionPanOffsetM;

    const bool fired = acc::audio::PlayCue3D(kCollisionCueResref, cue_pos);

    acclog::Write("SwoopRace",
                  "wall %s side=%s laneX=%.2f listener=(%.1f,%.1f,%.1f) "
                  "cuePos=(%.1f,%.1f,%.1f) resref=%s fired=%d",
                  label, (side > 0) ? "right" : "left", laneX,
                  listener_pos.x, listener_pos.y, listener_pos.z,
                  cue_pos.x, cue_pos.y, cue_pos.z,
                  kCollisionCueResref, fired ? 1 : 0);
    return fired;
}

void TickWallCollision(void* miniGame) {
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) return;

    Vector offset;
    if (!SafeReadVector(player, kMiniPlayerOffsetVectorOffset, offset)) {
        return;
    }

    // First sample post-ENTER establishes the baseline. The delta on
    // the very first tick would otherwise be "current_X minus 0" which
    // is hundreds of units of garbage motion.
    if (!g_state.last_player_x_valid) {
        g_state.last_player_x       = offset.x;
        g_state.last_player_x_valid = true;
        return;
    }

    const float dx = offset.x - g_state.last_player_x;
    g_state.last_player_x = offset.x;

    const ULONGLONG now = GetTickCount64();

    // --- Sustained-scrape branch: bike already pinned at a wall. ---
    if (g_state.wall_pinned_side != 0) {
        const float drift = std::fabs(offset.x - g_state.wall_pinned_at_x);
        if (drift > kCollisionPinnedReleaseDelta) {
            // Player has steered away from the wall. Clear the pin so
            // the initial-hit detector can re-arm for the next impact.
            acclog::Write("SwoopRace",
                          "wall released side=%s laneX=%.2f (was %.2f, drift %.2f)",
                          g_state.wall_pinned_side > 0 ? "right" : "left",
                          offset.x, g_state.wall_pinned_at_x, drift);
            g_state.wall_pinned_side = 0;
            // Fall through so the same tick can update EMAs etc.
        } else {
            // Still pinned. Re-fire the cue at scrape cadence so the
            // user has a continuous "I'm grinding the wall" signal.
            if (now - g_state.last_wall_scrape_ms >= kCollisionScrapeMs) {
                g_state.last_wall_scrape_ms = now;
                FireWallCue(g_state.wall_pinned_side, "scrape", offset.x);
            }
            // Skip EMA-based initial-hit detection while pinned; dx is
            // 0 here anyway and the EMAs would only decay further.
            return;
        }
    }

    // --- Initial-hit branch: detect the "was moving, now stalled" edge. ---
    g_state.lateral_ema_abs =
        (1.0f - kCollisionEmaAlpha) * g_state.lateral_ema_abs +
        kCollisionEmaAlpha * std::fabs(dx);
    g_state.lateral_ema_signed =
        (1.0f - kCollisionEmaAlpha) * g_state.lateral_ema_signed +
        kCollisionEmaAlpha * dx;

    if (g_state.lateral_ema_abs <= kCollisionMovingThreshold) return;
    if (std::fabs(dx) >= kCollisionStalledThreshold)            return;
    if (now - g_state.last_collision_ms < kCollisionDebounceMs) return;
    g_state.last_collision_ms = now;

    const int side = (g_state.lateral_ema_signed > 0.0f) ? +1 : -1;
    FireWallCue(side, "impact", offset.x);

    // Enter pinned state — scrape branch above takes over next tick
    // until the player steers away from the wall edge.
    g_state.wall_pinned_side    = side;
    g_state.wall_pinned_at_x    = offset.x;
    g_state.last_wall_scrape_ms = now;

    // Reset the EMAs so we don't immediately re-fire the impact on the
    // next tick (would happen if dx stays at 0 — though the pin branch
    // would intercept that anyway, this keeps the gates clean).
    g_state.lateral_ema_abs    = 0.0f;
    g_state.lateral_ema_signed = 0.0f;
}

// ============================================================================
// Continuous obstacle proximity cues.
//
// Iterates CSWMiniGame.obstacles_ptr[] each tick, computes 3D distance
// from the player creature to each obstacle's CAurObject world
// position, and re-fires kObstacleCueResref through audio_bus::PlayCue3D
// at the obstacle's position every kObstacleCueRepeatMs while the
// obstacle is within kObstacleCueRangeM. The engine's 3D listener
// (camera, which orbits the bike during the race) pans and attenuates
// each fire, so as the player approaches the obstacle the cue
// naturally swells and rotates from front-left/right toward dead
// ahead — the same model a sighted player gets from the obstacle's
// visual approach silhouette.
//
// First-pass coverage: all obstacles in obstacles_ptr[] are cued with
// the same warning sample. Accelerator pads are NOT distinguished yet
// — the engine's CSWMGObstacle struct doesn't expose a subtype field
// inline, so the next pass will either compare the obstacle's vtable
// against the two subtypes observed in the first-fire dump, or peek
// the script-slot resref ("accelpad" vs "obstacle") to route them to
// mgs_accelpad vs mgs_warnbust.
// ============================================================================

// Resolve the global CSWMiniGameObjectArray via:
//   *0x7a39fc (AppManager) +0x4
//     -> CClientExoApp +0x4
//       -> CClientExoAppInternal +0x0  (= the array itself)
void* ResolveMgoArray() {
    __try {
        void* appManager = *reinterpret_cast<void**>(
            kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* clientApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!clientApp) return nullptr;
        void* clientInternal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientApp) +
            kClientExoAppInternalOffset);
        if (!clientInternal) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientInternal) +
            kClientInternalMgoArrayOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Call an MGO object's vtable[slotOffset] AsXxx() thiscall. Returns
// the call's return value (which is `this` for the matching subclass
// or null otherwise). All __thiscall convention — ECX gets `this`, no
// stack args.
typedef void* (__thiscall* PFN_AsCast)(void* this_);

// CAurObject vtable[+0xc] — returns the obstacle's name string (lives
// in the underlying Gob/Model — same accessor sighted UI uses via
// CSWMGObstacle::GetName).
typedef const char* (__thiscall* PFN_GetAurName)(void* this_);

// Returns the CAurObject's name pointer (engine-owned, valid for the
// lifetime of the object), or nullptr on any null link / SEH fault.
const char* ReadAurObjectName(void* aurObject) {
    if (!aurObject) return nullptr;
    __try {
        void* vtable = *reinterpret_cast<void**>(aurObject);
        if (!vtable) return nullptr;
        void* fn = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vtable) +
            kAurVtableSlotGetName);
        if (!fn) return nullptr;
        return reinterpret_cast<PFN_GetAurName>(fn)(aurObject);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Lowercase a single ASCII char without locale.
char AsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// Case-insensitive substring search. Returns true if `needle` appears
// anywhere in `haystack`. Both must be NUL-terminated. Used for
// matching obstacle names against the booster-pattern table.
bool ContainsCi(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return false;
    for (const char* h = haystack; *h; ++h) {
        const char* hp = h;
        const char* np = needle;
        while (*hp && *np && AsciiLower(*hp) == AsciiLower(*np)) {
            ++hp; ++np;
        }
        if (!*np) return true;
    }
    return false;
}

// Pick the cue resref for an obstacle by matching its name against
// the booster-pattern table. Falls back to the warning cue for
// anything that doesn't match (rocks, debris, unknown obstacles).
const char* CueResrefForObstacle(const char* name) {
    if (!name || !*name) return kObstacleCueResref;
    for (size_t i = 0; i < sizeof(kBoosterNamePatterns) /
                              sizeof(kBoosterNamePatterns[0]); ++i) {
        if (ContainsCi(name, kBoosterNamePatterns[i])) {
            return kBoosterCueResref;
        }
    }
    return kObstacleCueResref;
}

void* CallAsCast(void* obj, size_t vtableSlotOffset) {
    if (!obj) return nullptr;
    __try {
        void* vtable = *reinterpret_cast<void**>(obj);
        if (!vtable) return nullptr;
        void* fn = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vtable) + vtableSlotOffset);
        if (!fn) return nullptr;
        auto castFn = reinterpret_cast<PFN_AsCast>(fn);
        return castFn(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void TickObstacleCues(void* /*miniGame*/) {
    void* mgoArray = ResolveMgoArray();
    if (!mgoArray) {
        acclog::Trace("SwoopRace", "obstacle cues: mgo array unresolved");
        return;
    }

    // Listener anchor. During the race the engine swaps the controlled
    // creature (PartyLeader log goes handle=0xffffffff), so
    // GetPlayerPosition is unreliable. Camera follows the bike, so
    // it's the correct listener.
    Vector listener_pos;
    if (!acc::engine::GetCameraPosition(listener_pos) &&
        !acc::engine::GetPlayerPosition(listener_pos)) {
        acclog::Trace("SwoopRace",
                      "obstacle cues: no listener anchor available");
        return;
    }

    const ULONGLONG now = GetTickCount64();
    int slots_seen = 0;
    int obstacles_found = 0;
    int obstacles_ahead = 0;
    int obstacles_in_range = 0;
    int cued_this_tick = 0;

    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        // mgoArray->objects[i]
        void* obj = SafeReadPtr(mgoArray,
                                kMgoArrayObjectsOffset +
                                static_cast<size_t>(i) * sizeof(void*));
        if (!obj) continue;
        ++slots_seen;

        // AsObstacle vtable[8] returns the same pointer if `obj` is a
        // CSWMGObstacle, null otherwise. That's the engine's own
        // type-check pattern.
        void* obstacle = CallAsCast(obj, kVtableSlotAsObstacle);
        if (!obstacle) continue;
        ++obstacles_found;

        // Position via CSWMGObstacle +0x60 -> CAurObject +0x78.
        void* aur = SafeReadPtr(obstacle, kObstacleAurObjectOffset);
        if (!aur) continue;
        Vector pos;
        if (!SafeReadVector(aur, kAurObjectPositionOffset, pos)) continue;

        // Read the obstacle's name (engine's own GetName accessor path,
        // via the CAurObject vtable). Used both for first-fire
        // diagnostics and for runtime cue routing (booster vs rock).
        const char* name = ReadAurObjectName(aur);

        // First-fire diagnostic: log EVERY obstacle's slot + name so we
        // can see the full per-track inventory in one pass. For the
        // first three obstacles ALSO dump the full struct bytes
        // (0xb4 = CSWMGObstacle SIZE per SARIF DATATYPE) so we can
        // compare them byte-by-byte and find the per-instance subtype
        // field (rock vs accelerator pad — the names are sequential
        // m03mg_MGO01..MGO22 and provide no semantic discrimination).
        if (!g_state.obstacle_diag_emitted) {
            void* vt = SafeReadPtr(obstacle, 0);
            acclog::Write("SwoopRace",
                          "obstacle slot=%d ptr=%p vtable=%p name=[%s] "
                          "pos=(%.2f,%.2f,%.2f) route=%s",
                          i, obstacle, vt, name ? name : "(null)",
                          pos.x, pos.y, pos.z,
                          CueResrefForObstacle(name));
            // Dump full struct + leading CAurObject bytes for the
            // first three obstacles so we can spot the subtype field
            // by diff. Three samples is enough to distinguish "all
            // obstacles look identical except for position" from "one
            // field varies per type".
            if (i < 3) {
                __try {
                    acclog::WriteHex("SwoopRace",
                                     "  obstacle full struct (0xb4)",
                                     obstacle, 0xb4);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    acclog::Write("SwoopRace",
                                  "  obstacle full dump faulted");
                }
                __try {
                    acclog::WriteHex("SwoopRace",
                                     "  obstacle.aurObject leading bytes (0x80)",
                                     aur, 0x80);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    acclog::Write("SwoopRace",
                                  "  obstacle.aurObject dump faulted");
                }
            }
        }

        // Forward-only filter: only cue obstacles ahead of the
        // listener (or just barely passed, inside the margin). Track Y
        // is the forward axis (low → high), confirmed live by the
        // listener heartbeat reaching Y≈4000 at race-end while the
        // farthest obstacle sits at Y=3742. The vehicle moves only
        // along this axis; obstacles behind us can't be hit and would
        // just clutter the soundstage.
        if (pos.y < listener_pos.y - kObstacleForwardMargin) continue;
        ++obstacles_ahead;

        // 3D distance from listener.
        float dx = pos.x - listener_pos.x;
        float dy = pos.y - listener_pos.y;
        float dz = pos.z - listener_pos.z;
        float distSq = dx*dx + dy*dy + dz*dz;
        const float rangeSq = kObstacleCueRangeM * kObstacleCueRangeM;
        if (distSq > rangeSq) continue;
        ++obstacles_in_range;

        // Fixed per-slot throttle. Spatial pan + volume (post-1:9
        // compression below) carry the distance cue; the tick rate
        // stays steady so the user hears a recognisable "approach +
        // pass" rhythm.
        const float dist = (distSq > 0.0f) ? std::sqrt(distSq) : 0.0f;
        const ULONGLONG interval = kObstacleCueIntervalMs;
        // SUPERSEDED — distance-modulated interval kept commented for A/B:
        // float t = dist / kObstacleCueRangeM;       // 0 = on top, 1 = max range
        // if (t < 0.0f) t = 0.0f;
        // if (t > 1.0f) t = 1.0f;
        // const ULONGLONG interval = static_cast<ULONGLONG>(
        //     kObstacleCueIntervalNearMs +
        //     t * (static_cast<float>(kObstacleCueIntervalFarMs -
        //                             kObstacleCueIntervalNearMs)));

        ULONGLONG& last_ms = g_state.obstacle_last_cue_ms[i];
        if (now - last_ms < interval) continue;
        last_ms = now;

        // Route per-obstacle: matched names get the booster cue,
        // everything else gets the warning cue. Resref string is
        // chosen per-fire so a single moving cluster of rocks +
        // boosters near the bike fans out into the two distinct
        // sounds the player needs to hear apart.
        const char* resref = CueResrefForObstacle(name);

        // Project source proportionally so distance encodes via volume
        // + pan rotation rather than a flat sphere. 1:9 compression
        // with 1 m floor — close-pass lands at 1 m (correct direction,
        // no sub-meter pan flip), farthest in-range obstacle (~180 m
        // real) lands at 20 m (engine audibility edge).
        Vector cue_pos = pos;
        if (dist > 0.0f) {
            float compressed = dist * kObstacleDistanceCompression;
            if (compressed < kObstacleMinSourceDistanceM) {
                compressed = kObstacleMinSourceDistanceM;
            }
            const float k = compressed / dist;
            cue_pos.x = listener_pos.x + dx * k;
            cue_pos.y = listener_pos.y + dy * k;
            cue_pos.z = listener_pos.z + dz * k;
        }

        // Per-fire log: actual dist, the interval we picked, and the
        // projected cue position so we can verify the audible-sphere
        // math after the fact.
        acclog::Trace("SwoopRace",
                      "fire slot=%d name=[%s] obstaclePos=(%.1f,%.1f,%.1f) "
                      "listener=(%.1f,%.1f,%.1f) dist=%.1f cuePos=(%.1f,%.1f,%.1f) "
                      "interval=%llu res=%s",
                      i, name ? name : "(null)",
                      pos.x, pos.y, pos.z,
                      listener_pos.x, listener_pos.y, listener_pos.z,
                      dist,
                      cue_pos.x, cue_pos.y, cue_pos.z,
                      interval, resref);

        acc::audio::PlayCue3D(resref, cue_pos);
        ++cued_this_tick;
    }

    // Mark diagnostic done AFTER the full sweep, so we get all 22
    // obstacles' names in the log on the first iteration (not just
    // the first one).
    if (!g_state.obstacle_diag_emitted && obstacles_found > 0) {
        g_state.obstacle_diag_emitted = true;
    }

    // Single rolling-state line — strictly more informative than the
    // previous two-branch logging (the false "0 in range" was a
    // hardcoded format-string typo; now the actual counts are tracked
    // and reported every tick).
    acclog::Trace("SwoopRace",
                  "scan: slots=%d obstacles=%d ahead=%d inRange=%d (%.0fm) "
                  "cued=%d listenerY=%.1f",
                  slots_seen, obstacles_found, obstacles_ahead,
                  obstacles_in_range, kObstacleCueRangeM,
                  cued_this_tick, listener_pos.y);
}

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
    g_state.obstacle_diag_emitted = false;
    g_state.last_accel_tick_ms  = 0;
    g_state.last_player_x_valid = false;
    g_state.last_player_x       = 0.0f;
    g_state.lateral_ema_abs     = 0.0f;
    g_state.lateral_ema_signed  = 0.0f;
    g_state.last_collision_ms   = 0;
    g_state.wall_pinned_side    = 0;
    g_state.wall_pinned_at_x    = 0.0f;
    g_state.last_wall_scrape_ms = 0;
    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        g_state.obstacle_last_cue_ms[i] = 0;
    }

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
        // Idle state. Wait for a non-null read to fire ENTER.
        if (mgArea) HandleEnter(mgArea);
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
    TickAccelerationCue(g_state.latched_mini_game);
    TickWallCollision(g_state.latched_mini_game);
    TickObstacleCues(g_state.latched_mini_game);
    EmitDiagSnapshot(g_state.latched_mini_game);
}

}  // namespace acc::swoop_race
