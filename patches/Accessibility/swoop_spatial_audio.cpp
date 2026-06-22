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

#include "audio_bus.h"        // PlayCue3D — co-pilot directional / aligned
                              //     one-shots (discrete steering commands)
#include "audio_cues.h"       // NavCue + GetNavCueResref (centralised
                              //     resref vocabulary — Audio glossary
                              //     and live race fire the same samples)
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
constexpr const char* kAccelpadLoopResref =
    acc::audio::GetNavCueResref(acc::audio::NavCue::SwoopAccelpadBoost);

// Same 300 m horizon as obstacles (raised from 200 m on 2026-06-20 —
// 200 m gave too little time to "pendel in" on the pan at gear 3). The
// masking concern from the first pass (30 concurrent thrust loops
// drowning obstacles) is already solved by the nearest-only policy
// below; range no longer needs to be a secondary mitigation. Reaction
// time (first-audible lead) at 300 m:
//   gear 1 (max 70 u/s):  ~4.3 s
//   gear 2 (max 120 u/s): ~2.5 s
//   gear 3 (max 190 u/s): ~1.6 s
// NOTE: kForwardCompression is tied to this — it maps this range onto
// the band floor (kSwoopCueMinVolDistM). Change both together.
constexpr float       kAccelpadCueRangeM        = 300.0f;


// Accelpads use the SAME multi-source pass as obstacles — every in-range
// pad gets its own continuously-playing loop.
//
// SUPERSEDED — nearest-only (one loop at a time). It was adopted because
// the first multi-pad pass blurred: under the old UNIFORM 1:9 compression
// lateral pan was squashed to ±2 m, so 3-4 pads all sounded centred and
// smeared together. With anisotropic projection (true lateral) each pad
// now pans to its real lane, so concurrent pads are spatially distinct.
// Nearest-only also caused the audible cue to teleport side-to-side at
// every hand-off and gave no approach ramp (a pad popped in already at
// full volume at ~112 m). Per-slot loops fix both: a passed pad dies
// behind you, a new one fades in faint at the far edge, and the pads
// between play continuously — confirmed via patch-20260620-114139.log
// analysis. If the stage gets cluttered, cap to the nearest N (the band
// already floors distant pads, so a cap loses little).

// Position retrieval for a CSWMiniEnemy (and any CSWTrackFollower):
// the engine's CSWTrackFollower::GetPosition @0x0066d5d0 walks
//   followers->models.data[0] → vtable[+0x64] → returns Vector* world pos
// We replicate that path here. Offset 0x68 is the CExoArrayList<undefined4>
// `models` field in CSWTrackFollower (after the CSWMiniGameObject base
// 0x60 + mini_game ptr 0x4 + field2_0x64 0x4).
constexpr size_t kTrackFollowerModelsDataOffset = 0x68;
constexpr size_t kModelVtableSlotGetPosition    = 0x64;

// ----- Lateral-pan diagnostic (added 2026-06-20) -----
//
// To tune the accelpad pan we need the lateral geometry the engine
// actually pans on, none of which was in the log before (pan tuning was
// blind): the listener (camera) X, the bike's own world X, the player's
// tunnel-frame lane X, and the nearest-ahead pad's lateral error +
// resulting pan angle. The open question this answers: does the chase
// camera strafe sideways with the bike — so aligning the bike centres
// the cue — or stay near lane centre, so steering barely moves the pan?
// Compare listenerX against bikeWorldX across a run: if they track, the
// pan reference is sound and the problem is the sample / concurrency; if
// listenerX stays put while bikeWorldX swings, the pan can never centre.
//
// Engine confirmed (docs/llm-docs/swoop-accelpad-hit-model.md): hit band
// is player.Sphere_Radius + pad.Sphere_Radius (Taris 6.0, Tatooine /
// Manaan 5.0) in world units; world-X tracks tunnel-X 1:1 (lane ±20).
// CSWMiniGame.player at +0x24; CSWMiniPlayer.offset (tunnel lane coord,
// x = lateral) at +0x1c4.
constexpr size_t kMiniGamePlayerOffset          = 0x24;
constexpr size_t kMiniPlayerOffsetVectorOffset  = 0x1c4;
constexpr float  kRadToDeg                       = 57.2957795f;

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
constexpr float       kObstacleCueRangeM     = 300.0f;
constexpr float       kObstacleForwardMargin = 10.0f;
constexpr const char* kObstacleWarnLoopResref =
    acc::audio::GetNavCueResref(acc::audio::NavCue::SwoopObstacleWarn);
// SUPERSEDED — earlier samples tried:
// "v_dur_shldred"  — Duros voice; routed to voice bus, way too quiet
// "mgs_warnbust"   — one-shot warning used before the loop refactor

