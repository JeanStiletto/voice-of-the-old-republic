// Turret (space-combat gunner) minigame accessibility — see
// turret_game.h for design.
//
// Shares CSWMiniGame with swoop racing; distinguished by type==3.
//
// What it does:
//   - Entry/exit announce (the native control hint — WASD aims, Space
//     fires; both are native keyboard actions, live-confirmed).
//   - Approach cue: a per-fighter 3D engine-loop so the player can hear
//     incoming fighters and swing the turret onto them BEFORE they open
//     fire. The vanilla fighters in this encounter carry NO engine sound
//     of their own (live diagnostic: every fighter's looping-sound source
//     at CSWTrackFollower+0x144 was null the whole game — only the gun
//     fire is audible), so there is nothing to "turn up"; we attach our
//     own loop using a custom sample (acc_turret_loop), a flattened
//     ping-pong turbine loop shipped in Override\. It was picked for its
//     broadband, high-frequency content: such sounds localise far better
//     — including in elevation (the W/S axis) — than a low engine drone,
//     whose energy sits below the band the ear uses to place a source.
//     (Built/tuned with `kdev sound-score`; see docs/kdev-design.md.)
//
// Cue policy: the nearest kFighterMaxConcurrent in-range fighters loop
// (not nearest-only of one), because a single-nearest scheme drops a
// fighter the moment a closer one appears — exactly the fly-by-while-
// another-approaches case the player needs to track. All fighters share
// the one loop sample; overlapping fighters are separated by 3D position
// (pan + distance), not timbre. Reuses the LoopSource + MGO-array walk
// pattern from swoop_spatial_audio.cpp.

#include "turret_game.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "audio_bus.h"        // PlayCue3D — one-shot lock-confirmation ping
#include "audio_loop.h"       // LoopSource — per-fighter 3D engine loop
#include "hotkeys.h"          // Q/E target-cycle bindings
#include "engine_area.h"      // GetCurrentArea + GetClientArea chain
#include "engine_offsets.h"   // Vector
#include "engine_player.h"    // AppManager chain (kAddrAppManagerPtr ...) +
                              //   GetCameraPosition / GetPlayerPosition
#include "log.h"
#include "prism.h"            // SpeakUrgent — entry/exit must beat NVDA's
                              //              typed-character cancel (the
                              //              player is holding fire keys)
#include "strings.h"          // Get(TurretGameStarted/Controls/Ended)

