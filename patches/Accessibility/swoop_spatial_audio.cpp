// Swoop race spatial-audio implementation — see swoop_spatial_audio.h.
//
// Split from swoop_race.cpp on 2026-05-27 (large-file-handling pass).
// The race lifecycle (entry/exit, gear watch, wall collision,
// acceleration speedometer) stays in swoop_race.cpp; everything that
// walks the 255-slot CSWMiniGameObjectArray to surface obstacles and
// accelpads as 3D loops lives here.

#include "swoop_spatial_audio.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstddef>

#include "audio_loop.h"       // LoopSource — sustained warning per
                              //     obstacle / nearest-pad cue
#include "engine_offsets.h"   // Vector
#include "engine_player.h"    // GetCameraPosition (primary listener
                              //     anchor — engine swaps the player
                              //     creature during the race, so
                              //     GetPlayerPosition is fallback)
#include "log.h"

namespace acc::swoop_race {

namespace {

// ============================================================================
// Engine struct offsets used by the spatial walks.
// ============================================================================

// CSWMGObstacle (line ~17456 in re/swkotor.exe.h) — AurObject pointer
// at +0x60, then world position via the AurObject's Gob at +0x78.
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
// non-null slot, keep the non-null returns. We make two such passes
// per tick — one for obstacles (rocks/debris), one for enemies
// (accelerator pads).
constexpr size_t kClientInternalMgoArrayOffset  = 0x0;
constexpr size_t kMgoArrayObjectsOffset         = 0x4;
constexpr int    kMgoArraySlotCount             = 255;
constexpr size_t kVtableSlotAsObstacle          = 0x20;
constexpr size_t kVtableSlotAsEnemy             = 0x1c;

// ----- Object pool split: rocks vs accelerator pads -----
//
// Verified by extracting the per-track .are files (all three swoop
// modules — m03mg, m17mg, m26mg — agree):
//
//   <list label="Obstacles">  — exactly 22 entries (mgo01..mgo22).
//     Rocks / debris. CSWMGObstacle, vtable 0x0075287C. Names are
//     sequential (m03mg_MGO01..MGO22) and carry no semantic discriminator.
//
//   <list label="Enemies">    — 30 entries riding their own tracks
//     (mgt02..mgt31). All use model `mgf_accelpad01`, all carry
//     `<byte label="Trigger">1</byte>` (collision-only, no bullets).
//     These are the accelerator pads. CSWMiniEnemy (extends
//     CSWTrackFollower). On collision, the area-level
//     OnHitFollower=accelpad script (tar_m03mg_s.rim/accelpad.ncs)
//     fires SWMG_SetPlayerSpeed * 1.05 + acceleration * 1.10.
//
// CSWMGObstacle::GetName @0x66dc80 reads its visual name by calling
// CAurObject's vtable[0xc]. Retained for the first-fire diagnostic
// dump so the obstacle inventory is still logged on entry.
constexpr size_t kAurVtableSlotGetName          = 0xc;

// ----- Accelerator-pad (CSWMiniEnemy) loop cue -----
//
// Custom resref backed by a trimmed WAV (Override\acc_boost.wav).
// Source: first 0.3 s of mgs_basethrust03 — the engine "base thrust"
// surge with the descending tail removed. The vanilla sample's
// closing half-second slopes down in pitch (reads as the bike
// powering DOWN, opposite of the cue's intent: "boost incoming");
// looping a tail-trimmed version preserves only the rising/sustained
// portion. 0.3 s also fits cleanly inside the per-pad listening
// window (the nearest-only hand-off keeps a cue on each pad ~1-2 s
// before the next pad takes over — see kAccelpadConcurrentLoops).
//
// File ships in Override\ rather than as a renamed engine resource
// so it doesn't collide with any vanilla use of mgs_basethrust03.
// Engine ResLoader checks Override → BIF, so a bare "acc_boost"
// resref resolves to our trimmed sample.
//
// SUPERSEDED candidates (kept for A/B):
//   mgs_basethrust03 — same timbre but the descending tail read as
//                      pitch-drop / wrong direction; trim fixed it
//   mgs_pwrup        — fired clean but timbre wasn't ideal
//   mgs_thrustloop01 — 5+ s sustained drone; only quiet attack got
//                      airtime in the 1-2 s window, read as unhearable
constexpr const char* kAccelpadLoopResref       = "acc_boost";

// Same 200 m horizon as obstacles. 100 m was tested and gave only
// ~0.5 s reaction time at gear 3 (max 190 u/s) — not enough to
// commit to a lane change. The masking concern from the first pass
// (30 concurrent thrust loops drowning obstacles) is already solved
// by the nearest-only policy below; range no longer needs to be a
// secondary mitigation. At 200 m reaction time scales:
//   gear 1 (max 70 u/s):  ~2.86 s
//   gear 2 (max 120 u/s): ~1.67 s
//   gear 3 (max 190 u/s): ~1.05 s
constexpr float       kAccelpadCueRangeM        = 200.0f;

// Only the nearest in-range accelpad fires a loop at any moment.
// Reasoning: the booster soundstage was the noisy half of the first
// pass — 3-4 thrust loops simultaneously masked obstacle cues and
// blurred any individual pad's spatial pan. Single-source keeps the
// booster channel clean and unambiguous. Obstacles keep their
// multi-source pass (they're avoidance cues; missing one is a hit).
constexpr int         kAccelpadConcurrentLoops  = 1;

// Position retrieval for a CSWMiniEnemy (and any CSWTrackFollower):
// the engine's CSWTrackFollower::GetPosition @0x0066d5d0 walks
//   followers->models.data[0] → vtable[+0x64] → returns Vector* world pos
// We replicate that path here. Offset 0x68 is the CExoArrayList<undefined4>
// `models` field in CSWTrackFollower (after the CSWMiniGameObject base
// 0x60 + mini_game ptr 0x4 + field2_0x64 0x4).
constexpr size_t kTrackFollowerModelsDataOffset = 0x68;
constexpr size_t kModelVtableSlotGetPosition    = 0x64;

// ============================================================================
// Continuous obstacle-proximity cue parameters.
// ============================================================================
//
//   kObstacleCueRangeM     — only obstacles within this 3D distance from
//                            the listener fire cues. Sized for ~1-1.3 s
//                            of advance warning at the top observed bike
//                            speed (~190 units/s in gear 3 on Taris), so
//                            the user has time to choose a lane before
//                            the obstacle arrives. Earlier 30 m was sized
//                            like the in-world Pillar-1 walls and gave
//                            only ~150 ms warning — confirmed too late by
//                            the user 2026-05-24.
//
//   kObstacleWarnLoopResref — engine sample played for each obstacle.
//                            mgs_hover_07l (the `l` suffix is the engine
//                            convention for designed-to-loop samples,
//                            same family as mgs_engine_NNl).
//
//   kObstacleForwardMargin — obstacles whose Y (forward axis on the
//                            tunnel track — the bike moves from low Y to
//                            high Y, confirmed live) are dropped if they
//                            sit behind the listener by more than this
//                            margin. Small negative margin allows
//                            immediately-adjacent obstacles (mid-pass) to
//                            keep cueing for one tick so the spatial pan
//                            completes its sweep.
constexpr float       kObstacleCueRangeM     = 200.0f;
constexpr float       kObstacleForwardMargin = 10.0f;
constexpr const char* kObstacleWarnLoopResref = "mgs_hover_07l";
// SUPERSEDED — earlier samples tried:
// "v_dur_shldred"  — Duros voice; routed to voice bus, way too quiet
// "mgs_warnbust"   — one-shot warning used before the loop refactor

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

// ============================================================================
// Module state. Single-threaded under the engine OnUpdate tick.
// ============================================================================

struct SpatialAudioState {
    // Per-slot looping cues. Sized to the global MGO array slot count
    // so we can key by slot without a parallel index map. An MGO slot
    // is either an obstacle or an enemy (the AsXxx vtable downcasts
    // return null for mismatches), never both, so the two arrays don't
    // overlap on any one slot. Auto-cleans at DLL unload via RAII.
    acc::audio::LoopSource obstacle_loops[kMgoArraySlotCount];
    acc::audio::LoopSource accelpad_loops[kMgoArraySlotCount];

