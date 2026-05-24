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
//                         the player creature fire cues. Far obstacles
//                         (the track is several km long total) stay
//                         silent so the user hears the immediate
//                         hazard zone instead of a constant chorus.
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
constexpr float       kObstacleCueRangeM   = 30.0f;
constexpr ULONGLONG   kObstacleCueRepeatMs = 250;
constexpr const char* kObstacleCueResref   = "mgs_warnbust";

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

        // First-fire diagnostic: capture the first real obstacle's
        // header so we can read its subtype (debris vs accelerator
        // pad) in the follow-up pass.
        if (!g_state.obstacle_diag_emitted) {
            g_state.obstacle_diag_emitted = true;
            void* vt = SafeReadPtr(obstacle, 0);
            acclog::Write("SwoopRace",
                          "obstacle[slot=%d] sample: ptr=%p vtable=%p",
                          i, obstacle, vt);
            __try {
                acclog::WriteHex("SwoopRace", "obstacle bytes (0x80)",
                                 obstacle, 0x80);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                acclog::Write("SwoopRace",
                              "obstacle bytes dump faulted at %p", obstacle);
            }
        }

        // Position via CSWMGObstacle +0x60 -> CAurObject +0x78.
        void* aur = SafeReadPtr(obstacle, kObstacleAurObjectOffset);
        if (!aur) continue;
        Vector pos;
        if (!SafeReadVector(aur, kAurObjectPositionOffset, pos)) continue;

        // 3D distance from listener.
        float dx = pos.x - listener_pos.x;
        float dy = pos.y - listener_pos.y;
        float dz = pos.z - listener_pos.z;
        float distSq = dx*dx + dy*dy + dz*dz;
        const float rangeSq = kObstacleCueRangeM * kObstacleCueRangeM;
        if (distSq > rangeSq) continue;

        // Per-slot re-fire gate.
        ULONGLONG& last_ms = g_state.obstacle_last_cue_ms[i];
        if (now - last_ms < kObstacleCueRepeatMs) continue;
        last_ms = now;

        acc::audio::PlayCue3D(kObstacleCueResref, pos);
        ++cued_this_tick;
    }

    if (cued_this_tick > 0) {
        acclog::Trace("SwoopRace",
                      "obstacle cues fired=%d (of %d obstacles in %d slots)",
                      cued_this_tick, obstacles_found, slots_seen);
    } else {
        // Heartbeat trace so the log shows whether we're finding
        // obstacles at all. Dedup collapses the steady-state.
        acclog::Trace("SwoopRace",
                      "obstacle scan: %d obstacles among %d non-null slots, "
                      "0 in range %.1fm",
                      obstacles_found, slots_seen, kObstacleCueRangeM);
    }
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
    TickObstacleCues(g_state.latched_mini_game);
    EmitDiagSnapshot(g_state.latched_mini_game);
}

}  // namespace acc::swoop_race