namespace acc::turret_game {

namespace {

// ============================================================================
// Engine struct offsets (shared with swoop_race.cpp — see that file and
// docs/llm-docs/re/swkotor.exe.h for the full CSWMiniGame walk).
// ============================================================================
constexpr size_t kClientAreaMiniGameOffset = 0x264;  // CSWCArea.mini_game
constexpr size_t kMiniGameVtableOffset     = 0x00;
constexpr size_t kMiniGamePlayerOffset     = 0x24;   // CSWMiniPlayer*
constexpr size_t kMiniGameEnemyCountOffset = 0x30;
constexpr size_t kMiniGameTypeOffset       = 0x84;   // 0=swoop, 3=turret

// Turret aim lives in CSWMiniPlayer.offset (+0x1c4) — rotational, NOT a
// flat screen reticle: z = azimuth (horizontal swing, 0..360°, A/D axis),
// x = elevation (vertical, W/S axis), y unused. WASD drives both natively
// (live-confirmed), so aiming/firing need no synthesis — only the spoken
// control hint and the approach cue below.
constexpr size_t kMiniPlayerAimOffset      = 0x1c4;  // Vector{x=elev,_,z=azi}

// The turret minigame type discriminator. Anything else on this struct
// (notably type==0 swoop) is not ours.
constexpr uint32_t kMiniGameTypeTurret     = 3;

// Exit-debounce: same rationale as swoop_race.cpp — the entry/exit
// transition flips the area chain for a few ticks, so only announce EXIT
// once the latched struct has been gone for this many consecutive ticks.
constexpr int       kExitDebounceTicks     = 60;

// ----- Approach-cue: MGO enemy-pool walk -----
//
// Same chain swoop_spatial_audio.cpp walks:
//   AppManager(*kAddrAppManagerPtr) +0x4 -> CClientExoApp +0x4 ->
//   CClientExoAppInternal +0x0 -> CSWMiniGameObjectArray
//   (+0x00 index, +0x04 objects[255]). vtable[0x1c] = AsEnemy.
constexpr size_t kClientInternalMgoArrayOffset = 0x0;
constexpr size_t kMgoArrayObjectsOffset        = 0x4;
constexpr int    kMgoArraySlotCount            = 255;
constexpr size_t kVtableSlotAsEnemy            = 0x1c;
// CSWTrackFollower model list (for world position via the model wrapper's
// vtable[+0x64], mirroring CSWTrackFollower::GetPosition).
constexpr size_t kTrackFollowerModelsDataOffset = 0x68;
constexpr size_t kModelVtableSlotGetPosition    = 0x64;
// Combat fields on CSWTrackFollower (== CSWMiniEnemy, which is just a single
// follower at offset 0; offsets confirmed against the explicit field9_0x88
// and field17_0x144 markers in swkotor.exe.h). Diagnostics only:
//   sphere_radius — hitbox radius, so the TRUE subtended half-angle the gun
//                   must fall inside is atan(radius/dist), not a fixed 6°.
//   hp/max_hp     — per-fighter health; an hp drop is a real engine-scored
//                   hit this tick, the ground truth for the actual hit cone.
//   invulnerability — i-frame timer; nonzero means hits won't register.
constexpr size_t kFollowerSphereRadiusOffset = 0x84;  // float
constexpr size_t kFollowerHpOffset           = 0x8c;  // int
constexpr size_t kFollowerMaxHpOffset        = 0x90;  // int
constexpr size_t kFollowerSpeedOffset        = 0x98;  // float (engine speed)
constexpr size_t kFollowerInvulnOffset       = 0x9c;  // float

// Cue range. Live distance survey (enemy-sound diagnostic, 802 samples):
// fighters span ~0 m to ~560 m, mean ~233 m, with real density past
// 500 m. 600 m range cues every fighter from the moment it appears.
constexpr float kFighterCueRangeM           = 600.0f;
// Distance compression onto the engine's well-behaved 5-20 m audibility
// band — same ratio mechanism swoop_spatial_audio uses for obstacles,
// but retuned for the turret's far-larger corridor (swoop is 1/9 over
// ~200 m; the turret is ~3x deeper). 1/30 maps the spawn distance
// (~560 m) to ~19 m (faint end of the band) and brings the source down
// to the 5 m floor by ~150 m, so a fighter is audible the instant it
// appears and grows louder all the way in.
constexpr float kFighterDistanceCompression = 1.0f / 30.0f;
// 5 m floor: swoop found 3 m inaudible and <5 m attenuates oddly, so the
// closest fighters sit at the loud-but-clean end of the band.
constexpr float kFighterMinSourceDistanceM  = 5.0f;
// Custom approach-cue loop sample, shipped in Override\acc_turret_loop.wav
// (a flattened ping-pong turbine loop — built with `kdev sound-score`).
// Resolved by bare resref via the engine ResLoader (Override -> BIF), the
// same way swoop's acc_boost is. One sample for every fighter; per-fighter
// separation comes from 3D position, not timbre.
constexpr const char* kFighterLoopResref    = "acc_turret_loop";
// Max fighters humming at once. All-in-range was tried and clumped into
// one wall of drone: every fighter is in range from the start, the heavy
// compression squashes their distances together (real 126-341 m -> src
// 5-11 m), and they fly in a forward cone, so 6 similar engine loops
// blend. Capping to the nearest few is what makes the swoop loop legible
// (it plays nearest-only). 2 keeps the soundstage separable while still
// covering the fly-by-while-another-approaches overlap.
constexpr int   kFighterMaxConcurrent       = 2;

// ----- Targeting "peg" tone (drives the Q/E-selected fighter) -----
// The aim (aziZ azimuth, elevX elevation, both degrees) forms a gun-ray
// direction vector; the angle between it and the direction to a fighter is
// the true 3D aim error (verified: at the zero-error frame the ray passed
// ~1.5 m from the fighter, and on-target frames averaged a 5 m lateral miss).
//
// The cue plays continuously while a fighter is SELECTED (via Q/E), mapping
// the aim error to that fighter onto loudness (via source distance): faint
// when the aim is far off it, loudest when on target. So the player locks a
// target with Q/E, then swings until the tone peaks and fires.
//
// "On target" is DISTANCE-SCALED, not a fixed angle. The engine scores hits
// against the fighter's 20 m sphere_radius, which subtends atan(radius/dist):
// ~20° at 60 m but only ~3° at 400 m (live-measured: on-target rate 11% close
// vs 0% far). The whole hitbox cone reads clearly LOUD (a shot can connect),
// but loudness still rises gently to a PEAK at dead-centre — because with bolt
// travel a moving fighter can drift out of the sphere mid-flight if you fire
// from the edge, whereas a centre shot leaves a full-radius margin. So:
//   centre (0°) ............ kPegMinDist  (loudest)
//   hitbox edge (subtend) .. kPegEdgeDist (still loud — "you can hit here")
//   subtend + kPegRampDeg .. kPegMaxDist  (faint — way off)
// A fixed peak-at-0° was too strict up close; a flat plateau lost the centre
// gradient that the bolt-travel margin needs.
constexpr float kPegRampDeg   = 30.0f;  // fade width beyond the hitbox edge
constexpr float kPegMinDist   = 5.0f;   // dead-centre -> nearest -> loudest
constexpr float kPegEdgeDist  = 9.0f;   // hitbox edge -> still clearly loud
constexpr float kPegMaxDist   = 20.0f;  // hitbox+ramp off -> farthest -> faintest
// Fallback on-target half-angle when the hitbox radius can't be read (no enemy
// ptr this tick). Also the value the diagnostic "onTarget" flag falls back to.
constexpr float kFallbackOnTargetDeg = 6.0f;
constexpr const char* kLockCueResref = "acc_turret_lock";

// ============================================================================
// Module state. Single-threaded under the engine OnUpdate tick.
// ============================================================================

struct State {
    bool      active           = false;
    void*     latched_mini_game = nullptr;
    void*     latched_vtable    = nullptr;
    ULONGLONG entered_at_ms     = 0;
    int       ticks_since_lost  = 0;

    // One loop per MGO slot index — a fighter is tracked by its slot so
    // the loop follows the same object across ticks (the pool is stable;
    // live diagnostic showed fixed enemy pointers per slot).
    acc::audio::LoopSource fighter_loops[kMgoArraySlotCount];

    // Single continuous targeting "peg" tone: tracks the Q/E-SELECTED
    // fighter; its loudness rises as the aim ray closes on that fighter.
    acc::audio::LoopSource peg_cue;

    // MGO slot of the fighter the player has locked via Q/E (-1 = none).
    // Explicit selection — the cue no longer auto-jumps between fighters.
    int selected_slot = -1;

    // CSWMiniGame.enemy_count (+0x30) seen last tick. Logged on change so a
    // kill (count drops) is distinguishable in the log from a fighter merely
    // flying out of cue range (count unchanged, slot returns later). -1 =
    // not yet sampled.
    int last_enemy_count = -1;

    // Stable display number per MGO slot (0 = unassigned). Assigned the first
    // time a slot is seen alive in the pool and never reused, so "Fighter N"
    // names the SAME physical fighter for the whole round — unlike the census
    // index, which is a per-tick distance rank. Survives the fighter leaving
    // cue range and (for the kill announce) outlives its destruction.
    int slot_number[kMgoArraySlotCount] = {0};
    int next_number = 1;

