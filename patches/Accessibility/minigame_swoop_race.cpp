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

#include "minigame_aim.h"
#include "minigame_swoop_race.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_app.h"       // GetServerApp (real race-timer read — see
                              //   TickRaceTimer)
#include "engine_area.h"      // GetClientArea + map-pin chain (back-pointer),
                              //   ReadGlobalNumber (MIN_RACE_GEAR / MIN_TIME_*)
#include "engine_game.h"      // IsKotor2 — gate ladder, jump watch, K2 keys
#include "engine_offsets.h"   // Vector
#include "engine_player.h"    // GetCameraPosition (jump-start log anchor)
#include "hotkeys.h"          // Pressed(SwoopShift) — keyboard shift (K2)
#include "log.h"
#include "audio_bus.h"        // PlayCue — non-positional "you can shift now" cue
#include "audio_cues.h"       // NavCue + GetNavCueResref (shift-ready resref)
#include "prism.h"            // SpeakUrgent — entry/exit/gear must beat
                              //              NVDA's typed-character cancel
                              //              (race input is held Space/Enter)
#include "strings.h"          // Get(SwoopRaceStarted/Controls/Ended) +
                              //     FmtSwoopRaceGear
#include "minigame_swoop_audio.h"  // TickSpatialAudio + ResetSpatialAudio
#include "engine_rebase.h"
#include "engine_offsets_select.h"