    // Diagnostic guards for the first-pass inventory dumps. One log
    // entry per race describing every obstacle / accelpad seen.
    bool obstacle_diag_emitted = false;
    bool accelpad_diag_emitted = false;
};

SpatialAudioState g_state;

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

bool SafeReadVector(void* base, size_t off, Vector& out) {
    if (!base) return false;
    __try {
        Vector* p = reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(base) + off);
        out = *p;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ============================================================================
// MGO array + AsXxx vtable downcasts + per-type position reads.
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

// Read the world position of a CSWTrackFollower (i.e. a CSWMiniEnemy)
// by mirroring CSWTrackFollower::GetPosition @0x0066d5d0:
//   followers->models.data[0] → vtable[+0x64](this, Vector* outBuf)
//     returns Vector*  (typically writes through outBuf, sometimes
//                       returns a pointer to a member Vector)
// Returns false on any null link, empty models array, or SEH fault.
typedef Vector* (__thiscall* PFN_GetPositionThunk)(void* this_, Vector* outBuf);

bool ReadTrackFollowerPosition(void* follower, Vector& out) {
    if (!follower) return false;
    __try {
        // models is a CExoArrayList<undefined4>. data is the first
        // member (offset 0); the array holds 4-byte pointers to model
        // wrapper objects (each with its own vtable).
        void* modelsData = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(follower) +
            kTrackFollowerModelsDataOffset);
        if (!modelsData) return false;
        // First model handle. (size lives at +0x6c; we don't need to
        // read it explicitly — a null data[0] is the empty case.)
        void* model = *reinterpret_cast<void**>(modelsData);
        if (!model) return false;
        void* vtable = *reinterpret_cast<void**>(model);
        if (!vtable) return false;
        void* fn = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vtable) +
            kModelVtableSlotGetPosition);
        if (!fn) return false;
        Vector buf = {0.0f, 0.0f, 0.0f};
        Vector* returned =
            reinterpret_cast<PFN_GetPositionThunk>(fn)(model, &buf);
        out = returned ? *returned : buf;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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
        auto castFn = reinterpret_cast<PFN_AsCast>(fn);
        return castFn(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ============================================================================
// Continuous obstacle-proximity cues.
// ============================================================================

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

    int slots_seen = 0;
    int obstacles_found = 0;
    int obstacles_ahead = 0;
    int obstacles_in_range = 0;
    int loops_started_this_tick = 0;
    int loops_stopped_this_tick = 0;
    // Per-tick "still in range" flag. Slots flipped true here are
    // either Started (loop wasn't active) or just UpdatePosition'd
    // (loop already running). At end of tick, any slot whose loop is
    // active but flag is false has gone out of range and gets Stop'd.
    bool active_this_tick[kMgoArraySlotCount] = {};

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

        // Read the obstacle's name via CAurObject's GetName vtable
        // accessor. Used for the first-fire inventory log; cue routing
        // is no longer name-based (rocks and accelpads are in different
        // engine pools — see the "Object pool split" comment at top).
        const char* name = ReadAurObjectName(aur);

        // First-fire diagnostic: log EVERY obstacle's slot + name so we
        // can see the full per-track inventory in one pass.
        if (!g_state.obstacle_diag_emitted) {
            void* vt = SafeReadPtr(obstacle, 0);
            acclog::Write("SwoopRace",
                          "obstacle slot=%d ptr=%p vtable=%p name=[%s] "
                          "pos=(%.2f,%.2f,%.2f)",
                          i, obstacle, vt, name ? name : "(null)",
                          pos.x, pos.y, pos.z);
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

        // Single cue sample for all obstacles. Accelpads are no longer
        // routed through this sweep (they live in the enemies pool —
        // see TickAccelpadCues).
        const char* resref = kObstacleWarnLoopResref;

        // Project source proportionally so distance encodes via volume
        // + pan rotation rather than a flat sphere. 1:9 compression
        // with 1 m floor — close-pass lands at 1 m (correct direction,
        // no sub-meter pan flip), farthest in-range obstacle (~180 m
        // real) lands at 20 m (engine audibility edge).
        const float dist = (distSq > 0.0f) ? std::sqrt(distSq) : 0.0f;
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

        active_this_tick[i] = true;
        if (g_state.obstacle_loops[i].IsActive()) {
            // Existing loop — just move it.
            g_state.obstacle_loops[i].UpdatePosition(cue_pos);
        } else {
            // First in-range tick for this obstacle — Start the loop.
            if (g_state.obstacle_loops[i].Start(resref, cue_pos)) {
                ++loops_started_this_tick;
                acclog::Trace("SwoopRace",
                              "loop start slot=%d name=[%s] "
                              "obstaclePos=(%.1f,%.1f,%.1f) "
                              "cuePos=(%.1f,%.1f,%.1f) dist=%.1f res=%s",
                              i, name ? name : "(null)",
                              pos.x, pos.y, pos.z,
                              cue_pos.x, cue_pos.y, cue_pos.z,
                              dist, resref);
            }
        }
    }

    // Stop any loops whose slot wasn't in-range this tick. Covers two
    // cases: obstacle passed > kObstacleForwardMargin behind us; or
    // (rarer) obstacle obj/aur/pos read failed this tick after having
    // succeeded last tick. Either way, Stop is idempotent so it's
    // safe to call.
    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        if (!active_this_tick[i] && g_state.obstacle_loops[i].IsActive()) {
            g_state.obstacle_loops[i].Stop();
            ++loops_stopped_this_tick;
            acclog::Trace("SwoopRace",
                          "loop stop slot=%d (out of range / unresolved)", i);
        }
    }