    // HP of the selected fighter last tick (-1 = unknown / just (re)selected).
    // A drop between ticks is a real engine-scored hit; logged with the aim
    // error at that frame to measure the actual hit cone. Reset on every
    // selection change so a new lock doesn't read as a phantom hit.
    int last_selected_hp = -1;

    // Previous-tick world position + timestamp of the selected fighter, for
    // the lead diagnostic (velocity = Δpos/Δt). Invalid until have_last_pos.
    Vector    last_selected_pos = {0.0f, 0.0f, 0.0f};
    ULONGLONG last_selected_pos_ms = 0;
    bool      have_last_pos = false;

    // ---- Per-session aim-quality counters (QC summary on exit). A clean,
    // bolt-travel-immune measure of how well the cue lets the player aim:
    // fraction of tracked frames with the aim ray inside the hitbox, by range
    // band. Reset on enter.
    int   qc_frames = 0;                  // TurretAim frames with a live aim
    int   qc_on_total = 0;                // within-hitbox frames (any range)
    int   qc_n[3]  = {0, 0, 0};           // frames per band: close/mid/far
    int   qc_on[3] = {0, 0, 0};           // within-hitbox per band
    float qc_min_err = 1e9f;              // best (smallest) errAngle seen
    float qc_sum_err = 0.0f;              // for mean errAngle
};

State g_state;

// ============================================================================
// SEH-guarded primitive reads (same pattern as engine_* / swoop_race).
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

float SafeReadF32(void* base, size_t off) {
    if (!base) return 0.0f;
    __try {
        return *reinterpret_cast<float*>(
            reinterpret_cast<unsigned char*>(base) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

// Read CSWCArea.mini_game via the player-area chain. Source of truth at
// the moment of detection; latched thereafter (the chain churns during
// the entry transition — see swoop_race.cpp).
void* ReadMiniGameViaArea() {
    void* serverArea = acc::engine::GetCurrentArea();
    if (!serverArea) return nullptr;
    void* clientArea = acc::engine::GetClientArea(serverArea);
    return SafeReadPtr(clientArea, kClientAreaMiniGameOffset);
}

// True iff `mg` is a live CSWMiniGame reporting the turret type.
bool IsTurretMiniGame(void* mg) {
    return mg && SafeReadU32(mg, kMiniGameTypeOffset) == kMiniGameTypeTurret;
}

bool LatchedStillValid() {
    if (!g_state.latched_mini_game || !g_state.latched_vtable) return false;
    void* vt = SafeReadPtr(g_state.latched_mini_game, kMiniGameVtableOffset);
    return vt == g_state.latched_vtable;
}

// ============================================================================
// MGO-walk helpers (mirrors swoop_spatial_audio.cpp — kept local so that
// swoop TU isn't entangled with turret-specific cueing).
// ============================================================================

typedef void*  (__thiscall* PFN_AsCast)(void* this_);
typedef Vector* (__thiscall* PFN_GetPositionThunk)(void* this_, Vector* outBuf);

void* ResolveMgoArray() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* clientApp = SafeReadPtr(appManager, kAppManagerClientAppOffset);
        if (!clientApp) return nullptr;
        void* clientInternal = SafeReadPtr(clientApp, kClientExoAppInternalOffset);
        if (!clientInternal) return nullptr;
        return SafeReadPtr(clientInternal, kClientInternalMgoArrayOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* CallAsCast(void* obj, size_t vtableSlotOffset) {
    if (!obj) return nullptr;
    __try {
        void* vtable = *reinterpret_cast<void**>(obj);
        if (!vtable) return nullptr;
        void* fn = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vtable) + vtableSlotOffset);
        if (!fn) return nullptr;
        return reinterpret_cast<PFN_AsCast>(fn)(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadFollowerPosition(void* follower, Vector& out) {
    if (!follower) return false;
    __try {
        void* modelsData = SafeReadPtr(follower, kTrackFollowerModelsDataOffset);
        if (!modelsData) return false;
        void* model = *reinterpret_cast<void**>(modelsData);
        if (!model) return false;
        void* vtable = *reinterpret_cast<void**>(model);
        if (!vtable) return false;
        void* fn = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vtable) + kModelVtableSlotGetPosition);
        if (!fn) return false;
        Vector buf = {0.0f, 0.0f, 0.0f};
        Vector* ret = reinterpret_cast<PFN_GetPositionThunk>(fn)(model, &buf);
        out = ret ? *ret : buf;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ============================================================================
// Approach cue. Only the nearest kFighterMaxConcurrent in-range fighters
// loop (the rest are stopped) — see kFighterMaxConcurrent for why all-in-
// range clumped. No forward-only filter: the turret swings a full 360°,
// so a fighter behind still matters. Loops are keyed by MGO slot so each
// follows its fighter across ticks; selection is re-evaluated every tick.
// ============================================================================

void StopAllLoops() {
    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        g_state.fighter_loops[i].Stop();
    }
    g_state.peg_cue.Stop();
}

// Read the turret aim as a unit direction vector. The aim is stored as
// {x=elevation, _, z=azimuth} in DEGREES; the gun ray is
//   (sin az·cos el, cos az·cos el, sin el).
// Verified identity against the 08:59 capture: at the zero-error frame
// (aziZ=0, elevX=9) this ray passed within ~1.5 m of the fighter.
bool ReadAimDir(Vector& dir) {
    void* player = SafeReadPtr(g_state.latched_mini_game, kMiniGamePlayerOffset);
    if (!player) return false;
    Vector aim = {0.0f, 0.0f, 0.0f};
    __try {
        aim = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(player) + kMiniPlayerAimOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    constexpr float kDeg2Rad = 0.01745329f;
    const float az = aim.z * kDeg2Rad;
    const float el = aim.x * kDeg2Rad;
    const float ce = std::cos(el);
    dir.x = std::sin(az) * ce;
    dir.y = std::cos(az) * ce;
    dir.z = std::sin(el);
    return true;
}

// Angle (degrees) between the aim ray and the (already normalised)
// direction to a target. 0 = dead on.
float AngleBetweenDeg(const Vector& a, const Vector& b) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z;
    if (dot > 1.0f) dot = 1.0f; else if (dot < -1.0f) dot = -1.0f;
    return std::acos(dot) * 57.2958f;
}

// Find a slot's index in the (nearest-first) census, or -1.
int CensusIndexOf(const int occSlot[], int occCount, int slot) {
    for (int a = 0; a < occCount; ++a) if (occSlot[a] == slot) return a;
    return -1;
}

// Stable "Fighter N" number for an MGO slot, assigned on first request and
// fixed for the round (see State::slot_number).
int NumberForSlot(int slot) {
    if (slot < 0 || slot >= kMgoArraySlotCount) return 0;
    if (g_state.slot_number[slot] == 0) {
        g_state.slot_number[slot] = g_state.next_number++;
    }
    return g_state.slot_number[slot];
}

// Lock onto census entry `idx`: set the selection, speak "Fighter N, D
// metres", and fire a LOUD one-shot positional cue toward the fighter so the
// player hears which way to swing — independent of where the aim currently
// points (the continuous peg is faint until the aim closes in). Shared by the
// Q/E cycle and the auto-advance on lock loss.
// `speak` gates the spoken "Fighter N, D metres" line. It is false for the
// silent re-pick after a kill — there the destroyed-fighter announce is the
// only speech, and this just relocks + fires the directional locator ping so
// the player can swing onto the next target without a second utterance.
void AnnounceSelectedTarget(int idx, const int occSlot[], const float occDist[],
                            const Vector occPos[], int occCount,
                            const Vector& listener, bool speak = true) {
    if (idx < 0 || idx >= occCount) return;
    g_state.selected_slot = occSlot[idx];
    g_state.last_selected_hp = -1;  // fresh lock — next tick re-baselines HP
    g_state.have_last_pos    = false;  // and re-baselines velocity
    const int number = NumberForSlot(occSlot[idx]);

    const int meters = static_cast<int>(occDist[idx] + 0.5f);
    if (speak) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      acc::strings::Get(acc::strings::Id::FmtTurretTarget),
                      number, meters);
        prism::SpeakUrgent(buf);
    }

    // One-shot confirmation at a compressed (always-audible) position in the
    // fighter's direction, full volume — "locked, it's this way". Plays even
    // on a silent re-pick (it's a locator cue, not speech).
    const float sd = occDist[idx];
    Vector pingPos = occPos[idx];
    if (sd > 0.0f) {
        const float kk = kPegMinDist / sd;
        pingPos.x = listener.x + (occPos[idx].x - listener.x) * kk;
        pingPos.y = listener.y + (occPos[idx].y - listener.y) * kk;
        pingPos.z = listener.z + (occPos[idx].z - listener.z) * kk;
    }
    acc::audio::PlayCue3D(kLockCueResref, pingPos);

    acclog::Write("Turret", "select -> slot=%d number=%d pos=%d/%d dist=%dm speak=%d",
                  g_state.selected_slot, number, idx + 1, occCount, meters, speak ? 1 : 0);
}

// Q/E target cycle. Q steps to the previous (nearer) target, E to the next
// (farther), wrapping at the ends. Polled inside the hotkeys BeginTick/
// EndTick window (core_tick calls turret Tick there), so Pressed() sees a
// clean rising edge.
void HandleTargetCycle(const int occSlot[], const float occDist[],
                       const Vector occPos[], int occCount,
                       const Vector& listener) {
    const bool next = acc::hotkeys::Pressed(acc::hotkeys::Action::TurretCycleNext);
    const bool prev = acc::hotkeys::Pressed(acc::hotkeys::Action::TurretCyclePrev);
    if (!next && !prev) return;

    if (occCount <= 0) {
        prism::SpeakUrgent(acc::strings::Get(acc::strings::Id::TurretNoTargets));
        g_state.selected_slot = -1;
        acclog::Write("Turret", "cycle: no targets");
        return;
    }

    int idx = CensusIndexOf(occSlot, occCount, g_state.selected_slot);
    if (idx < 0) {
        // No live selection — first press lands on nearest (E) / farthest (Q).
        idx = next ? 0 : occCount - 1;
    } else {
        idx += next ? 1 : -1;
        if (idx < 0)          idx = occCount - 1;  // wrap
        if (idx >= occCount)  idx = 0;
    }
    AnnounceSelectedTarget(idx, occSlot, occDist, occPos, occCount, listener);
}

// Drive the continuous peg tone on the selected fighter. Loudness (via source
// distance) encodes the aim error to THAT fighter — faint off-aim, peaks on
// target. When the selected fighter leaves the census (destroyed / out of
// range), AUTO-ADVANCE to the nearest remaining fighter and lock it; if none
// remain, announce "target lost" and fall silent.
void DriveSelectedPeg(const int occSlot[], const float occDist[],
                      const Vector occPos[], int occCount,
                      const Vector& aimDir, bool haveAim,
                      const Vector& listener, bool selectedAliveInPool,
                      void* selectedEnemy) {
    int idx = CensusIndexOf(occSlot, occCount, g_state.selected_slot);
    // True only when the locked fighter was already in the census on entry
    // (not a fresh re-pick this tick) — gates the HP/hit read so selectedEnemy
    // matches the fighter we log.
    const bool trackingExisting = (idx >= 0);

    if (idx < 0) {
        // The locked fighter is not in the census this tick.
        if (g_state.selected_slot >= 0) {
            // Distinguish a KILL (the fighter left the MGO pool entirely —
            // AsEnemy no longer resolves it) from a fighter that merely flew
            // beyond cue range (still alive, will return). selectedAliveInPool
            // is set by TickFighterCues' Pass-1 walk regardless of range.
            const bool killed = !selectedAliveInPool;

            if (g_state.peg_cue.IsActive()) {
                g_state.peg_cue.Stop();
                acclog::Write("Turret", "peg off (%s)",
                              killed ? "destroyed" : "out of range");
            }

            if (killed) {
                // Announce the DESTROYED fighter by its stable number — this
                // is the only speech; the new lock below is silent.
                const int num = NumberForSlot(g_state.selected_slot);
                char buf[64];
                std::snprintf(buf, sizeof(buf),
                              acc::strings::Get(acc::strings::Id::FmtTurretDestroyed),
                              num);
                prism::SpeakUrgent(buf);
                acclog::Write("Turret", "kill: slot=%d number=%d",
                              g_state.selected_slot, num);
            }

            if (occCount > 0) {
                // Auto-advance to the nearest remaining fighter so the player
                // keeps a target without re-pressing Q/E. Silent after a kill
                // (the destroyed-announce stands alone); spoken on a range-exit
                // so the player knows the lock moved.
                AnnounceSelectedTarget(0, occSlot, occDist, occPos, occCount,
                                       listener, /*speak=*/!killed);
                idx = 0;
            } else {
                // Nothing left to lock. On a kill the destroyed-announce
                // already explains the silence; only a range-exit needs the
                // "target lost" cue.
                if (!killed) {
                    prism::SpeakUrgent(
                        acc::strings::Get(acc::strings::Id::TurretTargetLost));
                }
                g_state.selected_slot = -1;
                acclog::Write("Turret", "no fighters remain (%s)",
                              killed ? "after kill" : "target lost");
                return;
            }
        } else {
            // Nothing selected yet (game start / all clear). Auto-pick the
            // nearest fighter the instant one is in range, so the player
            // begins the round already locked without pressing Q/E — same
            // nearest-first rule as the lose-target auto-advance above.
            if (occCount > 0) {
                AnnounceSelectedTarget(0, occSlot, occDist, occPos, occCount,
                                       listener);
                idx = 0;
            } else {
                return;  // no fighters in range yet — wait
            }
        }
    }

    if (!haveAim) return;  // hold the cue; can't compute aim this tick

    constexpr float kRad2Deg = 57.2957795f;

    const Vector& sp = occPos[idx];
    const float   sd = occDist[idx];
    float angle = 0.0f;
    Vector tdir = {0.0f, 0.0f, 0.0f};
    if (sd > 0.0f) {
        tdir.x = (sp.x - listener.x) / sd;
        tdir.y = (sp.y - listener.y) / sd;
        tdir.z = (sp.z - listener.z) / sd;
        angle = AngleBetweenDeg(aimDir, tdir);
    }

    // Combat fields off the selected CSWTrackFollower. The hitbox half-angle
    // (atan(radius/dist)) is what makes "on target" distance-correct.
    int   hp = -1, maxHp = -1;
    float radius = 0.0f, invuln = 0.0f, subtendDeg = 0.0f;
    if (selectedEnemy && trackingExisting) {
        hp     = static_cast<int>(SafeReadU32(selectedEnemy, kFollowerHpOffset));
        maxHp  = static_cast<int>(SafeReadU32(selectedEnemy, kFollowerMaxHpOffset));
        radius = SafeReadF32(selectedEnemy, kFollowerSphereRadiusOffset);
        invuln = SafeReadF32(selectedEnemy, kFollowerInvulnOffset);
        if (sd > 0.0f && radius > 0.0f) {
            subtendDeg = std::atan(radius / sd) * kRad2Deg;
        }
    }
    // The angle inside which a shot connects. subtendDeg when we have the
    // radius; the fixed fallback otherwise.
    const float onTargetAngle =
        (subtendDeg > 0.0f) ? subtendDeg : kFallbackOnTargetDeg;
    const bool  onTarget = (angle <= onTargetAngle);

    // Loudness: inside the hitbox, rise gently from the edge (kPegEdgeDist) to
    // a peak at dead-centre (kPegMinDist) so the player can refine to the
    // bolt-travel-safe centre; beyond the edge, fade to faint over kPegRampDeg.
    // Continuous at the boundary (both branches give kPegEdgeDist there).
    float srcDist;
    if (onTarget) {
        const float u = (onTargetAngle > 0.0f) ? angle / onTargetAngle : 0.0f;  // 0 centre..1 edge
        srcDist = kPegMinDist + u * (kPegEdgeDist - kPegMinDist);
    } else {
        float t = (angle - onTargetAngle) / kPegRampDeg;
        if (t > 1.0f) t = 1.0f;
        srcDist = kPegEdgeDist + t * (kPegMaxDist - kPegEdgeDist);
    }
    const float kk = (sd > 0.0f) ? srcDist / sd : 1.0f;
    const Vector cuePos = {
        listener.x + (sp.x - listener.x) * kk,
        listener.y + (sp.y - listener.y) * kk,
        listener.z + (sp.z - listener.z) * kk,
    };

    if (g_state.peg_cue.IsActive()) {
        g_state.peg_cue.UpdatePosition(cuePos);
    } else if (g_state.peg_cue.Start(kLockCueResref, cuePos)) {
        acclog::Write("Turret", "peg on selected slot=%d", g_state.selected_slot);
    }

    const float lateral = std::sin(angle * 0.01745329f) * sd;

    // ---- Diagnostic: split the blended error into per-axis azimuth and
    // elevation error, and log the raw aim/target angles that feed it.
    // aimAz/aimEl recovered from aimDir (same convention as ReadAimDir:
    // dir = (sin az·cos el, cos az·cos el, sin el)); tgt from tdir.
    auto wrap180 = [](float d) {
        while (d > 180.0f)  d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        return d;
    };
    auto clamp1 = [](float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); };
    const float aimAz = std::atan2(aimDir.x, aimDir.y) * kRad2Deg;
    const float aimEl = std::asin(clamp1(aimDir.z))   * kRad2Deg;
    const float tgtAz = std::atan2(tdir.x,   tdir.y)  * kRad2Deg;
    const float tgtEl = std::asin(clamp1(tdir.z))     * kRad2Deg;
    const float errAz = wrap180(aimAz - tgtAz);
    const float errEl = aimEl - tgtEl;

    // An hp drop between ticks is a real engine-scored hit — log the aim that
    // landed it (the engine plays its own mgs_sith_hit, so this is diagnostic
    // only). Confounded by bolt travel time: the hit reflects a past fire frame.
    if (selectedEnemy && trackingExisting) {
        if (g_state.last_selected_hp >= 0 && hp < g_state.last_selected_hp) {
            acclog::Write("TurretHit",
                          "slot=%d number=%d hp=%d->%d errAngle=%.1f "
                          "errAz=%.1f errEl=%.1f lateralMiss=%.1fm dist=%.0f "
                          "radius=%.1f subtendDeg=%.1f invuln=%.2f",
                          g_state.selected_slot, NumberForSlot(g_state.selected_slot),
                          g_state.last_selected_hp, hp, angle, errAz, errEl,
                          lateral, sd, radius, subtendDeg, invuln);
        }
        g_state.last_selected_hp = hp;
    }

    // ---- Lead diagnostic. Measure the fighter's velocity from per-tick Δpos,
    // and report critBoltSpeed = |v|·dist/radius — the bolt speed BELOW which
    // the fighter outruns the 20 m hitbox margin during flight (lead matters).
    // If critBoltSpeed stays small vs a real laser bolt, lead isn't worth it.
    // No bolt-speed RE needed; engine `speed` field logged to cross-check unit.
    {
        const ULONGLONG now = GetTickCount64();
        const float engineSpeed = (selectedEnemy && trackingExisting)
            ? SafeReadF32(selectedEnemy, kFollowerSpeedOffset) : 0.0f;
        if (g_state.have_last_pos && now > g_state.last_selected_pos_ms) {
            const float dt = (now - g_state.last_selected_pos_ms) / 1000.0f;
            const float vx = (sp.x - g_state.last_selected_pos.x) / dt;
            const float vy = (sp.y - g_state.last_selected_pos.y) / dt;
            const float vz = (sp.z - g_state.last_selected_pos.z) / dt;
            const float vmag = std::sqrt(vx * vx + vy * vy + vz * vz);
            const float crit = (radius > 0.0f) ? vmag * sd / radius : 0.0f;
            acclog::Write("TurretVel",
                          "slot=%d velMS=%.1f engineSpeed=%.1f dist=%.0f "
                          "radius=%.1f critBoltSpeed=%.0f",
                          g_state.selected_slot, vmag, engineSpeed, sd, radius,
                          crit);
        }
        g_state.last_selected_pos    = sp;
        g_state.last_selected_pos_ms = now;
        g_state.have_last_pos        = true;
    }

    // ---- QC accumulation: aim-quality, immune to bolt-travel/firing RNG.
    {
        const int band = (sd < 100.0f) ? 0 : (sd < 250.0f ? 1 : 2);
        ++g_state.qc_frames;
        ++g_state.qc_n[band];
        if (onTarget) { ++g_state.qc_on_total; ++g_state.qc_on[band]; }
        if (angle < g_state.qc_min_err) g_state.qc_min_err = angle;
        g_state.qc_sum_err += angle;
    }

    acclog::Write("TurretAim",
                  "selected slot=%d errAngle=%.1f errAz=%.1f errEl=%.1f "
                  "aim(az=%.1f el=%.1f) tgt(az=%.1f el=%.1f) "
                  "lateralMiss=%.0fm dist=%.0f hp=%d/%d radius=%.1f "
                  "subtendDeg=%.1f onTarget=%d",
                  g_state.selected_slot, angle, errAz, errEl,
                  aimAz, aimEl, tgtAz, tgtEl,
                  lateral, sd, hp, maxHp, radius, subtendDeg,
                  onTarget ? 1 : 0);
}

void TickFighterCues() {
    void* mgoArray = ResolveMgoArray();
    if (!mgoArray) return;

    Vector listener;
    if (!acc::engine::GetCameraPosition(listener) &&
        !acc::engine::GetPlayerPosition(listener)) {
        return;
    }

    const float rangeSq = kFighterCueRangeM * kFighterCueRangeM;

    // ---- Pass 1: census every in-range fighter (for the Q/E cycle) AND
    // pick the nearest kFighterMaxConcurrent for the ambient turbine loops.
    int    chosenSlot[kFighterMaxConcurrent];
    float  chosenDistSq[kFighterMaxConcurrent];
    Vector chosenPos[kFighterMaxConcurrent];
    for (int k = 0; k < kFighterMaxConcurrent; ++k) chosenSlot[k] = -1;

    Vector aimDir;
    const bool haveAim = ReadAimDir(aimDir);

    int    occSlot[kMgoArraySlotCount];
    float  occDist[kMgoArraySlotCount];
    Vector occPos[kMgoArraySlotCount];
    int    occCount = 0;

    // Whether the currently-locked fighter still exists in the MGO pool at
    // all (alive but possibly out of cue range). Lets DriveSelectedPeg tell a
    // kill (pool entry gone) from a mere range-exit (entry still there).
    bool  selectedAliveInPool = false;
    // The locked fighter's CSWTrackFollower* (for the HP/hitbox diagnostic).
    void* selectedEnemy        = nullptr;

    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        void* slot = SafeReadPtr(mgoArray,
                                 kMgoArrayObjectsOffset +
                                 static_cast<size_t>(i) * sizeof(void*));
        void* enemy = slot ? CallAsCast(slot, kVtableSlotAsEnemy) : nullptr;
        if (!enemy) continue;

        // Alive in the pool — assign its stable number (idempotent) and note
        // if it's the locked fighter, BEFORE the range cull below.
        NumberForSlot(i);
        if (i == g_state.selected_slot) {
            selectedAliveInPool = true;
            selectedEnemy       = enemy;
        }

        Vector pos;
        if (!ReadFollowerPosition(enemy, pos)) continue;
        const float dx = pos.x - listener.x;
        const float dy = pos.y - listener.y;
        const float dz = pos.z - listener.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > rangeSq) continue;

        // Census entry for Q/E cycling.
        occSlot[occCount] = i;
        occDist[occCount] = std::sqrt(distSq);
        occPos[occCount]  = pos;
        ++occCount;

        // Insert (i, distSq, pos) into the nearest-K ambient table.
        for (int k = 0; k < kFighterMaxConcurrent; ++k) {
            if (chosenSlot[k] < 0 || distSq < chosenDistSq[k]) {
                for (int m = kFighterMaxConcurrent - 1; m > k; --m) {
                    chosenSlot[m]   = chosenSlot[m - 1];
                    chosenDistSq[m] = chosenDistSq[m - 1];
                    chosenPos[m]    = chosenPos[m - 1];
                }
                chosenSlot[k]   = i;
                chosenDistSq[k] = distSq;
                chosenPos[k]    = pos;
                break;
            }
        }
    }