namespace acc::swoop_race {

namespace {

// SEH read primitives + minigame-object-array resolution are shared across
// the minigame TUs (see minigame_aim.h). Brought into unqualified scope so
// the dense reads below read as they did when each file had its own copy.
using acc::minigame::SafeReadPtr;
using acc::minigame::SafeReadU32;
using acc::minigame::SafeReadFloat;
using acc::minigame::SafeReadVector;
using acc::minigame::ResolveMgoArray;
using acc::minigame::CallAsCast;

// ============================================================================
// Engine struct offsets (from docs/llm-docs/re/swkotor.exe.h).
// ============================================================================
//
// CSWCArea.mini_game (verified line 8726, after rooms@+0x260):
//
// KOTOR 2 = +0x268. Witnessed as a WRITE, not inferred: the K2 area's
// minigame-load path @0x007A2FB0 allocates 0x11c bytes, runs the CSWMiniGame
// ctor @0x00839260 on it, and stores the result with `mov [ecx+0x268], edx`;
// the reader at 0x007AA940 loads the same slot. The +4 shift matches the map-pin
// array's (kClientAreaMapPinsOffset, 0x1c4 -> 0x1c8) — CSWCArea gained one dword
// low down and everything above moved together. Same delta again at the tail of
// the CSWMiniGame ctor, which takes the area's resref from area+0x110 on K2
// where KOTOR 1 takes it from +0x10c.
const size_t kClientAreaMiniGameOffset      = acc::off::Pick(0x264, 0x268);

// CSWMiniGame (line 9309). type/counts confirmed by name; obstacle data
// pointer at +0x44 + count at +0x48 confirmed from the patch-
// 20260524-163552.log first-fire byte dump (obstacles_ptr=0x1721AA08,
// count=22, the resref "m03mg" sits at the +0x0c CResRef as expected).
//
// KOTOR 2: CSWMiniGame is UNCHANGED through +0xec and merely grows a tail
// (0xf0 -> 0x11c: a unit Vector, the swoopupgrade.2da handle, and six per-level
// upgrade rows — K2's swoop-bike upgrades). Every field below is therefore
// Same(), each with its own witness in the K2 twins:
//   * ctor @0x00839260 — area at +0x28 (its only argument), player +0x24 nulled,
//     near/far clip 0.1f/100.0f at +0x68/+0x6c, view angle 65.0f at +0x70, and
//     five CExoArrayList triples constructed at +0x2c / +0x38 / +0x44 / +0x50 /
//     +0x5c, exactly the five KOTOR 1 zeroes inline (so the enemy id list stays
//     at +0x2c with its count at +0x30, and the obstacle list at +0x44 / +0x48);
//   * Load @0x0083db30 — branches on `[this+0x80] == 1` (swoop) vs `== 2`
//     (turret) to pick the MovementPerSec default, writes UseInertia/DoBumping
//     at +0x94/+0x95 and negates AxisX/AxisY at +0x84/+0x88;
//   * the resref lives in the CResHelper base at +0xc on both, and the streaming
//     sound source at +0x1c.
const size_t kMiniGameVtableOffset          = acc::off::Same(0x00);  // pointer
const size_t kMiniGameResrefOffset          = acc::off::Same(0x0c);  // CResRef[16]
const size_t kMiniGamePlayerOffset          = acc::off::Same(0x24);
const size_t kMiniGameEnemyCountOffset      = acc::off::Same(0x30);
const size_t kMiniGameObstacleDataOffset    = acc::off::Same(0x44);  // CSWMiniGameObject** (verified live)
const size_t kMiniGameObstacleCountOffset   = acc::off::Same(0x48);
const size_t kMiniGameTypeOffset            = acc::off::Same(0x80);  // 1=swoop, 2=turret
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
//
// KOTOR 2: unmoved. `speed` sits BELOW the script-CResRef array that grew from
// ten entries to thirteen (see minigame_aim.h), so it is one of the fields the
// +0x30 shift does not reach — its ctor twin @0x00833ba0 writes the same 100.0f
// default to dword index 0x26 = +0x98.
const size_t kFollowerSpeedOffset           = acc::off::Same(0x98);
// hp / max_hp sit just below speed (swkotor.exe.h: looping +0x80,
// sphere_radius +0x84, +0x88, hp +0x8c, max_hp +0x90, bump_damage +0x94,
// speed +0x98; SetHitPoints @0x0066c900 / SetMaxHitPoints @0x0066c950 write
// them). Below the script array on KOTOR 2 too — the first K2 player dump
// (patch-20260914-143533) shows 600 / 600 there, the .are's Hit_Points /
// Max_HPs for the Citadel bike.
const size_t kFollowerHpOffset              = acc::off::Same(0x8c);
const size_t kFollowerMaxHpOffset           = acc::off::Same(0x90);

// CSWMiniPlayer (line 15382, extends CSWTrackFollower size 0x1a4).
// Tunnel-frame offset (X=lane, Y=forward, Z=vertical) and the speed
// envelope.
//
// KOTOR 2 adds 0x30 to all three (follower base 0x1a4 -> 0x1d4 — K2's
// CSWMiniEnemy allocates exactly 0x1d4). min/max speed are direct witnesses:
// SetMinSpeed @0x00838240 writes +0x208, SetMaxSpeed @0x00838290 writes +0x20c,
// both reached from CSWMiniPlayer::Load's "Minimum_Speed" / "Maximum_Speed"
// reads. Full reasoning at kMiniPlayerOffsetVectorOffset in minigame_aim.h.
const size_t kMiniPlayerOffsetVectorOffset  = acc::off::Pick(0x1c4, 0x1f4);
const size_t kMiniPlayerMinSpeedOffset      = acc::off::Pick(0x1d8, 0x208);
const size_t kMiniPlayerMaxSpeedOffset      = acc::off::Pick(0x1dc, 0x20c);

// KOTOR 2 ONLY — the swoop bike's vertical (jump) velocity. It lives in the
// CSWMiniGame tail that KOTOR 1 does not have (K1's struct ends at 0xf0), not
// in the player. Witnessed in K2's CSWMiniPlayer::Control twin @0x00837960:
//   if (mini_game->type == 1) {            // swoop only
//       v = mini_game->+0x118;
//       offset_delta.z += v * dt;           // integrates into offset.z (+0x1fc)
//       mini_game->+0x118 = v - dt * 45.8f; // gravity, units/s²
//   }
// followed by KeepInTunnel, whose +Z bound the track scripts set to 10 (the
// onaccelerate gear-0 branch: SWMG_SetPlayerTunnelPos z=10). The only script
// writer is onjump.ncs — bound to the follower's OnBrake slot on all three K2
// tracks — which calls SWMG_SetJumpSpeed(30) unless the bike is already above
// z=2. 30 u/s against 45.8 u/s² gives a 1.3 s hop peaking at ~9.8 units, i.e.
// exactly under the tunnel ceiling. Read only on KOTOR 2 (Kotor2Only poisons
// the K1 side, and the consumers are IsKotor2()-gated as well).
const size_t kMiniGameJumpVelocityOffset    = acc::off::Kotor2Only(0x118);

// KOTOR 2 ONLY — the two per-slot script runners the client input handler
// (CClientExoAppInternal::HandleInputEvent twin @0x007b12c0) calls on the
// player bike. 173-byte twins of KOTOR 1's CSWMiniGameObject::OnHitFollower
// @0x0066c2d0: `if (param == 0) { GetScriptName(slot) -> valid? -> RunScript
// (name, mgo_index, 1) }`, with the slot index a literal:
//   @0x00838e10 -> slot 10 = OnAccelerate  (the gear ladder, onaccelerate.ncs)
//   @0x00838ec0 -> slot 11 = OnBrake       (the jump, onjump.ncs)
// The engine reaches them from event ids 0x16 (pad B) / 0x19 (left mouse
// button) for OnAccelerate and 0x1a (right mouse button) / 0xd9 (the MGshoot
// keymap row = Space, pad A) for OnBrake — events registered by K2's
// SetEventDescriptions twin @0x00785df0 (0x19 <- InputIndex 0 MOUSE_BUTTON0,
// 0x1a <- InputIndex 1 MOUSE_BUTTON1, class 1). KOTOR 1 sends 0x19 and 0xd9
// to CSWMiniPlayer::Fire instead, whose gun bank runs onfire.ncs — so Space
// shifts there and no keyboard key shifts on KOTOR 2 by default. The mod
// therefore re-routes: Space / pad A -> TryShift, W / pad B -> TryJump.
// __thiscall with one int (memory: the typedef must carry the int, or the
// callee reads caller-frame garbage).
const uintptr_t kAddrK2RunOnAccelerateScript = acc::addr::Kotor2Only(0x00838e10);
const uintptr_t kAddrK2RunOnBrakeScript      = acc::addr::Kotor2Only(0x00838ec0);
typedef void (__thiscall* PFN_RunFollowerSlotScript)(void* follower, int flag);

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
constexpr float kShiftGateSpeedK1[]       = { 0.0f, 60.0f, 100.0f, 150.0f,
                                              210.0f, 0.0f };
// KOTOR 2 uses the SAME ladder. A stack accounting of its onaccelerate.ncs
// (2026-09-14) read the gates one notch lower (35 / 60 / 100 / 150) and two
// rounds ran on that; the logs refuted it (rounds 4-5, patch-20260915-075412 /
// -083506): every shift the engine accepted landed at 70 / 105-110 / 155 /
// 214, and presses at 35-45 (gear 1), 76-86 (gear 2), 115-150 (gear 3),
// 158-200 (gear 4) ran the script with no gear change. The gate out of gear
// N is the NEXT gear's nominal minimum — 60 / 100 / 150 / 210 — i.e. the top
// 5-10 units of each gear's envelope (gear 1 [35f..70], gear 2 [60f..110],
// gear 3 [100f..155], gear 4 [150f..215]). The wrong table made the cue fire
// up to 40 units early, the player pressed into a no-op and then heard the
// next gear's cue right after the engine's own shift sound.
constexpr float kShiftGateSpeedK2[]       = { 0.0f, 60.0f, 100.0f, 150.0f,
                                              210.0f, 0.0f };
constexpr int   kFirstGatedGear           = 1;
constexpr int   kLastGatedGear            = 4;
constexpr ULONGLONG kShiftReadyHoldMs     = 800;   // see TickShiftReady

inline float ShiftGateSpeed(int gear) {
    return acc::game::IsKotor2() ? kShiftGateSpeedK2[gear]
                                 : kShiftGateSpeedK1[gear];
}

// ----- Gear source: the tracks' own MIN_RACE_GEAR global -----
//
// Both games' swoop scripts keep the gear in the global-number table under
// MIN_RACE_GEAR (K1 heartbeat/onfire, K2 heartbeat/onaccelerate/enemyhitfollower,
// all disassembled): oncreate stamps -5, the countdown walks -4/-3/-2/-1, "Go"
// writes 0, every upshift writes 1..5, the finish writes 6 — and on KOTOR 2 an
// obstacle hit writes 0 again ("gear0" animation, engine idle), so the K2 gear
// can go DOWN mid-race. The max_speed-plateau heuristic below cannot see that
// (it only counts upward jumps, so a re-launch after a hit would read as gear 6,
// 7, ...), which is why the global is now the primary source on both games and
// the heuristic is the fallback for a modded track that does not maintain it.
// Values outside 0..5 are "no gear information" (countdown, finish, or a failed
// read — ReadGlobalNumber returns -1 on a fault, which lands in the same bucket
// as the -1 countdown step, deliberately).
constexpr int kGearGlobalMin = 0;
constexpr int kGearGlobalMax = 5;
constexpr const char* kGearGlobalName = "MIN_RACE_GEAR";

// Reuse the turret minigame's "entered killable range" cue — same meaning to
// the player (a window just opened), already mixed and volume-grouped. See
// turret_game.cpp kRangeCueResref. Resref centralised in audio_cues.h so the
// audio glossary auditions the same sample.
// Resolved per game at fire time: KOTOR 2's archives lack the K1 sample (the
// whole first K2 round had a silent shift cue) — see GetNavCueResrefForGame.
inline const char* ShiftReadyCueResref() {
    return acc::audio::GetNavCueResrefForGame(acc::audio::NavCue::SwoopShiftReady);
}

// Exit-debounce. After we lose the latched pointer, hold off announcing
// EXIT until the loss persists for this many consecutive ticks. The
// race-start transition flips the area chain in < 5 ticks, so 60 ticks
// (~2 s at 30 fps) is comfortably past that without making true exits
// feel laggy.
constexpr int   kExitDebounceTicks        = 60;
// The finish-line time is spoken this long after the finish so the engine's
// race-end sound has faded under it ("the end race sound is louder than the
// TTS", round 16). Inside the exit debounce (60 ticks ≈ 2 s); the exit path
// flushes it regardless. A wreck is spoken immediately.
constexpr ULONGLONG kRaceEndSpeakDelayMs  = 1200;

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
    // True once MIN_RACE_GEAR has read a valid gear (0..5) this race. From
    // then on the global is the gear source and the plateau heuristic is
    // ignored, so the two never double-announce the same shift.
    bool          gear_from_global        = false;