    // Mark diagnostic done AFTER the full sweep, so we get all 22
    // obstacles' names in the log on the first iteration (not just
    // the first one).
    if (!g_state.obstacle_diag_emitted && obstacles_found > 0) {
        g_state.obstacle_diag_emitted = true;
    }

    // Single rolling-state line. inRange = obstacles that triggered
    // a loop Start or UpdatePosition this tick; started/stopped count
    // edge transitions. At steady-state in a long stretch of nearby
    // obstacles, started and stopped should both be small or zero
    // each tick — non-zero values mark transitions.
    acclog::Trace("SwoopRace",
                  "scan: slots=%d obstacles=%d ahead=%d inRange=%d (%.0fm) "
                  "started=%d stopped=%d listenerY=%.1f",
                  slots_seen, obstacles_found, obstacles_ahead,
                  obstacles_in_range, kObstacleCueRangeM,
                  loops_started_this_tick, loops_stopped_this_tick,
                  listener_pos.y);
}

// ============================================================================
// Continuous accelerator-pad proximity cues.
//
// Sibling sweep of TickObstacleCues over the same 255-slot MGO array,
// but downcasting via AsEnemy (vtable[0x1c]) instead of AsObstacle.
// Accelpads are spawned as CSWMiniEnemy with Trigger=1, each riding
// its own track (mgt02..mgt31) — see the "Object pool split" comment
// at top of file.
//
// Same range / forward-margin / 1:9 distance compression as the
// obstacle path so booster and obstacle cues are spatially
// comparable in flight. Different loop sample (acc_boost) so they're
// tonally distinguishable.
//
// Position retrieval differs: enemies don't carry a flat CAurObject
// pointer at +0x60; instead, the world position lives behind the
// first model in their models CExoArrayList, retrieved via
// vtable[+0x64] on that model wrapper. See ReadTrackFollowerPosition.
// ============================================================================

void TickAccelpadCues(void* /*miniGame*/) {
    void* mgoArray = ResolveMgoArray();
    if (!mgoArray) {
        acclog::Trace("SwoopRace", "accelpad cues: mgo array unresolved");
        return;
    }

    Vector listener_pos;
    if (!acc::engine::GetCameraPosition(listener_pos) &&
        !acc::engine::GetPlayerPosition(listener_pos)) {
        acclog::Trace("SwoopRace",
                      "accelpad cues: no listener anchor available");
        return;
    }

    // Single-pass scan. We need (a) the nearest in-range accelpad so
    // we know which slot to loop, and (b) its cue position. Keep the
    // raw pad position too — we recompute the compressed cue position
    // once at the end, both to save work in the hot loop and to keep
    // the resolution path obvious.
    int slots_seen = 0;
    int accelpads_found = 0;
    int accelpads_ahead = 0;
    int accelpads_in_range = 0;

    int   nearest_slot      = -1;
    float nearest_dist_sq   = 0.0f;
    Vector nearest_pos      = {0.0f, 0.0f, 0.0f};

    const float rangeSq = kAccelpadCueRangeM * kAccelpadCueRangeM;

    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        void* obj = SafeReadPtr(mgoArray,
                                kMgoArrayObjectsOffset +
                                static_cast<size_t>(i) * sizeof(void*));
        if (!obj) continue;
        ++slots_seen;

        // AsEnemy returns the same pointer if `obj` is a CSWMiniEnemy.
        // All accelpads classify as enemies; in vanilla swoop tracks
        // there are no non-accelpad enemies, but a model-name filter
        // could be added below if a track ever ships hostile enemies.
        void* enemy = CallAsCast(obj, kVtableSlotAsEnemy);
        if (!enemy) continue;
        ++accelpads_found;

        Vector pos;
        if (!ReadTrackFollowerPosition(enemy, pos)) continue;

        // First-fire diagnostic: log every accelpad's slot + position
        // so we have a per-track inventory similar to the obstacle log.
        if (!g_state.accelpad_diag_emitted) {
            void* vt = SafeReadPtr(enemy, 0);
            acclog::Write("SwoopRace",
                          "accelpad slot=%d ptr=%p vtable=%p "
                          "pos=(%.2f,%.2f,%.2f)",
                          i, enemy, vt, pos.x, pos.y, pos.z);
        }

        // Same forward-only filter as obstacles. Once we've passed an
        // accelpad it can no longer give a boost, so behind-listener
        // pads would just be soundstage noise.
        if (pos.y < listener_pos.y - kObstacleForwardMargin) continue;
        ++accelpads_ahead;

        const float dx = pos.x - listener_pos.x;
        const float dy = pos.y - listener_pos.y;
        const float dz = pos.z - listener_pos.z;
        const float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > rangeSq) continue;
        ++accelpads_in_range;