    // Sort the census nearest-first so Q/E cycles near->far and the auto-pick
    // lands on the nearest (insertion sort; occCount is small). The spoken
    // "Fighter N" is the slot's stable number, independent of this order.
    for (int a = 1; a < occCount; ++a) {
        const int    s = occSlot[a];
        const float  d = occDist[a];
        const Vector p = occPos[a];
        int b = a - 1;
        while (b >= 0 && occDist[b] > d) {
            occSlot[b + 1] = occSlot[b];
            occDist[b + 1] = occDist[b];
            occPos[b + 1]  = occPos[b];
            --b;
        }
        occSlot[b + 1] = s;
        occDist[b + 1] = d;
        occPos[b + 1]  = p;
    }

    // Q/E selects the locked target; the peg tone tracks it (and auto-
    // advances to the nearest fighter when the locked one is destroyed).
    HandleTargetCycle(occSlot, occDist, occPos, occCount, listener);
    DriveSelectedPeg(occSlot, occDist, occPos, occCount, aimDir, haveAim,
                     listener, selectedAliveInPool, selectedEnemy);

    // ---- Pass 2: stop any active loop not in the chosen set. -------------
    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        if (!g_state.fighter_loops[i].IsActive()) continue;
        bool keep = false;
        for (int k = 0; k < kFighterMaxConcurrent; ++k) {
            if (chosenSlot[k] == i) { keep = true; break; }
        }
        if (!keep) {
            g_state.fighter_loops[i].Stop();
            acclog::Write("Turret", "fighter loop stop slot=%d", i);
        }
    }