    // "You can shift now" cue latch. True once we've fired the cue for the
    // CURRENT gear; re-armed by TickGearWatch on every detected shift, so the
    // cue fires at most once per gear. See ShiftGateSpeed / TickShiftReady.
    bool          shift_ready_announced   = false;
    ULONGLONG     last_gear_change_ms     = 0;   // see kShiftReadyHoldMs

    // ---- Hit points (CSWTrackFollower +0x8c / +0x90). K2's hit scripts take
    //      50 (obstacle) / 100 (mine) of the bike's 600 and the heartbeat ends
    //      the race at 0 — round 3 reached that at 72 s and the mod announced
    //      a finish time. Drops are logged only (the HUD has a damage gauge;
    //      per-hit speech was tried in round 4 and cut as noise); `wrecked`
    //      is decided when the race ends, see DecideWreckAtFinish.
    int           last_hp                 = -1;
    bool          wrecked                 = false;

    // ---- Jump state (KOTOR 2 only; K1 clamps offset.z to 0 so it never
    //      arms). Edge-detected on the tunnel offset's Z so a jump start and
    //      its landing are logged with the bike's speed, gear and world
    //      position — the data the vertical-obstacle cue will be tuned from.
    bool          airborne                = false;
    ULONGLONG     jump_started_ms         = 0;
    float         jump_peak_z             = 0.0f;