// ----- Decoupled cue: lateral→pan, real distance→loudness -----
//
// The classic audiogame "one looping sound that gets louder and pans"
// works only when the two facts it carries land on SEPARATE, each-
// perceptible channels. With a plain 3D source they don't: pan and
// loudness both fall out of the single position, so forward distance
// (huge) governs both and the left/right offset (small) becomes an
// inaudible ~2° whisper. We split them:
//
//   1. PAN (which lane / how far to steer) ← LATERAL only.
//      The cue position keeps lateral (X) and vertical (Z) true, but
//      replaces the forward (Y) component with a FIXED reference depth
//      (kSwoopPanForwardRefM). So pan = atan(lateral / fixed_ref): it
//      depends ONLY on your lane error, scaled so a quarter-lane offset
//      reads as a clear pan. Crucially the reference is CONSTANT, not the
//      real (or a compressed real) distance — so the pan is stable: it
//      moves only when you steer or the pad's lane differs from yours,
//      never lurching as the pad passes. That kills the overshoot the
//      forward-compression builds had (pan inflating as forward shrank).
//
//   2. LOUDNESS (how soon / approach speed) ← REAL distance.
//      Driven by us via LoopSource::UpdateVolume on a linear curve over
//      the real 3D distance (full up close → kSwoopVolFarByte at the
//      300 m edge). Keeps the full distance cue, including speed: the
//      faster you close, the faster it swells. The FLAT BAND below makes
//      the engine apply full volume regardless of the source's (now
//      fixed-depth, hence near) position, so our manual curve is the sole
//      loudness control. Pan is unaffected by the band — only attenuation
//      magnitude is, and we want that flat.
//
//   kSwoopPanForwardRefM — fixed depth the pan is read against. Smaller =
//      stronger pan (a given lane offset reads as a wider angle), AND a
//      steeper gradient near centre — which is the point. 6 m (was 12 m,
//      tuned down 2026-06-20): the pan-diagnostic run showed ~10 of 22
//      pad misses were undershoots, stopping 6-11 units short because at
//      12 m "sounds centred" spanned wider than the ±5 unit catch band
//      (5 units off was only ~24°). At 6 m the same 5-unit error reads
//      ~40° and 1 unit reads ~9.5° (vs 4.8° before) — roughly double the
//      near-centre sensitivity, so the band between "aligned" and "close"
//      is far more audible. Shared by obstacle + accelpad cues so the two
//      stay on one coherent projection (an obstacle and a pad at the same
//      lateral offset pan identically). Trade-off: far targets saturate to
//      hard L/R sooner, which is fine — out there you only need "go right",
//      the fine resolution that matters is near the catch band.
//   kSwoopFlatBandMaxM / kSwoopFlatBandMinM — both past any source's
//      fixed-depth distance, so engine attenuation is flat (full).
//   kSwoopVolNearByte / kSwoopVolFarByte — linear loudness ramp endpoints:
//      near → full, far (dist = cue range) → ~⅓. Raise the far byte if
//      distant pads are too quiet to localise; lower it for more contrast.
//
// SUPERSEDED:
//   - 1:9 UNIFORM compression — squashed lateral pan to ±2 m, too weak to
//     hit pads by intent (patch-20260620-104051.log).
//   - TRUE position + 5/200 band — too quiet (patch-20260620-111927.log).
//   - Forward 1:10 + 3/30 band — nearly silent (max_vol too small).
//   - Forward 1:15 + 15/30 band — loud, but compression inflated the pan
//     angle → overshoot ("steer, hit wall, it passes").
//   - TRUE position + flat band + manual volume — honest, no overshoot,
//     but true pan made far targets a ~2° whisper: not localisable
//     (patch-20260620-122348.log). Hence the fixed-depth pan reference.
constexpr float       kSwoopPanForwardRefM = 6.0f;
constexpr float       kSwoopFlatBandMaxM   = 350.0f;
constexpr float       kSwoopFlatBandMinM   = 400.0f;
constexpr int         kSwoopVolNearByte    = 127;
constexpr int         kSwoopVolFarByte     = 50;

// ----- Steering-guide controller (2026-06-20 rework) ------------------------
//
// The guide is the SAME panned tone as before (acc_boost, kAccelpadLoopResref)
// — what changed is HOW its pan setpoint is computed: a look-ahead pure-pursuit
// controller (below) instead of the old "line at the bike's current depth",
// which overshot.
//
// Why not the game's own engine hum (the SpaghettiKart design): it is not a
// reachable engine-owned source. RE this session found the bike's engine loop
// would live at player+0x144 (CSWTrackFollower "Engine" slot, SetSoundName/
// Set3D), BUT the swoop Player's <Engine> resref is EMPTY in all three area
// GFFs (m03/m17/m26 — only the pads carry a Death sound), so SetSoundName makes
// no source and player+0x144 is always null (confirmed live: src=0/playing=-1
// across a whole race, patch-20260620-160229.log). The hum you hear is a
// model-attached sound the engine moves with the bike, not a follower source
// we can address — so we keep panning our own acc_boost tone instead.