    // ---- Pass 3: start / update the chosen loops. ------------------------
    for (int k = 0; k < kFighterMaxConcurrent; ++k) {
        const int i = chosenSlot[k];
        if (i < 0) continue;

        const float realDist = std::sqrt(chosenDistSq[k]);
        Vector cuePos = chosenPos[k];
        float  srcDist = realDist;
        if (realDist > 0.0f) {
            srcDist = realDist * kFighterDistanceCompression;
            if (srcDist < kFighterMinSourceDistanceM) {
                srcDist = kFighterMinSourceDistanceM;
            }
            const float kk = srcDist / realDist;
            cuePos.x = listener.x + (chosenPos[k].x - listener.x) * kk;
            cuePos.y = listener.y + (chosenPos[k].y - listener.y) * kk;
            cuePos.z = listener.z + (chosenPos[k].z - listener.z) * kk;
        }

        if (g_state.fighter_loops[i].IsActive()) {
            g_state.fighter_loops[i].UpdatePosition(cuePos);
        } else {
            if (g_state.fighter_loops[i].Start(kFighterLoopResref, cuePos)) {
                acclog::Write("Turret",
                              "fighter loop start slot=%d rank=%d res=%s "
                              "realDist=%.0f srcDist=%.1f pos=(%.1f,%.1f,%.1f)",
                              i, k, kFighterLoopResref, realDist, srcDist,
                              chosenPos[k].x, chosenPos[k].y, chosenPos[k].z);
            }
        }
    }