    // ---- Stuck detector (see TickStuckHint). Forward progress is measured
    //      on the camera's track-axis coordinate; the mark moves whenever the
    //      bike has gained kStuckProgressU since it was set.
    bool          have_progress_mark      = false;
    float         progress_mark_y         = 0.0f;
    ULONGLONG     progress_mark_ms        = 0;
    bool          stuck_hint_spoken       = false;
    ParkedBar     stuck_bar_spoken_for    = ParkedBar::None;   // re-arms when the situation changes

    // ---- Delayed race-end time (see kRaceEndSpeakDelayMs): the finish sound
    //      drowns speech spoken on the same frame, so the time waits.
    bool          race_end_pending        = false;
    ULONGLONG     race_end_due_ms         = 0;
    char          race_end_msg[160]       = {};
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
//
// KOTOR 2 uses the SAME script scheme — its three swoop tracks (211TEL, 371NAR,
// 510OND, the Type==1 minigame areas) stamp MIN_TIME_HOUR/MIN/SEC/MIL from their
// own heartbeat.ncs, exactly as KOTOR 1's do — so this whole reader ports, with
// only the two engine addresses changing:
//   * GetWorldTimer @0x0051C370 — `[this+4]` then `[internal+0x10048]`, the same
//     internal slot as KOTOR 1; cross-checked against the two call sites that
//     inline it (0x0051D790 / 0x0051E170) and hand the result straight to
//     GetWorldTime.
//   * GetWorldTime @0x0051AD20 — instruction-for-instruction KOTOR 1's: paused
//     check at [this+0x24], the fast path copying [this+0x28] to *outDay and
//     [this+0x2c] to *outMs, the slow path dividing the snapshot by 1000 and
//     [this+0x3c]. Four confirmed field offsets in a row is what makes
//     minutes_per_hour Same() below rather than a guess — and it has its own
//     witness anyway: K2's hour/minute split @0x0051AB40 reads it with the same
//     `movzx ecx, byte ptr [reg+0x38]` KOTOR 1's GetWorldTimeHour uses.
// The global-variable pair moved to acc::engine::ReadGlobalNumber (engine_area),
// which this file used to duplicate byte for byte; its K2 twins are documented
// there.
const uintptr_t kAddrCServerExoAppGetWorldTimer =
    acc::addr::Pick(0x004aede0, 0x0051C370);
const uintptr_t kAddrCWorldTimerGetWorldTime =
    acc::addr::Pick(0x004ade40, 0x0051AD20);
const size_t    kWorldTimerMinutesPerHourOffset     = acc::off::Same(0x38);  // byte

typedef void* (__thiscall* PFN_GetWorldTimer)(void* server);
typedef void  (__thiscall* PFN_GetWorldTime)(void* timer, uint32_t* outDay,
                                             uint32_t* outMs);

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
void DecideWreckAtFinish(void* miniGame);

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
        // The tracks' own finish signal: both games' heartbeat.ncs writes
        // MIN_RACE_GEAR = 6 when the bike crosses the line (see the gear-source
        // note at kGearGlobalName). Primary on both games.
        const bool script_finished =
            acc::engine::ReadGlobalNumber(kGearGlobalName) == 6;
        const bool envelope_finished =
            g_state.race_max_speed > kRaceMinTopSpeed && maxSpeed <= 0.0f;
        // The coast-to-a-stop fallback is KOTOR 1 only. On KOTOR 2 a collision
        // legitimately stops the bike dead mid-race (the hit script zeroes the
        // acceleration and the engine bounces it back), which fired a bogus
        // "23.22 seconds, race ended" at the first bar of the first K2 round
        // (patch-20260914-163810); the script flag above covers K2's finish.
        const bool coasted_to_stop =
            !acc::game::IsKotor2() &&
            g_state.race_peak_speed > kRaceMinTopSpeed && speed < kTerminalStopSpeed;
        if (script_finished || envelope_finished || coasted_to_stop) {
            acclog::Write("SwoopRace", "finish: script=%d envelope=%d coast=%d",
                          script_finished ? 1 : 0, envelope_finished ? 1 : 0,
                          coasted_to_stop ? 1 : 0);
            // KOTOR 2 writes the same flag for a wreck; K1 cannot wreck.
            if (acc::game::IsKotor2()) DecideWreckAtFinish(miniGame);
            SpeakRaceEndMessage();
            return;
        }
    }

    if (speed <= 1.0f) return;     // countdown / not launched yet
    if (maxSpeed <= 0.0f) return;  // race already ended — envelope cleared

    void* server = acc::engine::GetServerApp();
    if (!server) return;