        // Track nearest only — see kAccelpadConcurrentLoops rationale.
        if (nearest_slot < 0 || distSq < nearest_dist_sq) {
            nearest_slot    = i;
            nearest_dist_sq = distSq;
            nearest_pos     = pos;
        }
    }

    // ---- Apply: at most one loop active, on the nearest slot. -----------
    int loops_started_this_tick = 0;
    int loops_stopped_this_tick = 0;

    // Stop any active loop that isn't the current nearest. Covers the
    // hand-off case where the previous-tick nearest just got passed
    // and the new nearest is a different slot.
    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        if (i == nearest_slot) continue;
        if (g_state.accelpad_loops[i].IsActive()) {
            g_state.accelpad_loops[i].Stop();
            ++loops_stopped_this_tick;
            acclog::Trace("SwoopRace",
                          "accelpad loop stop slot=%d "
                          "(no longer nearest)", i);
        }
    }

    if (nearest_slot >= 0) {
        // Compress the nearest pad's position onto the engine's 5-20 m
        // audibility band, same 1:9 ratio + 1 m floor used for obstacles.
        const float dist = std::sqrt(nearest_dist_sq);
        Vector cue_pos = nearest_pos;
        if (dist > 0.0f) {
            float compressed = dist * kObstacleDistanceCompression;
            if (compressed < kObstacleMinSourceDistanceM) {
                compressed = kObstacleMinSourceDistanceM;
            }
            const float k = compressed / dist;
            const float dx = nearest_pos.x - listener_pos.x;
            const float dy = nearest_pos.y - listener_pos.y;
            const float dz = nearest_pos.z - listener_pos.z;
            cue_pos.x = listener_pos.x + dx * k;
            cue_pos.y = listener_pos.y + dy * k;
            cue_pos.z = listener_pos.z + dz * k;
        }

        if (g_state.accelpad_loops[nearest_slot].IsActive()) {
            g_state.accelpad_loops[nearest_slot].UpdatePosition(cue_pos);
        } else {
            if (g_state.accelpad_loops[nearest_slot].Start(
                    kAccelpadLoopResref, cue_pos)) {
                ++loops_started_this_tick;
                acclog::Trace("SwoopRace",
                              "accelpad loop start slot=%d "
                              "padPos=(%.1f,%.1f,%.1f) "
                              "cuePos=(%.1f,%.1f,%.1f) dist=%.1f res=%s",
                              nearest_slot,
                              nearest_pos.x, nearest_pos.y, nearest_pos.z,
                              cue_pos.x, cue_pos.y, cue_pos.z,
                              dist, kAccelpadLoopResref);
            }
        }
    }

    if (!g_state.accelpad_diag_emitted && accelpads_found > 0) {
        g_state.accelpad_diag_emitted = true;
    }

    acclog::Trace("SwoopRace",
                  "accelpad scan: slots=%d accelpads=%d ahead=%d inRange=%d "
                  "(%.0fm) nearest=%d started=%d stopped=%d listenerY=%.1f",
                  slots_seen, accelpads_found, accelpads_ahead,
                  accelpads_in_range, kAccelpadCueRangeM,
                  nearest_slot,
                  loops_started_this_tick, loops_stopped_this_tick,
                  listener_pos.y);
}

}  // namespace

// ============================================================================
// Public entry points (declared in swoop_spatial_audio.h).
// ============================================================================

void TickSpatialAudio(void* miniGame) {
    TickObstacleCues(miniGame);
    TickAccelpadCues(miniGame);
}

void ResetSpatialAudio() {
    g_state.obstacle_diag_emitted = false;
    g_state.accelpad_diag_emitted = false;
    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        g_state.obstacle_loops[i].Stop();
        g_state.accelpad_loops[i].Stop();
    }
}

}  // namespace acc::swoop_race