    acclog::Trace("Turret", "fighter cues: %d in range, cap %d (range %.0fm)",
                  occCount, kFighterMaxConcurrent, kFighterCueRangeM);
}

// ============================================================================
// Speech. Combine opener + control reminder into ONE urgent utterance so
// the two halves can't preempt each other (same rationale as the swoop
// opener — every SpeakUrgent interrupts).
// ============================================================================

void AnnounceEntry() {
    const char* opener   = acc::strings::Get(acc::strings::Id::TurretGameStarted);
    const char* controls = acc::strings::Get(acc::strings::Id::TurretGameControls);
    if ((!opener || !*opener) && (!controls || !*controls)) return;

    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s %s",
                  opener ? opener : "", controls ? controls : "");
    prism::SpeakUrgent(buf);
    acclog::Write("Turret", "spoke entry: [%s]", buf);
}

void AnnounceExit() {
    const char* msg = acc::strings::Get(acc::strings::Id::TurretGameEnded);
    if (msg && *msg) prism::SpeakUrgent(msg);
    acclog::Write("Turret", "spoke exit: [%s]", msg ? msg : "");
}

// ============================================================================
// State transitions.
// ============================================================================

void HandleEnter(void* mg) {
    g_state.active            = true;
    g_state.latched_mini_game = mg;
    g_state.latched_vtable    = SafeReadPtr(mg, kMiniGameVtableOffset);
    g_state.entered_at_ms     = GetTickCount64();
    g_state.ticks_since_lost  = 0;
    g_state.selected_slot     = -1;
    g_state.last_enemy_count  = -1;
    g_state.last_selected_hp  = -1;
    g_state.have_last_pos     = false;
    // Fresh stable-number assignment for this round.
    for (int i = 0; i < kMgoArraySlotCount; ++i) g_state.slot_number[i] = 0;
    g_state.next_number       = 1;
    // Fresh aim-quality counters for the session summary.
    g_state.qc_frames = 0;
    g_state.qc_on_total = 0;
    for (int b = 0; b < 3; ++b) { g_state.qc_n[b] = 0; g_state.qc_on[b] = 0; }
    g_state.qc_min_err = 1e9f;
    g_state.qc_sum_err = 0.0f;
    // Defensive: no loop from a prior turret session must survive.
    StopAllLoops();

    uint32_t enemies = SafeReadU32(mg, kMiniGameEnemyCountOffset);
    acclog::Write("Turret",
                  "ENTER mini_game=%p vtable=%p type=%u enemies=%u",
                  mg, g_state.latched_vtable,
                  SafeReadU32(mg, kMiniGameTypeOffset), enemies);

    AnnounceEntry();
}