// The pads are scattered GATES (they alternate nearly full-lane: 112→81→119→
// 97…), not a smooth racing line — so we aim straight at the next pad's lane,
// not at an interpolated line between pads (that points at the mid-lane, the
// WRONG way; it sent the guide opposite the gate — patch-20260620-212526.log).
//
// The bike has real lateral inertia (engine CSWMiniPlayer::DoInertia), but the
// cue does NOT try to predict around it (see below) — predicting off a noisy
// velocity pulled the bike off pads it had reached.
//
// PD cue (2026-06-21): err = padErr − kSteerLeadTicks·velocity, where padErr =
// padLane − bikeX. The velocity term is the BRAKE — as you build speed toward a
// pad it pushes err the OTHER way ("counter-steer now"), so you bleed off the
// bike's lateral momentum before you arrive instead of sailing past into the
// wall. The brake is ALLOWED to reverse sign; that reversal IS the cue to stop
// overshooting. (An earlier anti-reversal clamp suppressed exactly this and the
// bike overshot to the wall even at crawl speed — ...222112.log first-pad trace.)
//
// The thing that must NOT happen is the brake pulling you off a pad you're
// PARKED on. That's handled by the settle gate: it fires only when you are on
// the lane AND barely moving (kSteerSettleVel). Centred-but-still-sliding does
// NOT settle, so the brake survives when you need it. (The prior versions
// settled on centre alone, which is why the brake was lost mid-slide.)
constexpr float kSteerLeadTicks  = 5.0f;
// EMA on the noisy per-tick velocity. Raised 0.30->0.50 (2026-06-22, Flaw 5):
// with the coast lead cut to ~1 tick (kCoastTicks), velocity no longer feeds a
// x8 amplifier, so the heavy smoothing that was hiding per-tick noise is no
// longer needed — and its ~3-tick lag hurt responsiveness more than the noise.
constexpr float kSteerVelSmooth  = 0.50f;
constexpr float kSteerVelClamp   = 8.0f;   // reject one-tick bike-X glitches
// Low-pass on the panned offset. snap on pad handoff (below) avoids carry-over.
constexpr float kSteerPanSmooth  = 0.45f;
// Settle gate: within this lateral error of the lane AND below this lateral
// speed, the cue reads dead-centre so you can hold it. 1.5 u is well inside the
// 5-6 u catch radius; 0.4 u/tick is "barely drifting" (active steering is ~1-2).
constexpr float kSteerDeadzoneUnits = 1.5f;
constexpr float kSteerSettleVel     = 0.4f;
// Clamp on the steer error (the panned magnitude) and on how far a pad may sit
// laterally from the bike to count as real. The lane is only ~±20 wide, so a
// pad more than 60 u to the side is a junk read (one came back at X=1933 and
// poisoned the smoothed pan to 1695 for ~12 ticks). Both are hard guards.
constexpr float kSteerMaxErr     = 40.0f;
constexpr float kMaxPadLateral   = 60.0f;

// ----- Co-pilot discrete-command steering (experimental, 2026-06-21) --------
//
// The continuous panned guide (above) made the PLAYER the controller of a
// high-inertia, delayed plant — and that loop is unstable: the 2026-06-20
// 222843 log shows the bike overshooting and oscillating around even the easy
// first pad (reached the lane ~0.75 s early, then sailed 6 units past it). No
// re-encoding of "where to go" fixes that, because the instability is in the
// human-in-the-loop, not the information.
//
// The co-pilot takes the control DECISION off the player. Code runs the
// controller every tick and issues discrete, pre-timed commands:
//   coast = smoothed_vel * kCoastTicks  — where the bike drifts to if you
//                                         RELEASE the steer key now (your
//                                         reaction delay + the bike's inertial
//                                         stop, bundled into one lead term).
//   proj  = padErr - coast              — the lane miss AFTER that coast.
//   |proj| <= kReleaseBand → ALIGNED (release now; you coast onto the pad).
//   proj > 0  → steer RIGHT;  proj < 0 → steer LEFT.
// The player does only coarse hold/release; code owns the hard part (WHEN to
// stop). The loop stays closed in code (real bike X re-read each tick), so an
// early/late reaction just re-issues the command — robust to coast-model error.
enum {
    kSteerCmdLeft    = -1,
    kSteerCmdNone    = 0,
    kSteerCmdRight   = 1,
    kSteerCmdAligned = 2,
};
// Lead term, in ticks of current lateral velocity — predicts where the bike
// drifts if you release the steer key now.
//
// Engine grounding (decompiled 2026-06-22, CSWMiniPlayer::{AdjustPosition,
// Control,DoInertia,KeepInTunnel}): swoop steering is a BANK LEVEL clamped to
// [-10,+10] (AdjustPosition.field10_0x1d0), NOT a momentum accumulator, and the
// only lateral "inertia" is collision-plane bump inertia (DoInertia, gated on
// CSWMiniGame::use_inertia and only non-zero at a wall). Mid-lane there is
// essentially no momentum to coast on. Live traces confirm it: lateral velocity
// reverses in ~5-8 ticks and the real post-release coast is ~1 unit — whereas
// the old vel*8 model predicted ~13 (patch-20260622 slot 26: predicted 12.8 u,
// actual ~1 u). That ~10x overestimate fired the "aligned/release" cue 10-20 u
// short of the pad on 29% of gates — the dominant miss cause (Flaw 1).
//
// So this is a small velocity lead, NOT a stopping-distance estimate. RAISE a
// little if the bike still overshoots; do NOT restore the old large value.
//
// Probe 2026-06-22: raised 1.0 -> 2.5. With 1.0 the controller held "steer" all
// the way to ~2u and the bike overshot to the wall on 32% of ticks (up from
// 22%). Releasing a bit earlier should cut the wall-slamming and still land
// inside the 8u catch (real coast ~1u, so an aligned call near ~6u still lands
// ~5u). If this reintroduces undershoot, step back toward ~1.5.
constexpr float       kCoastTicks    = 2.5f;
// ----- Predictive overshoot / wall cue (2026-06-22) -------------------------
// Fire the existing wall-impact sound ~kWallLeadTicks BEFORE the bike pins,
// instead of on contact, so the player starts re-steering that much sooner (you
// can't out-react the pin, but you waste less time stuck against the wall). Lead
// is small on purpose: at top lateral speed the 40u lane crosses in ~0.3s, so a
// big lead would fire across most of the lane. ~3 ticks ≈ 0.1s. The cue is
// guarded to a genuine OVERSHOOT (moving away from the target pad toward the
// wall) so it doesn't cry wolf on the ~38% of pads that legitimately sit at the
// edges — that guard is one sign comparison, not a new subsystem.
constexpr float       kWallLeadTicks    = 3.0f;   // ticks of velocity look-ahead (~0.1s)
constexpr float       kWallEdgeOffsetU  = 20.0f;  // lane half-width in tunnel-offset units
constexpr float       kWallMinVel       = 0.5f;   // ignore slow drift (u/tick)
constexpr ULONGLONG   kWallCueDebounceMs = 400;   // don't machine-gun while pinned
constexpr float       kWallCuePanM      = 5.0f;   // ±pan of the cue to the wall side
constexpr const char* kWallCueResref =
    acc::audio::GetNavCueResref(acc::audio::NavCue::SwoopWallImpact);