    // Cache the fixed start stamp once the heartbeat has written THIS race's
    // value. MIN_TIME_* persists between races, so a stale previous-race stamp
    // would read as a huge initial elapsed — defer until it's fresh (a
    // just-stamped start gives a sub-second elapsed at launch).
    if (!g_state.have_start_ms) {
        uint32_t nowMs = 0, mph = 2;
        if (!ReadWorldClockMs(server, nowMs, mph)) return;
        using acc::engine::ReadGlobalNumber;
        const int sH = ReadGlobalNumber("MIN_TIME_HOUR");
        const int sM = ReadGlobalNumber("MIN_TIME_MIN");
        const int sS = ReadGlobalNumber("MIN_TIME_SEC");
        const int sC = ReadGlobalNumber("MIN_TIME_MIL");  // centiseconds
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
    // The scripts' gear global and (K2) the jump velocity ride along so a log
    // alone shows whether the bike is in the countdown (<0), racing (0..5) or
    // finished (6), and whether a Space press actually launched a hop.
    const int   scriptGear = acc::engine::ReadGlobalNumber(kGearGlobalName);
    const float jumpV      = acc::game::IsKotor2()
        ? SafeReadFloat(miniGame, kMiniGameJumpVelocityOffset) : 0.0f;
    acclog::Write("SwoopRace",
                  "snapshot speed=%.2f [%.2f..%.2f] gear=%d global=%d jumpV=%.1f "
                  "tunnel=%s (%.2f,%.2f,%.2f)",
                  speed, minSpeed, maxSpeed, g_state.gear, scriptGear, jumpV,
                  offOk ? "ok" : "FAULT", off.x, off.y, off.z);
}

// ============================================================================
// Gear-change detection.
// ============================================================================

// Speak the gear the bike is now in and re-arm the shift-ready cue for it.
// Shared by both gear sources. `why` is log-only.
void AnnounceGear(int gear, const char* why) {
    g_state.gear                  = gear;
    g_state.shift_ready_announced = false;
    g_state.last_gear_change_ms   = GetTickCount64();
    char buf[64];
    const char* fmt = acc::strings::Get(acc::strings::Id::FmtSwoopRaceGear);
    if (fmt && *fmt) {
        std::snprintf(buf, sizeof(buf), fmt, gear);
        prism::SpeakUrgent(buf);
    }
    acclog::Write("SwoopRace", "gear -> %d (%s)", gear, why);
}

void TickGearWatch(void* miniGame) {
    // ---- Primary: the track scripts' own MIN_RACE_GEAR (see kGearGlobalName).
    // Announced on EVERY change, downward included — on KOTOR 2 an obstacle
    // hit drops the bike to gear 0 and it has to be re-launched, which is
    // exactly the moment the player needs to hear "gear 0".
    const int scriptGear = acc::engine::ReadGlobalNumber(kGearGlobalName);
    if (scriptGear >= kGearGlobalMin && scriptGear <= kGearGlobalMax) {
        if (!g_state.gear_from_global) {
            g_state.gear_from_global = true;
            acclog::Write("SwoopRace",
                          "gear source: %s global (reads %d; plateau "
                          "heuristic off for this race)",
                          kGearGlobalName, scriptGear);
        }
        if (scriptGear != g_state.gear) {
            char why[48];
            std::snprintf(why, sizeof(why), "%s %d -> %d",
                          kGearGlobalName, g_state.gear, scriptGear);
            AnnounceGear(scriptGear, why);
        }
        return;
    }
    if (g_state.gear_from_global) return;  // countdown/finish value — keep last

    // ---- Fallback: max_speed-plateau heuristic (kGearShiftMaxDeltaMs), for a
    // track whose scripts do not maintain the global. Upward jumps only.
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) return;

    float maxSpeed = SafeReadFloat(player, kMiniPlayerMaxSpeedOffset);
    if (maxSpeed <= 0.0f) return;  // not yet initialised

    // Shift-up event: max_speed jumped by at least the noise floor.
    if (maxSpeed > g_state.last_max_speed + kGearShiftMaxDeltaMs) {
        char why[64];
        std::snprintf(why, sizeof(why), "plateau max=%.2f was %.2f",
                      maxSpeed, g_state.last_max_speed);
        g_state.last_max_speed = maxSpeed;
        AnnounceGear(g_state.gear + 1, why);
    }
}

// ============================================================================
// Hit points (log only). K2's enemyhitfollower.ncs subtracts 50 per obstacle
// collision, mine.ncs 100 per mine, both from the Citadel bike's 600; the
// heartbeat ends the race when the percentage it keeps in the racedialog
// local reaches 0 — round 3 (patch-20260914-204103) took exactly twelve hits
// and the run ended at 72 s, which the mod announced as a finish time. Each
// drop is logged; nothing is spoken here (round 4's "hit, N left" per drop
// was cut as noise — the HUD's damage gauge is the sighted equivalent). The
// wreck itself is decided at the race end (DecideWreckAtFinish): the engine
// mirrors a script's hit-point write late — round 4 read 95 for three
// seconds after the mine had zeroed the bike — so a live zero cannot be the
// trigger, and the follower reads 0 once more when it is torn down after
// the race. K1's obstacle script never touches hp, so nothing fires there.
// ============================================================================

void TickHitPoints(void* miniGame) {
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) return;
    const int hp = static_cast<int>(SafeReadU32(player, kFollowerHpOffset));
    if (hp < 0 || hp > 1000000) return;              // faulted / garbage
    // After the finish the engine zeroes the follower (max hp 0 too): that
    // final "hp 400 -> 0 (drop 400 of 0)" is not a hit, so keep it out of the
    // per-race hit count a log reader would tally.
    if (SafeReadU32(player, kFollowerMaxHpOffset) == 0) return;
    if (g_state.last_hp < 0) { g_state.last_hp = hp; return; }
    if (hp >= g_state.last_hp) { g_state.last_hp = hp; return; }
    const int drop = g_state.last_hp - hp;
    g_state.last_hp = hp;
    acclog::Write("SwoopRace", "hit: hp %d -> %d (drop %d of %u)",
                  hp + drop, hp, drop, SafeReadU32(player, kFollowerMaxHpOffset));
}