void HandleExit() {
    ULONGLONG dur = GetTickCount64() - g_state.entered_at_ms;
    acclog::Write("Turret", "EXIT after %llu ms (debounced %d ticks)",
                  dur, kExitDebounceTicks);

    // Session aim-quality summary — the bolt-travel-immune QC metric. "% inside
    // hitbox" by range band measures how well the cue let the player aim,
    // independent of kills (which are noisy: firing cadence, bolt travel, RNG).
    if (g_state.qc_frames > 0) {
        const float pct = 100.0f * g_state.qc_on_total / g_state.qc_frames;
        const float meanErr = g_state.qc_sum_err / g_state.qc_frames;
        auto bandPct = [](int on, int n) { return n > 0 ? 100.0f * on / n : 0.0f; };
        acclog::Write("TurretQC",
                      "session: frames=%d within-hitbox=%d (%.1f%%) "
                      "close=%d/%d (%.0f%%) mid=%d/%d (%.0f%%) far=%d/%d (%.0f%%) "
                      "minErr=%.1f meanErr=%.1f",
                      g_state.qc_frames, g_state.qc_on_total, pct,
                      g_state.qc_on[0], g_state.qc_n[0], bandPct(g_state.qc_on[0], g_state.qc_n[0]),
                      g_state.qc_on[1], g_state.qc_n[1], bandPct(g_state.qc_on[1], g_state.qc_n[1]),
                      g_state.qc_on[2], g_state.qc_n[2], bandPct(g_state.qc_on[2], g_state.qc_n[2]),
                      g_state.qc_min_err, meanErr);
    }

    StopAllLoops();
    g_state.active            = false;
    g_state.latched_mini_game = nullptr;
    g_state.latched_vtable    = nullptr;
    g_state.ticks_since_lost  = 0;
    g_state.selected_slot     = -1;
    g_state.last_enemy_count  = -1;
    AnnounceExit();
}

}  // namespace