// Combined accel-pad hit radius (player.Sphere_Radius + pad.Sphere_Radius), used
// ONLY for the cosmetic hit= flag on the CROSS scoring line. It is track- and
// mod-dependent: stock Taris = 6, stock Tatooine/Manaan = 5; our widened
// Tatooine .are makes it 8 (player 3 + pad 5). Set to the current test target so
// the log flag matches reality — re-scoring at other radii is still a one-liner
// over the abs= field. (The engine's real test isn't read here; see
// swoop-accelpad-hit-model.md.)
constexpr float       kAccelpadHitRadiusU = 8.0f;
// Post-coast miss within this band reads as "on the line" → release/hold. Kept
// inside the pad catch radius (~5-6 u) so a released coast still lands a hit.
constexpr float       kReleaseBand   = 4.0f;
// Directional-tick geometry (camera-relative, mirroring the proven wall-impact
// pan path) and cadence. Lateral ±pan at a slight forward depth gives a hard
// but slightly-ahead L/R image (atan2(8,2) ≈ 76°), and the 3D distance
// (≈8.3 m) sits in the audible 5-10 m band — cues closer than ~5 m attenuate
// oddly (see the wall-impact notes: 3 m was inaudible).
constexpr float       kSteerTickPanM    = 8.0f;
constexpr float       kSteerTickFwdM    = 2.0f;
// The aligned/release blip is centred (no pan), so it needs real forward depth
// to clear the near-field dead zone; 7 m keeps it audible and within range.
constexpr float       kSteerAlignedFwdM = 7.0f;
constexpr ULONGLONG   kSteerTickMs      = 160;
// Directional ticks carry the side in BOTH pitch (low=left, high=right) and
// pan, so a missed pan read is backed up by pitch. Aligned is a centred rising
// tone. All three are loud custom WAVs — see audio_cues.h.
constexpr const char* kSteerLeftResref =
    acc::audio::GetNavCueResref(acc::audio::NavCue::SwoopSteerLeft);
constexpr const char* kSteerRightResref =
    acc::audio::GetNavCueResref(acc::audio::NavCue::SwoopSteerRight);
constexpr const char* kSteerAlignedResref =
    acc::audio::GetNavCueResref(acc::audio::NavCue::SwoopSteerAligned);
// Per-cue base volume (0..127). The synthesised tones are hot (RMS ~44-48%);
// 96 ≈ 75% trims them from "a bit too loud" without losing them in the race
// mix. The global cue slider still scales this.
constexpr uint8_t     kSteerCueVolume = 96;

// ----- Next-gate preview (2026-06-21) ---------------------------------------
//
// The geometry proves the gates are reachable (worst 39 u lateral jump needs
// ~7 ticks; the forward gap gives 8-18) — what kills the tight ones is that the
// co-pilot only revealed the next gate AT the handoff, so the player's ~6-tick
// reaction delay ate the front of the window. Preview fixes that the way a
// sighted player does: a soft, single heads-up of the FOLLOWING gate's
// direction, fired once you're settled on the current gate, so the direction is
// pre-loaded before the loud steer-now ticks start. Same pitch language (low =
// left, high = right) but quieter — "get ready", not "act".
constexpr float       kPreviewLeadU  = 120.0f;  // announce next gate when current
                                                //   is within this far ahead
constexpr uint8_t     kPreviewVolume = 55;      // softer than the steer-now ticks