// At the track's own race-end flag: finish or wreck? Both write MIN_RACE_GEAR
// = 6. The Citadel heartbeat (heartbeat.ncs, disassembled) ends the race when
// the bike's y passes 6000 (the line), when the hit-point percentage in the
// racedialog local is <= 0, or on a menu quit; for the latter two it stamps
// 59 / 59 / 99 into the track's time globals (`<MODULE>_SWOOP_MIN` / `_SEC` /
// `_MSEC`, module = the area resref, "211TEL" on Citadel) where a finish
// stores the real time. That stamp is the decision here, with a zero
// hit-point read as the fallback. Round 4 (patch-20260915-075412) ended on a
// mine at y≈3360 with the mod speaking "61.56 seconds" — this turns that into
// "swoop wrecked". Names for the other two tracks follow the same pattern by
// assumption; a miss reads -1 and the fallback decides, and the log line
// shows which happened.
void DecideWreckAtFinish(void* miniGame) {
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    const int hp = player ? static_cast<int>(SafeReadU32(player, kFollowerHpOffset))
                          : -1;

    char area[96] = "";
    if (!acc::engine::GetCurrentAreaResName(area, sizeof(area))) area[0] = '\0';
    for (char* p = area; *p; ++p) {
        if (*p >= 'a' && *p <= 'z') *p = static_cast<char>(*p - 'a' + 'A');
    }
    char name[128];
    int stamp[3] = { -1, -1, -1 };
    const char* const kSuffix[3] = { "_SWOOP_MIN", "_SWOOP_SEC", "_SWOOP_MSEC" };
    if (area[0]) {
        for (int i = 0; i < 3; ++i) {
            std::snprintf(name, sizeof(name), "%s%s", area, kSuffix[i]);
            stamp[i] = acc::engine::ReadGlobalNumber(name);
        }
    }
    const bool forfeit_stamp = stamp[0] == 59 && stamp[1] == 59 && stamp[2] == 99;
    const bool hp_zero       = hp == 0;
    g_state.wrecked = forfeit_stamp || hp_zero;
    acclog::Write("SwoopRace",
                  "race end: %s (area=%s stamp=%d:%d.%d hp=%d)",
                  g_state.wrecked ? "WRECK" : "finish",
                  area[0] ? area : "?", stamp[0], stamp[1], stamp[2], hp);
}

// ============================================================================
// Stuck hint. The first K2 round ended pinned for 20 s: a bar hit had bounced
// the bike against a lane-edge post, and every relaunch (speed ~6) ran straight
// into it again — gear 1, gear 0, gear 1, gear 0 — while W-jumps from a
// standstill went nowhere. Nothing in the audio said what to do. When the
// race is running, the gear is 0 and the bike has not gained ground for
// kStuckHoldMs, speak the escape once; re-arm after real progress.
// ============================================================================

constexpr float     kStuckProgressU = 15.0f;   // forward gain that counts as progress
constexpr ULONGLONG kStuckHoldMs    = 6000;    // no progress for this long -> hint
// Parked right in front of a low bar (GetParkedBarSituation, KOTOR 2): the
// situation is unambiguous and the escape is a fixed key sequence, so the
// hint comes after this much instead — round 16 (patch-20260915-212249)
// spent up to 11 relaunches, 50 hp each, waiting for the six-second hint,
// and the hint then said "steer left, then W" without the jump.
constexpr ULONGLONG kStuckBarHoldMs = 1500;

constexpr float kStuckWallU = 13.0f;   // |tunnel x| at/beyond this = parked at a wall

void TickStuckHint(void* miniGame) {
    if (!g_state.have_start_ms) return;        // not launched yet / countdown
    Vector cam = {0.0f, 0.0f, 0.0f};
    if (!acc::engine::GetCameraPosition(cam)) return;
    const ULONGLONG now = GetTickCount64();
    if (!g_state.have_progress_mark ||
        cam.y - g_state.progress_mark_y >= kStuckProgressU) {
        g_state.have_progress_mark   = true;
        g_state.progress_mark_y      = cam.y;
        g_state.progress_mark_ms     = now;
        g_state.stuck_hint_spoken    = false;    // progress re-arms the hint
        g_state.stuck_bar_spoken_for = ParkedBar::None;
        return;
    }
    if (g_state.race_time_announced) return;   // finished, not stuck
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    const float speed = player ? SafeReadFloat(player, kFollowerSpeedOffset) : 0.0f;
    const int   gear  = acc::engine::ReadGlobalNumber(kGearGlobalName);
    if (gear != 0 || speed > 2.0f) return;     // moving or in gear: not pinned
    const ParkedBar bar  = GetParkedBarSituation();
    const ULONGLONG hold = (bar != ParkedBar::None) ? kStuckBarHoldMs : kStuckHoldMs;
    if (now - g_state.progress_mark_ms < hold) return;
    // Once per situation: a hint already spoken for THIS situation stays
    // spoken until progress; a changed situation (off the wall now, or the
    // bar classified after a generic hint) gets its own.
    if (g_state.stuck_hint_spoken && bar == g_state.stuck_bar_spoken_for) return;
    g_state.stuck_hint_spoken    = true;
    g_state.stuck_bar_spoken_for = bar;
    // At a wall the direction is known (K2 posts from |x| ≈ 16.3; 13 leaves
    // the pad lanes at 11 out of it): say which way, then the shift key.
    Vector off = {0.0f, 0.0f, 0.0f};
    const bool offOk = player && SafeReadVector(player, kMiniPlayerOffsetVectorOffset, off);
    acc::strings::Id id = acc::strings::Id::SwoopRaceStuckHint;
    const char* why = "generic";
    switch (bar) {
        case ParkedBar::WallRight: id = acc::strings::Id::SwoopRaceStuckBarWallLeft;  why = "bar, wall right"; break;
        case ParkedBar::WallLeft:  id = acc::strings::Id::SwoopRaceStuckBarWallRight; why = "bar, wall left";  break;
        case ParkedBar::Strip:     id = acc::strings::Id::SwoopRaceStuckBarStrip;     why = "bar, strip";      break;
        case ParkedBar::MustJump:  id = acc::strings::Id::SwoopRaceStuckBarJump;      why = "bar, must jump";  break;
        case ParkedBar::None:
            if (offOk && off.x >= kStuckWallU)       { id = acc::strings::Id::SwoopRaceStuckHintLeft;  why = "wall right"; }
            else if (offOk && off.x <= -kStuckWallU) { id = acc::strings::Id::SwoopRaceStuckHintRight; why = "wall left"; }
            break;
    }
    const char* msg = acc::strings::Get(id);
    if (msg && *msg) prism::SpeakUrgent(msg);
    acclog::Write("SwoopRace", "stuck hint: no progress for %llums at y=%.1f "
                  "(gear=%d speed=%.1f tunnelX=%.1f) [%s]",
                  now - g_state.progress_mark_ms, cam.y, gear, speed,
                  offOk ? off.x : 0.0f, why);
}