bool IsActive() { return g_state.active; }

void Tick() {
    void* mgArea = ReadMiniGameViaArea();

    if (!g_state.active) {
        // Idle. Fire ENTER only for the turret minigame (type==3); swoop
        // (type==0) is handled by swoop_race.cpp.
        if (IsTurretMiniGame(mgArea)) HandleEnter(mgArea);
        return;
    }

    // Active. Verify the latch is still alive (two truth sources — area
    // chain and latched vtable; EXIT only when both say gone for
    // kExitDebounceTicks). Mirrors swoop_race.cpp.
    if (mgArea && mgArea != g_state.latched_mini_game) {
        // Different struct mid-game (engine swap / fresh game). Only
        // re-latch if it's still a turret; otherwise let the debounce
        // run toward EXIT.
        if (IsTurretMiniGame(mgArea)) {
            void* vt = SafeReadPtr(mgArea, kMiniGameVtableOffset);
            acclog::Write("Turret", "re-latch: old=%p new=%p vtable=%p",
                          g_state.latched_mini_game, mgArea, vt);
            g_state.latched_mini_game = mgArea;
            g_state.latched_vtable    = vt;
            g_state.ticks_since_lost  = 0;
        } else {
            ++g_state.ticks_since_lost;
        }
    } else if (mgArea && mgArea == g_state.latched_mini_game) {
        g_state.ticks_since_lost = 0;
    } else if (!mgArea && LatchedStillValid()) {
        // Area chain lost visibility but the struct is intact — stay locked.
        g_state.ticks_since_lost = 0;
    } else {
        ++g_state.ticks_since_lost;
    }

    if (g_state.ticks_since_lost >= kExitDebounceTicks) {
        HandleExit();
        return;
    }

    // Surface kills: when CSWMiniGame.enemy_count drops, a fighter was
    // destroyed (vs. merely leaving cue range, which leaves the count alone).
    const int enemyCount =
        static_cast<int>(SafeReadU32(g_state.latched_mini_game,
                                     kMiniGameEnemyCountOffset));
    if (enemyCount != g_state.last_enemy_count) {
        acclog::Write("Turret", "enemy_count %d -> %d",
                      g_state.last_enemy_count, enemyCount);
        g_state.last_enemy_count = enemyCount;
    }

    // Aiming (WASD) and firing (Space/Enter) are native keyboard actions.
    // The per-fighter approach loop is the one thing we drive per tick.
    TickFighterCues();
}

}  // namespace acc::turret_game