// Linear volume byte for a source at 3D distance `dist` within `range`:
// full (kSwoopVolNearByte) at the listener, ramping to kSwoopVolFarByte
// at the range edge. Clamped to the endpoints outside [0, range].
inline int SwoopVolumeByte(float dist, float range) {
    if (range <= 0.0f) return kSwoopVolNearByte;
    float t = dist / range;                 // 0 near .. 1 far
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    int vb = static_cast<int>(
        kSwoopVolNearByte + t * (kSwoopVolFarByte - kSwoopVolNearByte) + 0.5f);
    if (vb < kSwoopVolFarByte)  vb = kSwoopVolFarByte;
    if (vb > kSwoopVolNearByte) vb = kSwoopVolNearByte;
    return vb;
}

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

    // The panned steering-guide tone (acc_boost). Pan setpoint = the look-ahead
    // pure-pursuit controller in TickAccelpadCues; centred = on the line.
    acc::audio::LoopSource accelpad_line_loop;

    // Low-pass state for the panned steering offset (eased toward the damped,
    // deadzoned err each tick; snapped on pad handoff).
    float smoothed_steer_err = 0.0f;
    // Lateral-velocity tracking for the PD damping term: bike world X last tick
    // and the smoothed per-tick lateral velocity.
    float prev_bike_x      = 0.0f;
    bool  have_prev_bike_x = false;
    float smoothed_vel     = 0.0f;
    // The pad slot targeted last tick — when it changes (a pad crossed) we snap
    // the pan instead of easing, so no stale carry-over into the next approach.
    int   prev_ahead_slot    = -1;

    // Co-pilot command state (experimental). steer_cmd is the last command
    // issued (kSteerCmd*); the aligned blip fires once on a steering→aligned
    // transition. last_steer_tick_ms paces the repeating directional tick.
    int       steer_cmd          = kSteerCmdNone;
    ULONGLONG last_steer_tick_ms = 0;

    // Next-gate preview: the ahead_slot we've already announced the follow-on
    // gate for, so the heads-up fires once per gate.
    int       previewed_slot     = -1;

    // Predictive overshoot/wall cue debounce: last time we fired the early
    // wall-impact sound, so it doesn't machine-gun while the bike sits pinned.
    ULONGLONG last_wall_cue_ms    = 0;

    // Crossing-event scoring: last tick's targeted gate + its lateral miss and
    // forward gap, so when the target changes we can interpolate the EXACT
    // lateral miss at fwdGap=0 (ground-truth hit, vs the noisy periodic copilot
    // samples). prev_ahead_slot above is reused as the gate id.
    bool      have_prev_ahead    = false;
    float     prev_ahead_posX    = 0.0f;
    float     prev_ahead_posY    = 0.0f;
    float     prev_ahead_padErr  = 0.0f;
    float     prev_ahead_fwdGap  = 0.0f;

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

        // Decoupled cue: lateral (X) + vertical (Z) true, forward (Y)
        // pinned to a FIXED reference depth → pan encodes lane error only,
        // stable. Loudness comes from our manual curve on the REAL 3D
        // distance. See the design comment above.
        const float dist = (distSq > 0.0f) ? std::sqrt(distSq) : 0.0f;
        Vector cue_pos = pos;
        cue_pos.y = listener_pos.y + kSwoopPanForwardRefM;
        const int vol_byte = SwoopVolumeByte(dist, kObstacleCueRangeM);

        active_this_tick[i] = true;
        if (g_state.obstacle_loops[i].IsActive()) {
            // Existing loop — move it and re-assert the distance volume.
            g_state.obstacle_loops[i].UpdatePosition(cue_pos);
            g_state.obstacle_loops[i].UpdateVolume(vol_byte);
        } else {
            // First in-range tick for this obstacle — Start the loop with
            // the flat band so only our manual volume modulates loudness.
            if (g_state.obstacle_loops[i].Start(
                    resref, cue_pos, /*looping=*/true, /*spatial=*/true,
                    /*priorityGroup=*/-1, /*volumeByte=*/vol_byte,
                    /*maxVolDist=*/kSwoopFlatBandMaxM,
                    /*minVolDist=*/kSwoopFlatBandMinM)) {
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
// Accelerator-pad STEERING GUIDE — panned tone, look-ahead pure-pursuit.
//
// A single continuous tone (acc_boost) panned toward the side to steer, so you
// drive TOWARD the sound — the centering paradigm real racing-game a11y mods
// use (SpaghettiKart / "Super Blind Kart"). We pan our OWN tone rather than the
// game's bike hum because that hum is not a reachable engine source (the swoop
// Player's <Engine> resref is empty in every area GFF, so player+0x144 is null
// — see the constants block above).
//
// The pads are scattered GATES, not a smooth racing line (they alternate
// nearly full-lane). The setpoint targets the NEXT pad with a PD (proportional-
// derivative) controller:
//   1. Pick the next gate: nearest pad ahead whose lateral position is sane
//      (outlier guard — a junk X=1933 read once poisoned the pan for a second).
//   2. padErr = padLane − bikeX (proportional). err = padErr − lead·velocity
//      (the derivative is a BRAKE: it counter-steers you to bleed off lateral
//      momentum before you overshoot). A settle gate zeroes it only when you're
//      on the lane AND barely moving, so the brake survives mid-slide but you
//      can still park. See the constants block for why the gate (not a reversal
//      clamp) is the crux.
//   3. Pan the tone to that offset against a fixed depth; centred = on the lane,
//      and it STAYS centred so you can hold it. World +X is listener-right.
//
// Pad world positions come from each CSWMiniEnemy's first model via
// vtable[+0x64] (ReadTrackFollowerPosition); pads are reached via the AsEnemy
// (vtable[0x1c]) downcast.
//
// One dead end we backed out of: an interpolated pad-to-pad line — the pads
// alternate full-lane, so the line ran through the mid-lane and pointed the
// guide at the WRONG side (...212526.log). The first PD attempts also failed,
// but for a fixable reason (no anti-reversal/settle clamp → it reversed you off
// pads you sat on); the clamps above are that fix.
//
// Honest limit: the steepest alternating-lane gates are a near-full-lane swing
// in well under a second — at the physical edge of the inert bike, so some get
// cut like a sighted player cuts them. The damping is tuned (kSteerLeadTicks)
// to convert the medium pads without breaking settle. Levers: kSteerLeadTicks
// (more damping = ease earlier), kSteerPanSmooth, kSteerDeadzoneUnits.
// ============================================================================

void TickAccelpadCues(void* miniGame) {
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

    // Bike world position (player follower). The line is evaluated at the
    // bike's own forward Y so the lateral target is "where you should be
    // right now"; the camera (listener) sits a little behind, but its X
    // tracks the bike's to ~0.04 units (verified), so it's the pan anchor.
    void* player = SafeReadPtr(miniGame, kMiniGamePlayerOffset);
    Vector bikePos;  const bool bikeOk = ReadTrackFollowerPosition(player, bikePos);
    Vector tunnel;   const bool tunOk  =
        SafeReadVector(player, kMiniPlayerOffsetVectorOffset, tunnel);
    const float refY = bikeOk ? bikePos.y : listener_pos.y;

    int slots_seen = 0;
    int accelpads_found = 0;
    int accelpads_ahead = 0;

    // The gate to aim at = next pad to cross: the smallest-Y pad strictly ahead
    // whose lateral position is plausible (within kMaxPadLateral of the bike).
    // The outlier guard drops junk reads (one pad came back at X=1933).
    const float bikeXref = bikeOk ? bikePos.x : listener_pos.x;
    int   ahead_slot = -1;  float ahead_y = 0.0f;  Vector ahead_pos = {0,0,0};
    // Second-nearest gate ahead — drives the preview heads-up.
    int   next_slot  = -1;  float next_y  = 0.0f;  Vector next_pos  = {0,0,0};

    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        void* obj = SafeReadPtr(mgoArray,
                                kMgoArrayObjectsOffset +
                                static_cast<size_t>(i) * sizeof(void*));
        if (!obj) continue;
        ++slots_seen;

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

        if (pos.y < refY) continue;                          // behind us
        if (std::fabs(pos.x - bikeXref) > kMaxPadLateral) continue;  // junk read
        ++accelpads_ahead;
        if (ahead_slot < 0 || pos.y < ahead_y) {
            // New nearest gate — demote the old nearest to second.
            next_slot = ahead_slot; next_y = ahead_y; next_pos = ahead_pos;
            ahead_slot = i; ahead_y = pos.y; ahead_pos = pos;
        } else if (next_slot < 0 || pos.y < next_y) {
            next_slot = i; next_y = pos.y; next_pos = pos;
        }
    }

    if (!g_state.accelpad_diag_emitted && accelpads_found > 0) {
        g_state.accelpad_diag_emitted = true;
    }

    // ----- Lateral velocity (units/tick), smoothed + clamped. -----
    if (bikeOk) {
        if (g_state.have_prev_bike_x) {
            float v = bikePos.x - g_state.prev_bike_x;
            if (v >  kSteerVelClamp) v =  kSteerVelClamp;
            if (v < -kSteerVelClamp) v = -kSteerVelClamp;
            g_state.smoothed_vel += (v - g_state.smoothed_vel) * kSteerVelSmooth;
        }
        g_state.prev_bike_x = bikePos.x;
        g_state.have_prev_bike_x = true;
    }

    // ----- Crossing-event log (ground-truth hit scoring). -----
    // When the targeted gate changes, the previous gate has just passed. We have
    // its last in-front sample (prev_ahead_fwdGap > 0, prev_ahead_padErr) and we
    // can recompute its geometry from THIS tick's bike position (fwdGap now <= 0)
    // — so interpolate the exact lateral miss at fwdGap=0. That is the engine's
    // real hit test (|miss| <= ~6 on Taris), unambiguous, unlike the periodic
    // copilot samples that land ~4 u before the true crossing.
    if (bikeOk && g_state.have_prev_ahead &&
        ahead_slot != g_state.prev_ahead_slot && g_state.prev_ahead_slot >= 0) {
        const float fwdGap_now = g_state.prev_ahead_posY - refY;
        const float padErr_now = g_state.prev_ahead_posX - bikePos.x;
        if (g_state.prev_ahead_fwdGap > 0.0f &&
            fwdGap_now < g_state.prev_ahead_fwdGap) {
            float t = g_state.prev_ahead_fwdGap /
                      (g_state.prev_ahead_fwdGap - fwdGap_now);
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            const float crossErr =
                g_state.prev_ahead_padErr +
                (padErr_now - g_state.prev_ahead_padErr) * t;
            const float acrx = crossErr < 0.0f ? -crossErr : crossErr;
            acclog::Write("SwoopRace",
                          "CROSS slot=%d padX=%.2f crossErr=%.2f abs=%.2f "
                          "hit=%d vel=%.2f",
                          g_state.prev_ahead_slot, g_state.prev_ahead_posX,
                          crossErr, acrx, (acrx <= kAccelpadHitRadiusU) ? 1 : 0,
                          g_state.smoothed_vel);
        }
    }

    // ----- Co-pilot command (experimental). -----
    // padErr/fwdGap are always the TRUE nearest-gate values — scoring (the CROSS
    // interpolation) and the preview both rely on them, so they must not move.
    // coast = where the bike drifts if you release now. proj = post-coast miss.
    const bool have_target = (ahead_slot >= 0) && bikeOk;
    const float padErr = have_target ? (ahead_pos.x - bikePos.x) : 0.0f;
    const float fwdGap = have_target ? (ahead_pos.y - refY) : -1.0f;

    int   desired = kSteerCmdNone;
    float coast   = 0.0f;
    float proj    = 0.0f;
    if (have_target) {
        coast = g_state.smoothed_vel * kCoastTicks;
        proj  = padErr - coast;
        if (proj >  kReleaseBand)      desired = kSteerCmdRight;
        else if (proj < -kReleaseBand) desired = kSteerCmdLeft;
        else                            desired = kSteerCmdAligned;
    }

    // ----- Render the command as discrete one-shots. -----
    // Directional ticks repeat at kSteerTickMs (panned hard to the side to
    // steer) so "keep going this way" is reinforced; they fire IMMEDIATELY on a
    // direction change so a correction isn't delayed by the cadence. Silence =
    // hold / you're on the line. One aligned blip marks the release moment — the
    // most time-critical signal, so it rides the lowest-latency cue and is
    // followed by silence.
    const ULONGLONG now = GetTickCount64();
    bool tick_fired = false;
    if (desired == kSteerCmdLeft || desired == kSteerCmdRight) {
        const bool dir_changed = (desired != g_state.steer_cmd);
        if (dir_changed || now - g_state.last_steer_tick_ms >= kSteerTickMs) {
            Vector cue = listener_pos;
            cue.x = listener_pos.x +
                    (desired == kSteerCmdRight ? +kSteerTickPanM : -kSteerTickPanM);
            cue.y = listener_pos.y + kSteerTickFwdM;
            cue.z = listener_pos.z;
            acc::audio::PlayCue3D(
                desired == kSteerCmdRight ? kSteerRightResref : kSteerLeftResref,
                cue, /*priorityGroup=*/0, kSteerCueVolume);
            g_state.last_steer_tick_ms = now;
            tick_fired = true;
        }
    } else if (desired == kSteerCmdAligned) {
        if (g_state.steer_cmd == kSteerCmdLeft ||
            g_state.steer_cmd == kSteerCmdRight) {
            Vector cue = listener_pos;          // centred — no pan
            cue.y = listener_pos.y + kSteerAlignedFwdM;
            acc::audio::PlayCue3D(kSteerAlignedResref, cue,
                                  /*priorityGroup=*/0, kSteerCueVolume);
        }
    }
    g_state.steer_cmd = desired;

    // ----- Predictive overshoot / wall cue. -----
    // Fire the wall-impact sound ~kWallLeadTicks before the bike pins instead of
    // on contact, so re-steering starts sooner. Guard: only on a genuine
    // OVERSHOOT — projected to cross the lane edge AND moving away from the
    // target pad (vel and padErr opposite signs) — so it stays silent when you're
    // correctly racing toward a pad that sits near the edge. tunnel.x is the lane
    // offset (±kWallEdgeOffsetU = wall); smoothed_vel is the lateral velocity
    // (world-X delta, same units as the offset delta).
    if (have_target && tunOk) {
        const float vel       = g_state.smoothed_vel;
        const float projected = tunnel.x + vel * kWallLeadTicks;
        const bool  toward_wall  = std::fabs(projected) >= kWallEdgeOffsetU &&
                                   std::fabs(tunnel.x)  <  kWallEdgeOffsetU;
        const bool  overshooting = vel * padErr < 0.0f;  // moving away from pad
        if (toward_wall && overshooting && std::fabs(vel) > kWallMinVel &&
            now - g_state.last_wall_cue_ms >= kWallCueDebounceMs) {
            const int side = (vel > 0.0f) ? +1 : -1;
            Vector cue = listener_pos;
            cue.x += (side > 0) ? +kWallCuePanM : -kWallCuePanM;
            acc::audio::PlayCue3D(kWallCueResref, cue);
            g_state.last_wall_cue_ms = now;
            acclog::Write("SwoopRace",
                          "wall predict side=%s tunnelX=%.1f vel=%.2f "
                          "proj=%.1f padErr=%.1f",
                          side > 0 ? "right" : "left", tunnel.x, vel,
                          projected, padErr);
        }
    }

    // The continuous pan tone is retired in co-pilot mode; silence any loop
    // left active by a prior build / race so the two cues never overlap.
    if (g_state.accelpad_line_loop.IsActive()) g_state.accelpad_line_loop.Stop();

    // ----- Next-gate preview heads-up. -----
    // Once you're SETTLED on the current gate (aligned) and it's within the lead
    // window, announce the FOLLOWING gate's direction (relative to the current
    // gate's lane — that's where you'll be at the crossing). Soft + single =
    // "get ready", so it pre-loads the direction before the loud steer-now ticks,
    // recovering the reaction delay the old handoff snap used to eat. Once/gate.
    // Flaw 2 fix (2026-06-22): fire the heads-up once per gate whenever the
    // current gate is within the lead window — NOT only when you're aligned on
    // it. Gating on alignment meant a missed gate (you never settle) suppressed
    // the NEXT gate's preview, so one miss cascaded into a run of misses
    // (patch-20260622 miss clusters 26-28, 36-38, 47-51). The next-gate
    // direction is meaningful whether or not you caught the current one.
    if (have_target && next_slot >= 0 &&
        g_state.previewed_slot != ahead_slot && fwdGap < kPreviewLeadU) {
        const float nextDelta = next_pos.x - ahead_pos.x;
        if (std::fabs(nextDelta) > kReleaseBand) {       // a real move is coming
            const bool nextRight = nextDelta > 0.0f;
            Vector cue = listener_pos;
            cue.x = listener_pos.x +
                    (nextRight ? +kSteerTickPanM : -kSteerTickPanM);
            cue.y = listener_pos.y + kSteerTickFwdM;
            cue.z = listener_pos.z;
            acc::audio::PlayCue3D(nextRight ? kSteerRightResref : kSteerLeftResref,
                                  cue, /*priorityGroup=*/0, kPreviewVolume);
            acclog::Trace("SwoopRace",
                          "preview: cur=%d next=%d nextDelta=%.1f dir=%s fwdGap=%.1f",
                          ahead_slot, next_slot, nextDelta,
                          nextRight ? "R" : "L", fwdGap);
        }
        g_state.previewed_slot = ahead_slot;   // mark done even if no move
    }

    // ----- Co-pilot diagnostic -----
    // cmd: -1 left, +1 right, 2 aligned, 0 none. Tune kCoastTicks so the
    // steering→aligned transition lands |padErr| ~0 at the crossing (fwdGap≈0).
    if (have_target) {
        acclog::Trace("SwoopRace",
                      "copilot: padX=%.2f bikeX=%.2f padErr=%.2f vel=%.2f "
                      "coast=%.2f proj=%.2f cmd=%d tick=%d fwdGap=%.1f "
                      "tunnelX=%s%.2f",
                      ahead_pos.x, bikePos.x, padErr, g_state.smoothed_vel,
                      coast, proj, desired, tick_fired ? 1 : 0, fwdGap,
                      tunOk ? "" : "FAULT:", tunOk ? tunnel.x : 0.0f);
    }

    acclog::Trace("SwoopRace",
                  "accelpad scan: slots=%d accelpads=%d ahead=%d aheadSlot=%d "
                  "cmd=%d refY=%.1f",
                  slots_seen, accelpads_found, accelpads_ahead, ahead_slot,
                  g_state.steer_cmd, refY);

    // Remember this tick's targeted gate for next tick's crossing interpolation.
    if (have_target) {
        g_state.prev_ahead_slot   = ahead_slot;
        g_state.prev_ahead_posX   = ahead_pos.x;
        g_state.prev_ahead_posY   = ahead_pos.y;
        g_state.prev_ahead_padErr = padErr;
        g_state.prev_ahead_fwdGap = fwdGap;
        g_state.have_prev_ahead   = true;
    } else {
        g_state.have_prev_ahead   = false;
        g_state.prev_ahead_slot   = -1;
    }
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
    g_state.smoothed_steer_err    = 0.0f;
    g_state.prev_bike_x           = 0.0f;
    g_state.have_prev_bike_x      = false;
    g_state.smoothed_vel          = 0.0f;
    g_state.prev_ahead_slot       = -1;
    g_state.steer_cmd             = kSteerCmdNone;
    g_state.last_steer_tick_ms    = 0;
    g_state.previewed_slot        = -1;
    g_state.last_wall_cue_ms      = 0;
    g_state.have_prev_ahead       = false;
    g_state.prev_ahead_posX       = 0.0f;
    g_state.prev_ahead_posY       = 0.0f;
    g_state.prev_ahead_padErr     = 0.0f;
    g_state.prev_ahead_fwdGap     = 0.0f;
    g_state.accelpad_line_loop.Stop();
    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        g_state.obstacle_loops[i].Stop();
    }
}

}  // namespace acc::swoop_race