// ============================================================================
// Jump watch (KOTOR 2). Log-only for now: start + landing edges with the
// numbers a "jump now" cue needs (speed, gear, air time, peak height, world
// position). K1 never arms — its tunnel clamps offset.z to 0.
// ============================================================================

constexpr float kJumpArmZ    = 0.25f;  // offset.z above this = airborne
constexpr float kJumpLandZ   = 0.05f;  // ... and back below this = landed

void TickJumpWatch(void* miniGame) {
    if (!acc::game::IsKotor2()) return;
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) return;
    Vector off;
    if (!SafeReadVector(player, kMiniPlayerOffsetVectorOffset, off)) return;

    if (!g_state.airborne) {
        if (off.z <= kJumpArmZ) return;
        g_state.airborne        = true;
        g_state.jump_started_ms = GetTickCount64();
        g_state.jump_peak_z     = off.z;
        const float speed = SafeReadFloat(player, kFollowerSpeedOffset);
        const float vz    = SafeReadFloat(miniGame, kMiniGameJumpVelocityOffset);
        Vector pos = {0.0f, 0.0f, 0.0f};
        const bool posOk = acc::engine::GetCameraPosition(pos);
        acclog::Write("SwoopRace",
                      "jump start: z=%.2f vz=%.1f speed=%.1f gear=%d "
                      "camera=(%.1f,%.1f,%.1f)%s",
                      off.z, vz, speed, g_state.gear,
                      pos.x, pos.y, pos.z, posOk ? "" : " (no camera)");
        return;
    }
    if (off.z > g_state.jump_peak_z) g_state.jump_peak_z = off.z;
    if (off.z >= kJumpLandZ) return;
    g_state.airborne = false;
    const ULONGLONG air = GetTickCount64() - g_state.jump_started_ms;
    acclog::Write("SwoopRace",
                  "jump end: air=%llums peak_z=%.2f gear=%d",
                  air, g_state.jump_peak_z, g_state.gear);
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
    // Hold after a gear change. On KOTOR 2 a late shift (at the old gear's
    // top speed) is already above the NEXT gate, so the cue fired in the same
    // tick as the "gear N" speech and read as a shift confirmation
    // (round 3, 2026-09-14). A short gap keeps it a separate event; on K1 the
    // gates sit a notch higher so the hold never applies.
    if (GetTickCount64() - g_state.last_gear_change_ms < kShiftReadyHoldMs) return;

    const int gear = g_state.gear;
    if (gear < kFirstGatedGear || gear > kLastGatedGear) return;  // launch / top gear: no gate
    const float gate = ShiftGateSpeed(gear);
    if (gate <= 0.0f) return;

    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    if (!player) return;

    const float maxSpeed = SafeReadFloat(player, kMiniPlayerMaxSpeedOffset);
    if (maxSpeed <= 0.0f || gate >= maxSpeed) return;  // self-guard: gate unreachable

    const float speed = SafeReadFloat(player, kFollowerSpeedOffset);
    if (speed <= gate) return;

    g_state.shift_ready_announced = true;
    acc::audio::PlayCue(ShiftReadyCueResref());
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
    // KOTOR 2 rebinds the race: Space is the JUMP (OnBrake slot -> onjump.ncs)
    // and the gear shift moved to the OnAccelerate slot — see the string's
    // comment in strings.h for the input ids behind that.
    const char* controls = acc::strings::Get(
        acc::game::IsKotor2() ? acc::strings::Id::SwoopRaceControlsK2
                              : acc::strings::Id::SwoopRaceControls);
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
    // A wreck (hit points 0) is not a finish: no time, say what happened.
    if (g_state.wrecked) {
        msg = acc::strings::Get(acc::strings::Id::SwoopRaceWrecked);
    }
    // If we captured a finish-line time, speak the time-bearing variant (it
    // leads with the number and includes the "race ended" phrase); otherwise
    // the plain exit cue.
    else if (g_state.have_race_time) {
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
    if (msg && *msg) {
        if (g_state.wrecked || !g_state.have_race_time) {
            prism::SpeakUrgent(msg);   // a wreck: say it now
        } else {
            // The time waits out the finish sound (see kRaceEndSpeakDelayMs);
            // FlushRaceEndSpeech speaks it from the tick, or at once on exit.
            std::snprintf(g_state.race_end_msg, sizeof(g_state.race_end_msg), "%s", msg);
            g_state.race_end_pending = true;
            g_state.race_end_due_ms  = GetTickCount64() + kRaceEndSpeakDelayMs;
        }
    }
    acclog::Write("SwoopRace",
                  "spoke race end: [%s] (raceTime=%.2f have=%d%s)",
                  msg ? msg : "", g_state.race_time_seconds,
                  g_state.have_race_time ? 1 : 0,
                  g_state.race_end_pending ? " delayed" : "");
}

// Speaks the pending race time once its delay has elapsed (force = the race
// module is exiting: speak whatever is pending now rather than lose it).
void FlushRaceEndSpeech(bool force) {
    if (!g_state.race_end_pending) return;
    if (!force && GetTickCount64() < g_state.race_end_due_ms) return;
    g_state.race_end_pending = false;
    prism::SpeakUrgent(g_state.race_end_msg);
    acclog::Write("SwoopRace", "spoke race time%s: [%s]",
                  force ? " (flushed on exit)" : "", g_state.race_end_msg);
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
    g_state.gear_from_global    = false;
    g_state.shift_ready_announced = false;
    g_state.airborne            = false;
    g_state.jump_started_ms     = 0;
    g_state.jump_peak_z         = 0.0f;
    g_state.have_progress_mark  = false;
    g_state.stuck_hint_spoken   = false;
    g_state.last_hp             = -1;
    g_state.wrecked             = false;
    g_state.last_gear_change_ms = 0;
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
    FlushRaceEndSpeech(/*force=*/true);   // a delayed time must not die with the module
    // Stop every obstacle + accelpad loop — some may still be active
    // if the race ended while objects were in range.
    ResetSpatialAudio();
    g_state.active              = false;
    g_state.latched_mini_game   = nullptr;
    g_state.latched_vtable      = nullptr;
    g_state.ticks_since_lost    = 0;
    g_state.gear                = 0;
    g_state.last_max_speed      = 0.0f;
    g_state.gear_from_global    = false;
    g_state.shift_ready_announced = false;
    g_state.airborne            = false;
    // Fallback: if the terminal-stop detector already spoke the time (the
    // common path), this is a no-op; otherwise speak it now.
    SpeakRaceEndMessage();
}

// Run one of the K2 player bike's slot scripts through the engine's own
// runner, mirroring the guards the client input handler applies (a live
// swoop-type minigame with a player). SEH-guarded; false = nothing ran.
bool RunPlayerSlotScript(uintptr_t runner, const char* what) {
    if (!acc::game::IsKotor2() || !g_state.active) return false;
    void* mg = g_state.latched_mini_game;
    if (!mg || runner == 0) return false;
    if (SafeReadU32(mg, kMiniGameTypeOffset) != kMiniGameTypeSwoop) return false;
    void* player = SafeReadPtr(mg, kMiniGamePlayerOffset);
    if (!player) return false;
    __try {
        reinterpret_cast<PFN_RunFollowerSlotScript>(runner)(player, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("SwoopRace", "%s: runner faulted (player=%p)", what, player);
        return false;
    }
    acclog::Write("SwoopRace", "%s: ran (gear global=%d)", what,
                  acc::engine::ReadGlobalNumber(kGearGlobalName));
    return true;
}

}  // namespace

bool IsActive() { return g_state.active; }

bool TryShift() {
    return RunPlayerSlotScript(kAddrK2RunOnAccelerateScript, "shift (OnAccelerate)");
}

bool TryJump() {
    return RunPlayerSlotScript(kAddrK2RunOnBrakeScript, "jump (OnBrake)");
}

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

    // Keyboard shift (K2): the bare W hotkey. Space / pad A (jump) and pad B
    // (shift) arrive as events at the client input hook (input_pipeline.cpp);
    // W never does (the race polls it as an axis, not an event), so it is
    // polled here instead.
    if (acc::game::IsKotor2() &&
        acc::hotkeys::Pressed(acc::hotkeys::Action::SwoopShift)) {
        TryShift();
    }

    TickGearWatch(g_state.latched_mini_game);
    TickShiftReady(g_state.latched_mini_game);
    TickJumpWatch(g_state.latched_mini_game);
    TickHitPoints(g_state.latched_mini_game);   // log only; see DecideWreckAtFinish
    // TickRaceTimer first so have_start_ms is current this tick: it flips true
    // at the "Go!" launch (actual speed > 1 + fresh MIN_TIME stamp). Gate the
    // obstacle + accelpad proximity cues behind it so they stay silent through
    // the pre-race countdown and only begin once the race is actually underway.
    TickRaceTimer(g_state.latched_mini_game);
    if (g_state.have_start_ms) {
        TickSpatialAudio(g_state.latched_mini_game);
    }
    TickStuckHint(g_state.latched_mini_game);
    FlushRaceEndSpeech(/*force=*/false);
    EmitDiagSnapshot(g_state.latched_mini_game);
}

}  // namespace acc::swoop_race
