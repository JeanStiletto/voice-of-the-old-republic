// Turret (space-combat gunner) minigame accessibility — see
// turret_game.h for design.
//
// Shares CSWMiniGame with swoop racing; distinguished by type==3.
//
// What it does:
//   - Entry/exit announce (the native control hint — WASD aims, Space
//     fires; both are native keyboard actions, live-confirmed).
//   - ONE 3D crosshair cue per round, on the SELECTED fighter (Q/E to lock /
//     cycle), using a RISING TICK RATE for centring:
//       * OFF-TARGET — the cue PULSES, panned toward the (lead-corrected)
//         target so the player can swing onto it from anywhere. The pulse
//         interval shrinks as the aim closes on the hit cone (slow pings far
//         off, near-buzz at the edge); behind, the pulses pin to the shorter-
//         swing side.
//       * ON-TARGET — the cue goes SOLID (continuous tone) the instant the aim
//         is inside the range-scaled hitbox. Solid == a shot connects, at any
//         range. (Loudness still swells toward dead-centre within the cone as a
//         secondary cue; it is NOT the primary signal — the engine's distance→
//         gain curve has too steep a knee to track the cone by loudness alone.)
//     Aim point is the INTERCEPT (range-gated lead; see kBoltSpeed) so distance
//     shots lead the target. Earlier we ran a SECOND ambient engine-loop on the
//     nearest fighters ("hum"); play-testing showed it was mostly ignored, so
//     it was dropped in favour of this single, less-cluttered cue. The vanilla
//     fighters carry no engine loop of their own (CSWTrackFollower+0x144 null);
//     the engine still plays its own one-shot mgs_sith_hit / _expl on
//     hit/kill, which covers "you connected" without us adding anything.

#include "turret_game.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "audio_bus.h"        // PlayCue3D — one-shot lock-confirmation ping
#include "audio_loop.h"       // LoopSource — per-fighter 3D engine loop
#include "hotkeys.h"          // Q/E target-cycle bindings
#include "menus_modsettings.h" // TurretAutoAim easy-mode toggle
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

// NOTE (RE + measured 2026-06-04): sphere_radius is the value OUR cue subtends
// against, but it is NOT the engine's hit primitive. The bullet-hit path is
// CSWMiniGame::DoHitCheck -> CSWTrackFollower::DoBulletHitCheck ->
// CSWMiniGame::HitCheckBullet, which intersects the bullet against the
// follower's MODEL parts (this->models) — mesh geometry, not this scalar.
// None of those three reads sphere_radius, so writing it only widens the cue,
// not the real hitbox. The OnTurretBulletHit diagnostic below CONFIRMED the
// ~20 m hitbox is real and the cue is honest (30 hits across the whole sphere,
// frac to 1.06) — so the cue must NOT be tightened. We then TESTED two ways to
// enlarge the hitbox at runtime — 2x model scale AND sphere_radius=40 — and
// BOTH left the hit radius at ~20 m (the gate is a cached bounding sphere, not
// runtime-patchable). So the hitbox can't be grown here; the easier-to-hit
// lever is aim-assist (magnetism), below. See docs/turret-difficulty-investigation.md.

// ----- TurretHitGeom diagnostic (OnHitBullet hook) -----
// CSWMiniGameObject::OnHitBullet fires once per engine-CONFIRMED bullet hit.
// On the hit-event object (ECX at the hook): +0x54/+0x58/+0x5c hold the
// world-space impact point (the engine feeds them to Play3DOneShotSound), and
// vtable[0x14] returns the victim CSWTrackFollower (the object whose HP it then
// adjusts). |impact - followerCentre| is therefore the distance from centre at
// which the shot actually connected — the real hit radius.
constexpr size_t kHitEventImpactXOffset      = 0x54;  // float (world)
constexpr size_t kHitEventImpactYOffset      = 0x58;  // float (world)
constexpr size_t kHitEventImpactZOffset      = 0x5c;  // float (world)
constexpr size_t kVtableSlotGetVictimFollower = 0x14; // OnHitBullet's vtable[0x14]

// Census / selection range. Live distance survey (enemy-sound diagnostic,
// 802 samples): fighters span ~0 m to ~560 m, mean ~233 m, with real density
// past 500 m. 600 m covers every fighter from the moment it appears, so Q/E
// can lock anything on the field.
constexpr float kFighterCueRangeM           = 600.0f;

// ----- Hum (approach cue on the LOCKED fighter) -----
// A continuous loop on the selected fighter at its REAL position (distance-
// compressed onto the engine's audible band), so its loudness tracks how close
// THAT fighter actually is — "it's approaching" — and it pans toward where the
// fighter really is. Distinct from the peg, which marks the lead/aim point and
// swells with aim error; the gap between the two IS the lead. Restored from the
// beatable build (which rode selected + nearest; now just the locked one).
// 1/30 maps the ~560 m spawn distance to ~19 m (faint end of the 5-20 m band)
// and reaches the 5 m floor by ~150 m, so the hum grows louder all the way in.
constexpr float kFighterDistanceCompression = 1.0f / 30.0f;
constexpr float kFighterMinSourceDistanceM  = 5.0f;
constexpr const char* kFighterLoopResref    = "acc_turret_loop";

// ----- Targeting "peg" cue (the single unified cue; drives the selected fighter) -----
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
//   subtend + kPegRampDeg .. kPegMaxDist  (audible FLOOR — the beacon level)
// A fixed peak-at-0° was too strict up close; a flat plateau lost the centre
// gradient that the bolt-travel margin needs. The whole hitbox MUST stay
// clearly loud ("loud == a shot connects"): a version that rewarded only the
// hitbox CENTRE (not the full subtend) tested distinctly worse — it fought the
// player toward dead-centre when the engine would already score a hit, so the
// loud peak stopped meaning "you can hit". So onTarget stays == full subtend.
//
// The saturation problem in the sole-cue build was the GRADIENT, not the floor.
// Pan already does the coarse "which way" job over the full 360° swing, so
// loudness only needs to carry the FINE approach near centre. The 45° ramp we
// tried when this became the only cue spread the 9->5 climb so thinly that
// "half right" already sat near max — no usable warmer/colder near centre. So
// keep the ramp NARROW: the volume swing concentrates in the last stretch of
// the swing, where fine aiming happens. 25° is a touch steeper than the 30°
// the two-cue ("beatable") build used, because the peg now carries fine-centring
// alone (no hum to assist).
//   FULL-RANGE FIX (2026-06-03): the earlier "loudness feels flat / loud the
//   moment I'm roughly on" was NOT the engine curve being dead — swoop's
//   obstacle cue proves pan+volume works (swoop_spatial_audio.cpp:185-205, the
//   curve has usable slope from ~1 m "right on top" through ~20 m "just
//   audible"). The peg was mapping aim error into only the TOP HALF of that
//   range (5->20 m = the gentle end), so dead-centre never reached the loud end
//   and the whole swing sat compressed and faint. Fix: open the ramp to the
//   full band — dead-centre at 1 m (right on top, loudest), the quiet beacon
//   floor at 20 m. Same gradient shape, just the full dynamic range.
constexpr float kPegRampDeg   = 25.0f;  // fade width beyond the hitbox edge (narrow = steep near-centre swing)
constexpr float kPegMinDist   = 1.0f;   // dead-centre -> right on top -> loudest
constexpr float kPegEdgeDist  = 9.0f;   // hitbox edge -> clearly audible (mid)
constexpr float kPegMaxDist   = 20.0f;  // way off -> just-audible beacon floor
// Fallback on-target half-angle when the hitbox radius can't be read (no enemy
// ptr this tick). Also the value the diagnostic "onTarget" flag falls back to.
constexpr float kFallbackOnTargetDeg = 6.0f;
// Behind-gate. The continuous cue is SILENCED only when the selected target
// is BEHIND the aim — past the rear boundary, i.e. in the rear hemisphere. The
// full forward 180° stays audible, so forward awareness is intact; we only drop
// the targets a crosshair could never point at without first turning around.
// The player swings (or Q/E-cycles) to bring a rear target back into the
// forward half. ~90° is the true front/back boundary, so this also removes the
// stereo front/back mirror (a rear-left target can't masquerade as a front-left
// one when the rear half is silent) without narrowing awareness.
//
// HYSTERESIS: a fighter orbiting right at the boundary used to flap the peg on
// and off every few ticks (a 60 s session logged 17 off / 18 on transitions),
// which — with the behind tick firing the SAME sample — read as mush rather
// than a clean tone. So the gate has a dead band: cross INTO behind only past
// kBehindEnterDeg, and back to front only once below kBehindExitDeg. State is
// latched in State::selected_behind (reset on (re)lock / out-of-view).
constexpr float kBehindEnterDeg = 95.0f;  // front -> behind above this
constexpr float kBehindExitDeg  = 85.0f;  // behind -> front below this
constexpr const char* kLockCueResref = "acc_turret_lock";

// ----- Rising-tick-rate crosshair -----
// The precision signal is the PULSE RATE, not loudness. Loudness-via-distance
// has a steep knee in the engine curve that can't track the range-scaled hit
// cone (it read "loud the moment I'm roughly on" well outside the cone at
// range). So off-target the cue PULSES, and the interval shrinks as the aim
// closes on the cone — slow pings far off, near-buzz at the edge — then goes
// SOLID (continuous tone) the instant the aim is inside the hitbox (onTarget).
// Solid == "a shot connects here", at ANY range, because onTarget is already
// scaled to the subtend. Off-target ticks carry direction via pan.
constexpr ULONGLONG kTickFastMs  = 90;    // just outside the cone — near-buzz
constexpr ULONGLONG kTickSlowMs  = 700;   // far off / behind — slow ping
constexpr float     kTickRampDeg = 90.0f; // angular span over which rate ramps:
// the FULL forward hemisphere. The aim is routinely 60-160° off a far target;
// a narrow ramp pinned the rate at slowest for the whole swing ("barely rises").
// Spanning ~90° means every degree of swinging toward the target speeds the
// pulse — the warmer/colder tracking signal — collapsing to solid in the cone.
constexpr float     kTickDist    = 8.0f;  // fixed pan distance for forward ticks
// The pulse uses a SHORT (~60 ms) percussive sample, NOT the ~1 s sustained
// acc_turret_lock the solid tone loops: one-shots of the long sample overlapped
// into a constant drone that masked the rate entirely (only pan changed). The
// tick is the same timbre family (sliced from the lock tone) so accelerating
// pulses audibly merge into the solid lock tone on-target.
constexpr const char* kTickCueResref = "acc_turret_tick";

// ----- Behind steering (rear-arc pinned pan) -----
// In the rear hemisphere the target can't masquerade as a front one, so the
// ticks (behind is always off-target) PIN hard to the shorter-swing side: pan
// left = swing left, pan right = swing right. The side is latched through a
// near-zero (dead-behind) lateral so it doesn't flip-flop. Hysteretic gate
// (kBehindEnterDeg/kBehindExitDeg) so a target orbiting the ~90° boundary
// doesn't flap. The elevation pitch glide applies to the solid tone (set
// unconditionally below) once the target is back in the forward cone.
constexpr float kBehindPanDist = 9.0f;  // pinned tick distance while behind

// ----- Elevation channel (peg playback PITCH = vertical aim error) -----
// Stereo pan carries azimuth (left/right) but nothing about elevation, so the
// peg's loudness alone left the up/down axis blind — the dominant miss at
// range (QC: vertically on-target only ~15% of frames; targets span -7°..+73°
// elevation against a fixed aim). We add the orthogonal channel as PITCH: the
// peg plays at its natural rate while the aim is within the hitbox's elevation
// tolerance ("sounds normal" = on the vertical line, a shot connects), and
// glides off-pitch outside it — HIGHER when the target is above the aim (press
// W) and LOWER when below (press S). The dead band is the hitbox subtend (the
// same range-scaled tolerance the azimuth/loudness logic already uses), so the
// "normal pitch" landmark means "elevation is inside the hitbox" at any range:
// wide and forgiving up close, tight far out.
constexpr float kElevDegPerOctave = 25.0f;  // vertical error beyond band per octave
constexpr float kElevMaxOctaves   = 1.0f;   // clamp the glide to +/- 1 octave

// ----- "Fire now" lock cue (one-shot, on entering the hitbox) -----
// The single most useful signal for a blind gunner: a distinct one-shot the
// instant the COMBINED aim (azimuth via pan + elevation via pitch) crosses
// INTO the hitbox cone (onTarget false->true) — "both axes aligned, shoot".
// It collapses the two-axis judgement into one reaction, and self-gates by the
// hitbox geometry: it fires readily up close (wide cone) and almost never far
// (3° cone), which correctly signals that far fighters aren't worth firing at.
// One-shot, not continuous — the player is rarely on a fast target long enough
// to ride a sustained tone. Played toward the fighter so it pans correctly.
constexpr const char* kFireCueResref = "c_drdastro_hit2";

// ----- Range-window cue (joining / leaving killable range) -----
// Far fighters (subtend ~2.6° past 250 m) are unhittable by ear; close ones
// (~20° subtend under 100 m) hit ~25% (live QC). So the playable loop is
// "track it in as it closes, fire when it's actually in range." This one-shot
// punctuates that boundary: it fires when the locked fighter crosses INTO
// killable range, and again when it LEAVES (leaving matters most — it says
// "window closed, stop firing / reacquire"). Positioned at the fighter so it
// pans toward it. A short, loud, distinct sample so it never blurs into the
// continuous crosshair tone. 130 m is the first knob to tune — derived from
// the close-band 25%-hit boundary, nudged out a little for reaction time.
constexpr float kKillableRangeM       = 130.0f;
constexpr const char* kRangeCueResref = "c_drdastro_atk1";
// LEAVE fires the marker twice (a double-tap) so it's unambiguous against the
// single-tap ENTER — leaving ("window closed, reacquire") is the event the
// player most needs to read at a glance. This is the gap to the 2nd tap; long
// enough that the short sample doesn't blur into one, tick-rate independent.
constexpr ULONGLONG kLeaveTapGapMs    = 180;

// ----- Predictive lead (intercept aiming) -----
// The cue aims at where the fighter WILL BE when a bolt arrives, not where it
// is now — the audio analog of a sighted player pulling ahead of the target.
// Our bolt speed is authoritative from the M12ab area GIT (MiniGame -> Player
// -> Gun_Banks -> Bullet -> Speed = 300; Lifespan = 3 s -> ~900-unit range).
// With this (slow-ish) bolt the measured critBoltSpeed (149 close / 753 mid /
// 1366 far) means lead is unnecessary close but ESSENTIAL at mid/far, where
// the ~13.5deg lead exceeds the hitbox window — so leading converts mid-range
// from a systematic miss into a hittable shot. We solve the intercept in the
// LISTENER frame (relative position + relative velocity) so the result is
// directly "where to point the turret" and ship motion folds in automatically.
constexpr float kBoltSpeed      = 300.0f;  // units/s (M12ab Bullet.Speed)
constexpr float kMaxLeadSeconds = 3.0f;    // = Bullet.Lifespan; clamp intercept
constexpr float kVelEmaAlpha    = 0.25f;   // velocity smoothing (per-tick Δpos)
// Range-gate the lead: it is UNNECESSARY when the fighter would stay inside its
// own hitbox during the bolt's flight (the centre-aim margin already covers
// that drift), and applying it there only pulls the loud spot off the ship and
// adds velocity-jitter — which measurably hurt the close-range aim that wins
// the game. So ramp the lead in by how far the fighter drifts during flight
// relative to its radius: none below kLeadGateLowFrac·radius, full by
// kLeadGateHighFrac·radius. This auto-adapts to each fighter's actual speed.
constexpr float kLeadGateLowFrac  = 0.75f;  // start leading at 0.75·radius drift
constexpr float kLeadGateHighFrac = 1.5f;   // full lead by 1.5·radius drift

// ----- Curve-aware lead (centripetal turn model) -----
// The fighters hold ~constant speed and only TURN (relVel sat at ~74 u/s at every
// range in the logs), so their acceleration is essentially centripetal. We
// estimate it as the PERPENDICULAR component of d(vel_ema)/dt — discarding the
// along-track part enforces the constant-speed prior and kills most of the noise
// — smooth it harder than velocity (a second derivative is noisier), and fold a
// 0.5*a*t^2 term into the intercept via a fixed-point flight-time solve. Heavy
// smoothing is safe because the curve term is weighted by t^2, which is tiny on
// the close fast passes (short flight, noisiest estimate) and only grows at
// mid-range where the turn is steady and easy to track. The correction is
// leadFrac-gated, flight-time-capped, and clamped to never exceed the linear
// lead, so it sharpens mid-range aim without destabilising close or far shots.
constexpr float kAccelEmaAlpha       = 0.12f;  // centripetal-accel smoothing (< vel alpha)
constexpr int   kAccelSettleSamples  = 5;      // ticks before the turn estimate is trusted
constexpr float kMaxCentripetalAccel = 300.0f; // u/s^2 sanity clamp on the estimate
constexpr float kAccelLeadCapSeconds = 1.0f;   // cap on the flight-time used in 0.5*a*t^2

// ----- Aim-assist (magnetism — the DEFAULT, always-on mode) -----
// The hitbox can't be enlarged at runtime (see the sphere_radius RE note), so
// the honest "easier to hit" lever is to nudge the GUN toward the locked
// fighter once the aim is already close — pulling near-misses into the real
// ~20 m hitbox. It is a PARTIAL pull (a fraction of the remaining gap per tick),
// not a snap: the player still does the acquisition swing and keeps agency; the
// assist only closes the last few degrees. Engages only inside kAssistZoneDeg
// (so it never fights a deliberate swing to a far/rear target), via the
// CSWMiniPlayer.aim (+0x1c4) write. This is ON by default — a within-session A/B
// proved it ~14x's the hit rate vs unaided (~0.8% — effectively unplayable by
// ear). The opt-in "Autoaiming" toggle (TurretAutoAim) upgrades it to a full
// lock-on (pull=1, no zone limit) in the assist block below.
constexpr float kAssistZoneDeg = 15.0f;  // engage only within this aim error
constexpr float kAssistPull    = 0.50f;  // fraction of the gap closed per tick

// ============================================================================
// Module state. Single-threaded under the engine OnUpdate tick.
// ============================================================================

struct State {
    bool      active           = false;
    void*     latched_mini_game = nullptr;
    void*     latched_vtable    = nullptr;
    ULONGLONG entered_at_ms     = 0;
    int       ticks_since_lost  = 0;

    // Single continuous targeting "peg" tone: tracks the Q/E-SELECTED
    // fighter; its loudness rises as the aim ray closes on that fighter.
    acc::audio::LoopSource peg_cue;

    // Continuous "approach" hum on the locked fighter (real compressed position;
    // loudness = how close it is). See kFighterLoopResref.
    acc::audio::LoopSource hum_cue;

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

    // Per-slot kill/HP tracking across ticks, independent of selection — so we
    // announce EVERY downed fighter (not just the locked one) and can tell a
    // kill from a despawn. Filled by the Pass-1 walk, which visits every
    // fighter anyway. slot_alive_last = resolved AsEnemy last tick;
    // slot_hp_last = last-seen hp (-1 unknown); slot_damaged = ever seen hp
    // below max (so a fighter that vanishes undamaged reads as a despawn).
    bool slot_alive_last[kMgoArraySlotCount] = {false};
    int  slot_hp_last[kMgoArraySlotCount]    = {0};
    bool slot_damaged[kMgoArraySlotCount]    = {false};

    // Aim-assist heartbeat-log throttle (the assist writes the aim every tick;
    // we only log ~1/s so normal play isn't spammed).
    ULONGLONG assist_last_log_ms = 0;

    // Session accuracy telemetry: player shots (CSWMiniPlayer::Fire calls) and
    // fighter hits, for a hits/shots line at exit. Reset on turret entry.
    int shots_fired = 0;
    int fighter_hits = 0;

    // Rear-arc pinned pan (behind steering): the last chosen swing side. Latched
    // so a target sitting exactly behind (lateral ~0) doesn't flip-flop the pan
    // left/right tick to tick.
    bool      last_behind_swing_left = false;

    // Rising-tick-rate crosshair: timestamp of the last off-target pulse. The
    // pulse interval is recomputed each tick from the aim error; a pulse fires
    // when this many ms have elapsed. Solid tone (on-target) bypasses it.
    ULONGLONG last_tick_ms = 0;

    // On-target edge tracker for the selected fighter: true while the combined
    // aim was inside the hitbox last tick. A false->true transition fires the
    // one-shot "fire now" cue. Reset on (re)lock and when the target leaves the
    // forward view, so re-acquiring re-arms the cue.
    bool selected_on_target = false;

    // Behind-gate hysteresis latch: true while the selected fighter is in the
    // rear hemisphere (peg silenced, behind-tick steering). Crosses to true
    // above kBehindEnterDeg and back to false below kBehindExitDeg, so a target
    // orbiting near the 90° boundary doesn't flap the peg on/off. Reset to
    // false (front) on (re)lock and out-of-view so re-acquiring re-evaluates.
    bool selected_behind = false;

    // HP of the selected fighter last tick (-1 = unknown / just (re)selected).
    // A drop between ticks is a real engine-scored hit; logged with the aim
    // error at that frame to measure the actual hit cone. Reset on every
    // selection change so a new lock doesn't read as a phantom hit.
    int last_selected_hp = -1;

    // Killable-range edge tracker for the locked fighter: -1 unknown, 0 out of
    // range, 1 in range. A change fires the enter/leave range cue. On a fresh
    // lock AnnounceSelectedTarget baselines this to the target's actual state
    // (and fires ENTER immediately if it's already in range).
    int selected_in_range = -1;

    // Delayed second tap for a LEAVE range cue (double-tap = unambiguous vs the
    // single ENTER tap). 0 = none pending; otherwise the GetTickCount64 ms at
    // which the second tap fires, played at leave_tap_pos.
    ULONGLONG leave_tap_due_ms = 0;
    Vector    leave_tap_pos    = {0.0f, 0.0f, 0.0f};

    // Previous-tick LISTENER-RELATIVE position + timestamp of the selected
    // fighter, for the velocity estimate (v = Δrelpos/Δt). Working relative to
    // the listener folds ship motion into the velocity automatically, so the
    // intercept is directly "where to point the turret". Invalid until
    // have_last_pos. vel_ema is the smoothed relative velocity used for lead.
    Vector    last_selected_pos = {0.0f, 0.0f, 0.0f};
    ULONGLONG last_selected_pos_ms = 0;
    bool      have_last_pos = false;
    Vector    vel_ema = {0.0f, 0.0f, 0.0f};
    bool      have_vel = false;

    // Curve-aware lead: smoothed centripetal acceleration (the perpendicular part
    // of d(vel_ema)/dt), with a settle counter. Reset on (re)lock with have_vel.
    Vector    accel_ema     = {0.0f, 0.0f, 0.0f};
    bool      have_accel    = false;
    int       accel_samples = 0;

    // LeadCheck self-measuring diagnostic: a deferred ring of intercept
    // predictions (world frame) tagged with their bolt-arrival time. When a probe
    // matures we compare it to the fighter's ACTUAL position then, logging the
    // linear-vs-curve miss distance — so the lead model is tunable/validatable
    // from the log alone, no sighted check. dueMs == 0 marks an empty slot.
    struct LeadProbe { ULONGLONG dueMs; int slot; Vector lin; Vector curve; };
    static constexpr int kLeadProbeCount = 16;
    LeadProbe lead_probes[kLeadProbeCount] = {};
    int       lead_probe_next = 0;
    ULONGLONG lead_probe_push_ms = 0;

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
    g_state.peg_cue.Stop();
    g_state.hum_cue.Stop();
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

// Write the turret aim (easy-mode auto-track). The exact inverse of
// ReadAimDir: store azimuth in aim.z and elevation in aim.x (degrees), leaving
// aim.y (unused). The engine's free-aim fire path orients bolts by the gun, so
// writing the aim steers both the barrel and the shots. We overwrite after the
// engine's per-tick WASD update; with WASD idle in auto mode our value holds.
bool WriteAim(float azDeg, float elDeg) {
    void* player = SafeReadPtr(g_state.latched_mini_game, kMiniGamePlayerOffset);
    if (!player) return false;
    __try {
        Vector* aim = reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(player) + kMiniPlayerAimOffset);
        aim->x = elDeg;   // elevation
        aim->z = azDeg;   // azimuth
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Angle (degrees) between the aim ray and the (already normalised)
// direction to a target. 0 = dead on.
float AngleBetweenDeg(const Vector& a, const Vector& b) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z;
    if (dot > 1.0f) dot = 1.0f; else if (dot < -1.0f) dot = -1.0f;
    return std::acos(dot) * 57.2958f;
}

// ----- LeadCheck self-measuring diagnostic -----
// Enqueue this tick's linear & curve intercepts (world frame) tagged with the
// bolt-arrival time; when one matures, compare both to the fighter's ACTUAL
// position then and log the miss distances. Lets us prove/tune the curve model
// from the log alone. Slot-tagged so a probe outlives a Q/E switch harmlessly.
void PushLeadProbe(int slot, const Vector& linW, const Vector& curveW, ULONGLONG dueMs) {
    State::LeadProbe& p = g_state.lead_probes[g_state.lead_probe_next];
    p.dueMs = dueMs; p.slot = slot; p.lin = linW; p.curve = curveW;
    g_state.lead_probe_next = (g_state.lead_probe_next + 1) % State::kLeadProbeCount;
}

void DrainLeadProbes(int slot, const Vector& fighterWorldNow, ULONGLONG now) {
    for (int i = 0; i < State::kLeadProbeCount; ++i) {
        State::LeadProbe& p = g_state.lead_probes[i];
        if (p.dueMs == 0 || now < p.dueMs) continue;
        if (p.slot == slot) {
            const float lx = p.lin.x - fighterWorldNow.x, ly = p.lin.y - fighterWorldNow.y, lz = p.lin.z - fighterWorldNow.z;
            const float cx = p.curve.x - fighterWorldNow.x, cy = p.curve.y - fighterWorldNow.y, cz = p.curve.z - fighterWorldNow.z;
            const float el = std::sqrt(lx * lx + ly * ly + lz * lz);
            const float ec = std::sqrt(cx * cx + cy * cy + cz * cz);
            acclog::Write("LeadCheck", "slot=%d linErr=%.1f curveErr=%.1f gain=%.1f",
                          slot, el, ec, el - ec);
        }
        p.dueMs = 0;  // consume (or discard a stale-slot probe)
    }
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

// Play the range-boundary marker (c_drdastro_atk1) at a compressed, always-
// audible point toward the fighter so it pans correctly — the same trick the
// lock locator uses. Returns the position used, so a LEAVE event can schedule
// its second tap at the same spot.
Vector PlayRangeCueAt(const Vector& targetPos, float dist, const Vector& listener) {
    Vector pos = targetPos;
    if (dist > 0.0f) {
        const float kk = kPegMinDist / dist;
        pos.x = listener.x + (targetPos.x - listener.x) * kk;
        pos.y = listener.y + (targetPos.y - listener.y) * kk;
        pos.z = listener.z + (targetPos.z - listener.z) * kk;
    }
    acc::audio::PlayCue3D(kRangeCueResref, pos);
    return pos;
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
    g_state.have_vel         = false;
    g_state.have_accel       = false;  // and the turn estimate
    g_state.accel_samples    = 0;
    g_state.selected_on_target = false;  // re-arm the "fire now" cue
    g_state.selected_behind    = false;  // re-evaluate the behind-gate cleanly
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
    // on a silent re-pick (it's a locator cue, not speech). Uses the SHORT tick
    // sample, not the ~1 s lock beep: rapid Q/E cycling fires this on every
    // switch, and overlapping one-shots of the long sample drone into "scrilling".
    const float sd = occDist[idx];
    Vector pingPos = occPos[idx];
    if (sd > 0.0f) {
        const float kk = kPegMinDist / sd;
        pingPos.x = listener.x + (occPos[idx].x - listener.x) * kk;
        pingPos.y = listener.y + (occPos[idx].y - listener.y) * kk;
        pingPos.z = listener.z + (occPos[idx].z - listener.z) * kk;
    }
    acc::audio::PlayCue3D(kTickCueResref, pingPos);

    // Baseline the range-edge tracker to this lock's actual state. If it's
    // already inside killable range, fire the in-range (ENTER) cue now — so a
    // Q/E onto a close target (and the opening cluster after the cutscene)
    // announces "you can hit this" immediately, not only on the next crossing.
    const bool inRangeNow = (sd > 0.0f && sd <= kKillableRangeM);
    g_state.selected_in_range = inRangeNow ? 1 : 0;
    if (inRangeNow) {
        PlayRangeCueAt(occPos[idx], sd, listener);
        acclog::Write("Turret", "range cue: ENTER (on lock) slot=%d dist=%.0fm",
                      g_state.selected_slot, sd);
    }

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
            // AsEnemy no longer resolves it) from a fighter that is still
            // alive but merely dropped out of the census this tick (flew
            // beyond cue range / off-screen, or a one-tick position-read miss).
            // selectedAliveInPool is set by TickFighterCues' Pass-1 walk
            // regardless of range.
            const bool killed = !selectedAliveInPool;

            if (!killed) {
                // Alive, just out of view. HOLD the lock and fall silent —
                // do NOT re-pick and do NOT announce. The player keeps the
                // same target and can wait for it to come back or manually
                // retarget with Q/E. (Re-picking here jolted the lock, and a
                // transient read miss could trip it spuriously; re-pick now
                // happens only on round start and on a kill.)
                if (g_state.peg_cue.IsActive()) {
                    g_state.peg_cue.Stop();
                    acclog::Write("Turret",
                                  "peg off (out of view) — holding lock slot=%d",
                                  g_state.selected_slot);
                }
                g_state.selected_on_target = false;  // re-arm fire-now on return
                g_state.selected_behind    = false;  // re-evaluate gate on return
                return;
            }

            // KILL: the selected fighter left the pool. The global per-slot
            // down-detector in TickFighterCues already announced "Fighter N
            // destroyed", so here we only transition the lock — stop the cue
            // and auto-advance to the nearest remaining fighter (silently).
            if (g_state.peg_cue.IsActive()) {
                g_state.peg_cue.Stop();
                acclog::Write("Turret", "peg off (selected destroyed) slot=%d",
                              g_state.selected_slot);
            }

            if (occCount > 0) {
                AnnounceSelectedTarget(0, occSlot, occDist, occPos, occCount,
                                       listener, /*speak=*/false);
                idx = 0;
            } else {
                g_state.selected_slot = -1;
                acclog::Write("Turret", "no fighters remain (after kill)");
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
    auto wrap180 = [](float d) {
        while (d > 180.0f)  d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        return d;
    };
    auto clamp1 = [](float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); };

    const Vector& sp = occPos[idx];
    const float   sd = occDist[idx];

    // Range-window cue: punctuate the locked fighter crossing INTO killable
    // range (enter) and back OUT of it (leave). Fires independent of the
    // front-cone gate — it's about distance, not bearing. Same sample for both
    // for now; the crosshair tone swelling (enter) vs fading (leave) is the
    // current disambiguator. Played at a compressed, always-audible point in
    // the fighter's direction so it pans toward it (the lock-locator trick).
    {
        const int wasInRange = g_state.selected_in_range;          // -1/0/1
        const int nowInRange = (sd > 0.0f && sd <= kKillableRangeM) ? 1 : 0;
        if (wasInRange >= 0 && nowInRange != wasInRange) {
            const Vector pos = PlayRangeCueAt(sp, sd, listener);
            if (!nowInRange) {
                // LEAVE: schedule a 2nd tap so a double-tap unambiguously means
                // "window closed / reacquire", vs the single tap for ENTER.
                g_state.leave_tap_due_ms = GetTickCount64() + kLeaveTapGapMs;
                g_state.leave_tap_pos    = pos;
            }
            acclog::Write("Turret", "range cue: %s slot=%d dist=%.0fm",
                          nowInRange ? "ENTER" : "LEAVE",
                          g_state.selected_slot, sd);
        }
        g_state.selected_in_range = nowInRange;
    }

    // Fighter position relative to the listener (gun). We solve the whole lead
    // in this frame so the answer is "where to point the turret".
    const Vector P = {sp.x - listener.x, sp.y - listener.y, sp.z - listener.z};

    // ---- Smoothed relative velocity (Δ relative-pos / Δt, EMA). ------------
    const ULONGLONG now = GetTickCount64();
    if (g_state.have_last_pos && now > g_state.last_selected_pos_ms) {
        const float dt = (now - g_state.last_selected_pos_ms) / 1000.0f;
        const Vector vinst = {(P.x - g_state.last_selected_pos.x) / dt,
                              (P.y - g_state.last_selected_pos.y) / dt,
                              (P.z - g_state.last_selected_pos.z) / dt};
        if (g_state.have_vel) {
            const Vector velPrev = g_state.vel_ema;
            const float a = kVelEmaAlpha;
            g_state.vel_ema.x = a * vinst.x + (1.0f - a) * g_state.vel_ema.x;
            g_state.vel_ema.y = a * vinst.y + (1.0f - a) * g_state.vel_ema.y;
            g_state.vel_ema.z = a * vinst.z + (1.0f - a) * g_state.vel_ema.z;

            // Centripetal acceleration = perpendicular part of d(vel_ema)/dt.
            // Drop the along-track component (speed is ~constant, so it's noise),
            // clamp the magnitude, then EMA harder than velocity.
            if (dt > 1e-4f) {
                Vector ai = {(g_state.vel_ema.x - velPrev.x) / dt,
                             (g_state.vel_ema.y - velPrev.y) / dt,
                             (g_state.vel_ema.z - velPrev.z) / dt};
                const float vm = std::sqrt(g_state.vel_ema.x * g_state.vel_ema.x +
                                           g_state.vel_ema.y * g_state.vel_ema.y +
                                           g_state.vel_ema.z * g_state.vel_ema.z);
                if (vm > 1e-3f) {
                    const Vector vh = {g_state.vel_ema.x / vm,
                                       g_state.vel_ema.y / vm,
                                       g_state.vel_ema.z / vm};
                    const float adv = ai.x * vh.x + ai.y * vh.y + ai.z * vh.z;
                    ai.x -= adv * vh.x; ai.y -= adv * vh.y; ai.z -= adv * vh.z;
                }
                const float am = std::sqrt(ai.x * ai.x + ai.y * ai.y + ai.z * ai.z);
                if (am > kMaxCentripetalAccel) {
                    const float s = kMaxCentripetalAccel / am;
                    ai.x *= s; ai.y *= s; ai.z *= s;
                }
                const float b = kAccelEmaAlpha;
                g_state.accel_ema.x = b * ai.x + (1.0f - b) * g_state.accel_ema.x;
                g_state.accel_ema.y = b * ai.y + (1.0f - b) * g_state.accel_ema.y;
                g_state.accel_ema.z = b * ai.z + (1.0f - b) * g_state.accel_ema.z;
                if (++g_state.accel_samples >= kAccelSettleSamples)
                    g_state.have_accel = true;
            }
        } else {
            g_state.vel_ema = vinst;
            g_state.have_vel = true;
        }
    }
    g_state.last_selected_pos    = P;
    g_state.last_selected_pos_ms = now;
    g_state.have_last_pos        = true;
    const Vector v = g_state.vel_ema;
    const float  vmag = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);

    // ---- Combat fields off the selected CSWTrackFollower. ------------------
    int   hp = -1, maxHp = -1;
    float radius = 0.0f, invuln = 0.0f, engineSpeed = 0.0f;
    if (selectedEnemy && trackingExisting) {
        hp     = static_cast<int>(SafeReadU32(selectedEnemy, kFollowerHpOffset));
        maxHp  = static_cast<int>(SafeReadU32(selectedEnemy, kFollowerMaxHpOffset));
        radius = SafeReadF32(selectedEnemy, kFollowerSphereRadiusOffset);
        invuln = SafeReadF32(selectedEnemy, kFollowerInvulnOffset);
        engineSpeed = SafeReadF32(selectedEnemy, kFollowerSpeedOffset);
    }

    // ---- Lead: aim at the intercept point. Solve |P + v·t| = kBoltSpeed·t for
    // the smallest positive t (a·t² + b·t + c = 0), then I = P + v·t. Falls
    // back to the current position when velocity isn't established yet or no
    // valid intercept exists. Clamped to the bolt lifespan. ------------------
    Vector aimRel = P;           // listener-relative point the cue aims at
    Vector aimRelLin = P;        // linear-only baseline (LeadCheck diagnostic)
    float  leadT = 0.0f, leadDist = 0.0f, leadFrac = 0.0f;
    if (g_state.have_vel) {
        const float a  = (v.x * v.x + v.y * v.y + v.z * v.z) - kBoltSpeed * kBoltSpeed;
        const float bq = 2.0f * (P.x * v.x + P.y * v.y + P.z * v.z);
        const float cq = (P.x * P.x + P.y * P.y + P.z * P.z);
        float t = -1.0f;
        if (std::fabs(a) < 1e-3f) {
            if (std::fabs(bq) > 1e-6f) t = -cq / bq;
        } else {
            const float disc = bq * bq - 4.0f * a * cq;
            if (disc >= 0.0f) {
                const float sq = std::sqrt(disc);
                const float t1 = (-bq + sq) / (2.0f * a);
                const float t2 = (-bq - sq) / (2.0f * a);
                if (t1 > 0.0f)                       t = t1;
                if (t2 > 0.0f && (t < 0.0f || t2 < t)) t = t2;
            }
        }
        if (t > 0.0f && t <= kMaxLeadSeconds) {
            leadT = t;
            const float fullLead = vmag * t;  // drift during flight
            // Range-gate: ramp lead in by drift relative to the hitbox radius,
            // so close shots (drift < ~0.75·radius) keep the pure ship aim.
            const float lo = kLeadGateLowFrac  * radius;
            const float hi = kLeadGateHighFrac * radius;
            if (radius > 0.0f && hi > lo) {
                leadFrac = (fullLead - lo) / (hi - lo);
                if (leadFrac < 0.0f)      leadFrac = 0.0f;
                else if (leadFrac > 1.0f) leadFrac = 1.0f;
            } else {
                leadFrac = 1.0f;  // no radius this tick — lead fully
            }
            aimRel.x = P.x + v.x * t * leadFrac;
            aimRel.y = P.y + v.y * t * leadFrac;
            aimRel.z = P.z + v.z * t * leadFrac;
            leadDist = fullLead * leadFrac;  // applied lead
            aimRelLin = aimRel;              // capture the linear baseline

            // Curve-aware refinement (constant centripetal accel). A few
            // flight-time fixed-point iterations layer a 0.5·a·t² turn term onto
            // the linear lead. The accel part is leadFrac-gated, flight-time-
            // capped, and clamped to never exceed the linear lead — sharpening
            // mid-range aim without destabilising close or far shots. This is the
            // SHARED aim point: cue, magnetism, and autoaim all read aimRel.
            if (g_state.have_accel) {
                const Vector ac = g_state.accel_ema;
                float tc = t;
                Vector pred = aimRel;
                float corrMag = leadDist;
                for (int it = 0; it < 3; ++it) {
                    const float te = (tc < kAccelLeadCapSeconds) ? tc : kAccelLeadCapSeconds;
                    const float half = 0.5f * te * te * leadFrac;
                    Vector linPart = {v.x * tc * leadFrac, v.y * tc * leadFrac, v.z * tc * leadFrac};
                    Vector accPart = {ac.x * half, ac.y * half, ac.z * half};
                    const float lpm = std::sqrt(linPart.x * linPart.x + linPart.y * linPart.y + linPart.z * linPart.z);
                    const float apm = std::sqrt(accPart.x * accPart.x + accPart.y * accPart.y + accPart.z * accPart.z);
                    if (apm > lpm && apm > 1e-4f) {  // curve part can't exceed linear lead
                        const float s = lpm / apm;
                        accPart.x *= s; accPart.y *= s; accPart.z *= s;
                    }
                    const Vector corr = {linPart.x + accPart.x, linPart.y + accPart.y, linPart.z + accPart.z};
                    pred = {P.x + corr.x, P.y + corr.y, P.z + corr.z};
                    corrMag = std::sqrt(corr.x * corr.x + corr.y * corr.y + corr.z * corr.z);
                    const float pd = std::sqrt(pred.x * pred.x + pred.y * pred.y + pred.z * pred.z);
                    tc = pd / kBoltSpeed;
                    if (tc > kMaxLeadSeconds) tc = kMaxLeadSeconds;
                    else if (tc < 0.0f)       tc = 0.0f;
                }
                aimRel   = pred;
                leadT    = tc;
                leadDist = corrMag;
            }
        }
    }

    // Aim error to the intercept point (this drives the cue); also the error to
    // the CURRENT fighter (no-lead) for diagnostic comparison.
    const float aimTgtDist =
        std::sqrt(aimRel.x * aimRel.x + aimRel.y * aimRel.y + aimRel.z * aimRel.z);
    Vector tdir = {0.0f, 0.0f, 0.0f};
    float  angle = 0.0f;
    if (aimTgtDist > 0.0f) {
        tdir.x = aimRel.x / aimTgtDist;
        tdir.y = aimRel.y / aimTgtDist;
        tdir.z = aimRel.z / aimTgtDist;
        angle = AngleBetweenDeg(aimDir, tdir);
    }
    float angleNow = 0.0f;
    if (sd > 0.0f) {
        const Vector nd = {P.x / sd, P.y / sd, P.z / sd};
        angleNow = AngleBetweenDeg(aimDir, nd);
    }

    // Hitbox half-angle at the INTERCEPT distance — "on target" = a bolt fired
    // now lands in the sphere when it arrives.
    const float subtendDeg =
        (radius > 0.0f && aimTgtDist > 0.0f) ? std::atan(radius / aimTgtDist) * kRad2Deg : 0.0f;
    const float onTargetAngle =
        (subtendDeg > 0.0f) ? subtendDeg : kFallbackOnTargetDeg;
    const bool  onTarget = (angle <= onTargetAngle);

    // "Fire now": one-shot the instant the combined aim crosses INTO the hitbox
    // (false->true). Played at a compressed, always-audible point toward the
    // fighter so it pans correctly. selected_on_target latches so it fires once
    // per entry, not every tick you hold the cone.
    if (onTarget && !g_state.selected_on_target) {
        Vector firePos = sp;
        if (sd > 0.0f) {
            const float fk = kPegMinDist / sd;
            firePos.x = listener.x + (sp.x - listener.x) * fk;
            firePos.y = listener.y + (sp.y - listener.y) * fk;
            firePos.z = listener.z + (sp.z - listener.z) * fk;
        }
        acc::audio::PlayCue3D(kFireCueResref, firePos);
        acclog::Write("Turret", "fire-now slot=%d angle=%.1f subtend=%.1f dist=%.0fm",
                      g_state.selected_slot, angle, subtendDeg, sd);
    }
    g_state.selected_on_target = onTarget;

    // Loudness: rise gently from the hitbox edge (kPegEdgeDist) to a peak at
    // dead-centre (kPegMinDist); beyond the edge, fade to faint over
    // kPegRampDeg. Continuous at the boundary.
    float srcDist;
    if (onTarget) {
        const float u = (onTargetAngle > 0.0f) ? angle / onTargetAngle : 0.0f;
        srcDist = kPegMinDist + u * (kPegEdgeDist - kPegMinDist);
    } else {
        float t = (angle - onTargetAngle) / kPegRampDeg;
        if (t > 1.0f) t = 1.0f;
        srcDist = kPegEdgeDist + t * (kPegMaxDist - kPegEdgeDist);
    }
    const float kk = (aimTgtDist > 0.0f) ? srcDist / aimTgtDist : 1.0f;
    const Vector cuePos = {
        listener.x + aimRel.x * kk,
        listener.y + aimRel.y * kk,
        listener.z + aimRel.z * kk,
    };

    // Behind-gate (hysteretic): a target orbiting the ~90° boundary mustn't flap.
    // Cross INTO behind above kBehindEnterDeg, back to front below kBehindExitDeg.
    // Behind is always off-target (onTargetAngle is small), so it always ticks —
    // pinned to the shorter-swing side.
    const bool behind = g_state.selected_behind ? (angle >= kBehindExitDeg)
                                                : (angle > kBehindEnterDeg);
    const bool behindEntered = behind && !g_state.selected_behind;
    g_state.selected_behind = behind;

    // Where the off-target pulse plays. Forward: toward the aim/intercept at a
    // fixed audible distance, so pan points the way and volume stays steady
    // (rate, not loudness, carries closeness). Behind: pinned to the shorter-
    // swing side (2D cross product of aim x target gives the side; latched
    // through a near-zero dead-behind lateral so it doesn't flip-flop).
    Vector tickPos;
    bool   swingLeft = g_state.last_behind_swing_left;
    if (behind) {
        const float cross = aimDir.x * tdir.y - aimDir.y * tdir.x;
        if (cross > 0.05f)       swingLeft = true;
        else if (cross < -0.05f) swingLeft = false;
        g_state.last_behind_swing_left = swingLeft;
        float sx = swingLeft ? -aimDir.y : aimDir.y;
        float sy = swingLeft ?  aimDir.x : -aimDir.x;
        const float sm = std::sqrt(sx * sx + sy * sy);
        if (sm > 1e-4f) { sx /= sm; sy /= sm; }
        tickPos = { listener.x + sx * kBehindPanDist,
                    listener.y + sy * kBehindPanDist,
                    listener.z };
    } else {
        tickPos = { listener.x + tdir.x * kTickDist,
                    listener.y + tdir.y * kTickDist,
                    listener.z + tdir.z * kTickDist };
    }

    if (onTarget && !behind) {
        // SOLID: continuous tone at the loudness-ramped position, which still
        // swells toward dead-centre within the cone (the bolt-margin reward).
        // The elevation pitch glide applies below. This is the unambiguous
        // "a shot connects — fire" signal, at any range.
        if (g_state.peg_cue.IsActive()) {
            g_state.peg_cue.UpdatePosition(cuePos);
        } else if (g_state.peg_cue.Start(kLockCueResref, cuePos,
                       /*looping=*/true, /*spatial=*/true, /*priorityGroup=*/-1,
                       /*volumeByte=*/-1, /*maxVolDist=*/kPegMinDist,
                       /*minVolDist=*/kPegMaxDist)) {
            acclog::Write("Turret", "peg SOLID (on target) slot=%d angle=%.1f subtend=%.1f",
                          g_state.selected_slot, angle, subtendDeg);
        }
    } else {
        // TICKING: stop the solid loop; pulse a one-shot whose interval shrinks
        // as the aim approaches the cone (fast just outside, slow far off/behind).
        if (g_state.peg_cue.IsActive()) {
            g_state.peg_cue.Stop();
            acclog::Write("Turret", "peg -> tick mode slot=%d angle=%.1f%s",
                          g_state.selected_slot, angle, behind ? " (behind)" : "");
        } else if (behindEntered) {
            acclog::Write("Turret", "tick behind: pin %s slot=%d angle=%.0f",
                          swingLeft ? "LEFT" : "RIGHT", g_state.selected_slot, angle);
        }
        float frac = (angle - onTargetAngle) / kTickRampDeg;
        if (frac < 0.0f) frac = 0.0f; else if (frac > 1.0f) frac = 1.0f;
        const ULONGLONG interval = kTickFastMs +
            static_cast<ULONGLONG>(frac * (kTickSlowMs - kTickFastMs));
        const ULONGLONG nowT = GetTickCount64();
        if (nowT - g_state.last_tick_ms >= interval) {
            g_state.last_tick_ms = nowT;
            acc::audio::PlayCue3D(kTickCueResref, tickPos);
        }
    }

    const float lateral = std::sin(angle * 0.01745329f) * aimTgtDist;

    // Per-axis error to the INTERCEPT direction (the cue's target).
    const float aimAz = std::atan2(aimDir.x, aimDir.y) * kRad2Deg;
    const float aimEl = std::asin(clamp1(aimDir.z))   * kRad2Deg;
    const float tgtAz = std::atan2(tdir.x,   tdir.y)  * kRad2Deg;
    const float tgtEl = std::asin(clamp1(tdir.z))     * kRad2Deg;
    const float errAz = wrap180(aimAz - tgtAz);
    const float errEl = aimEl - tgtEl;

    // ---- Aim-assist: two modes. --------------------------------------------
    // DEFAULT (always on): magnetism. When the aim is already within
    // kAssistZoneDeg of the locked fighter's intercept, pull the gun kAssistPull
    // of the way toward it per tick — closing near-misses into the real hitbox
    // while leaving acquisition and most of the aiming to the player. A/B-proven
    // ~14x hit-rate vs unaided (which is ~1% — effectively unplayable by ear).
    //
    // "Autoaiming" toggle (opt-in, OFF by default): full lock-on. Pull all the
    // way to the intercept every tick (pull=1, no zone limit), so the turret
    // tracks the locked fighter by itself — for players who want no challenge or
    // have stronger hearing impairments. Same +0x1c4 write path (proven to hold:
    // the magnetism's accuracy gain is the write landing every tick).
    if (aimTgtDist > 0.0f) {
        const bool  fullAuto = acc::menus::modsettings::GetToggle(
                                   acc::menus::modsettings::Option::TurretAutoAim);
        const float zone = fullAuto ? 360.0f : kAssistZoneDeg;
        const float pull = fullAuto ? 1.0f   : kAssistPull;
        if (angle <= zone) {
            // Blend the aim toward the intercept direction; renormalise; write.
            // pull=1 reproduces tdir exactly (full lock-on).
            Vector na = {
                aimDir.x + (tdir.x - aimDir.x) * pull,
                aimDir.y + (tdir.y - aimDir.y) * pull,
                aimDir.z + (tdir.z - aimDir.z) * pull,
            };
            const float nm = std::sqrt(na.x * na.x + na.y * na.y + na.z * na.z);
            if (nm > 0.0f) {
                na.x /= nm; na.y /= nm; na.z /= nm;
                const float naAz = std::atan2(na.x, na.y) * kRad2Deg;
                const float naEl = std::asin(clamp1(na.z)) * kRad2Deg;
                WriteAim(naAz, naEl);
                // Heartbeat log, throttled to ~1/s so normal play isn't spammed.
                const ULONGLONG nowLog = GetTickCount64();
                if (nowLog - g_state.assist_last_log_ms >= 1000) {
                    g_state.assist_last_log_ms = nowLog;
                    acclog::Write("TurretAssist",
                                  "%s slot=%d angle=%.1f az %.1f->%.1f el %.1f->%.1f dist=%.0f",
                                  fullAuto ? "autoaim" : "magnet",
                                  g_state.selected_slot, angle, aimAz, naAz,
                                  aimEl, naEl, sd);
                }
            }
        }
    }

    // ---- Elevation channel: drive the peg's PITCH from the vertical error. --
    // elevErr > 0 -> target ABOVE the aim (glide pitch up, player presses W);
    // < 0 -> below (pitch down, S). Inside the hitbox's elevation tolerance
    // (dead band = subtend) the peg holds its natural pitch — "sounds normal"
    // == on the vertical line. No-op when the peg is silenced (behind-gate) or
    // the 3D voice isn't up yet. The loudness/pan channels are unchanged, so
    // this adds the up/down axis without touching the existing azimuth feel.
    const float elevErr  = -errEl;  // = tgtEl - aimEl
    const float elevBand = (subtendDeg > 0.0f) ? subtendDeg : kFallbackOnTargetDeg;
    float elevBeyond = 0.0f;
    if (elevErr > elevBand)       elevBeyond = elevErr - elevBand;
    else if (elevErr < -elevBand) elevBeyond = elevErr + elevBand;
    float elevOct = elevBeyond / kElevDegPerOctave;
    if (elevOct > kElevMaxOctaves)       elevOct = kElevMaxOctaves;
    else if (elevOct < -kElevMaxOctaves) elevOct = -kElevMaxOctaves;
    const float pitchMult = std::pow(2.0f, elevOct);
    g_state.peg_cue.SetPitchMultiplier(pitchMult);

    // Real engine-scored hit (hp drop). Engine plays its own mgs_sith_hit, so
    // this is diagnostic only. errAngle here is to the intercept; lead-correct
    // shots should now show small errAngle at the hit frame (validates lead).
    if (selectedEnemy && trackingExisting) {
        if (g_state.last_selected_hp >= 0 && hp < g_state.last_selected_hp) {
            acclog::Write("TurretHit",
                          "slot=%d number=%d hp=%d->%d errAngle=%.1f errNow=%.1f "
                          "errAz=%.1f errEl=%.1f leadT=%.2f leadDist=%.0f "
                          "dist=%.0f radius=%.1f subtendDeg=%.1f",
                          g_state.selected_slot, NumberForSlot(g_state.selected_slot),
                          g_state.last_selected_hp, hp, angle, angleNow, errAz, errEl,
                          leadT, leadDist, sd, radius, subtendDeg);
        }
        g_state.last_selected_hp = hp;
    }

    // Lead/velocity diagnostic: relative speed, the intercept lead, and
    // critBoltSpeed (= |v|·dist/radius — the bolt speed below which lead matters).
    acclog::Write("TurretVel",
                  "slot=%d relVelMS=%.1f engineSpeed=%.1f dist=%.0f leadT=%.2f "
                  "leadDist=%.0f leadFrac=%.2f aimDist=%.0f critBoltSpeed=%.0f",
                  g_state.selected_slot, vmag, engineSpeed, sd, leadT, leadDist,
                  leadFrac, aimTgtDist, (radius > 0.0f) ? vmag * sd / radius : 0.0f);

    // LeadCheck: drain any matured predictions against the fighter's real
    // position now, then (throttled) enqueue this tick's linear & curve
    // intercepts in world space for a later maturity comparison.
    DrainLeadProbes(g_state.selected_slot, sp, now);
    if (g_state.have_vel && leadT > 0.0f && now - g_state.lead_probe_push_ms >= 200) {
        g_state.lead_probe_push_ms = now;
        const Vector wLin   = {listener.x + aimRelLin.x, listener.y + aimRelLin.y, listener.z + aimRelLin.z};
        const Vector wCurve = {listener.x + aimRel.x,    listener.y + aimRel.y,    listener.z + aimRel.z};
        PushLeadProbe(g_state.selected_slot, wLin, wCurve,
                      now + static_cast<ULONGLONG>(leadT * 1000.0f));
    }

    // QC accumulation (aim quality at the intercept — the operative on-target).
    {
        const int band = (sd < 100.0f) ? 0 : (sd < 250.0f ? 1 : 2);
        ++g_state.qc_frames;
        ++g_state.qc_n[band];
        if (onTarget) { ++g_state.qc_on_total; ++g_state.qc_on[band]; }
        if (angle < g_state.qc_min_err) g_state.qc_min_err = angle;
        g_state.qc_sum_err += angle;
    }

    acclog::Write("TurretAim",
                  "selected slot=%d errAngle=%.1f errNow=%.1f errAz=%.1f errEl=%.1f "
                  "aim(az=%.1f el=%.1f) tgt(az=%.1f el=%.1f) leadT=%.2f leadDist=%.0f "
                  "lateralMiss=%.0fm dist=%.0f aimDist=%.0f srcDist=%.1f hp=%d/%d "
                  "radius=%.1f subtendDeg=%.1f onTarget=%d elevErr=%.1f elevBand=%.1f "
                  "pitchMult=%.2f",
                  g_state.selected_slot, angle, angleNow, errAz, errEl,
                  aimAz, aimEl, tgtAz, tgtEl, leadT, leadDist,
                  lateral, sd, aimTgtDist, srcDist, hp, maxHp, radius, subtendDeg,
                  onTarget ? 1 : 0, elevErr, elevBand, pitchMult);
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

    // ---- Pass 1: census every in-range fighter (for the Q/E cycle). The
    // engine-loop set is chosen AFTER the census (selected fighter's real ship
    // + nearest other), so it isn't built here.
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

    // Which slots resolve AsEnemy this tick. Compared against slot_alive_last
    // after the walk to announce every fighter that left the field.
    bool aliveNow[kMgoArraySlotCount] = {false};

    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        void* slot = SafeReadPtr(mgoArray,
                                 kMgoArrayObjectsOffset +
                                 static_cast<size_t>(i) * sizeof(void*));
        void* enemy = slot ? CallAsCast(slot, kVtableSlotAsEnemy) : nullptr;
        if (!enemy) continue;

        // Alive in the pool — assign its stable number (idempotent) and note
        // if it's the locked fighter, BEFORE the range cull below.
        aliveNow[i] = true;
        NumberForSlot(i);

        // HP tracking for EVERY fighter (not just the locked one): a drop is a
        // hit on that fighter, and "ever damaged" lets the down-detector below
        // tell a kill from a despawn. Logged so we can see hits on fighters the
        // player isn't locked onto.
        const int hp    = static_cast<int>(SafeReadU32(enemy, kFollowerHpOffset));
        const int maxHp = static_cast<int>(SafeReadU32(enemy, kFollowerMaxHpOffset));
        if (g_state.slot_hp_last[i] >= 0 && hp < g_state.slot_hp_last[i]) {
            acclog::Write("TurretHit", "ANY slot=%d number=%d hp=%d->%d%s",
                          i, g_state.slot_number[i], g_state.slot_hp_last[i], hp,
                          (i == g_state.selected_slot) ? " (locked)" : "");
        }
        if (maxHp > 0 && hp < maxHp) g_state.slot_damaged[i] = true;
        g_state.slot_hp_last[i] = hp;

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

    // Announce EVERY fighter that left the field this tick, by its stable
    // number — not just the locked one (the old logic only saw the selected
    // slot, so kills you weren't locked onto were silent). MUST go via
    // SpeakUrgent (SAPI), NOT the normal NVDA backend: the player holds the
    // fire key during the turret game, and held keys trigger NVDA's
    // cancel-on-keypress, which swallows normal-backend speech entirely (same
    // reason entry/exit use SpeakUrgent). Trade-off: SpeakUrgent interrupts, so
    // two kills within ~1 s clobber — but hearing the last beats hearing none.
    // If clusters prove lossy, serialize via a small FIFO drained one-per-cue.
    // slot_damaged tells a real kill from a possible despawn; for now we
    // announce both as destroyed and log the distinction.
    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        if (g_state.slot_alive_last[i] && !aliveNow[i]) {
            const int num = g_state.slot_number[i];
            if (num > 0) {
                char buf[64];
                std::snprintf(buf, sizeof(buf),
                              acc::strings::Get(acc::strings::Id::FmtTurretDestroyed),
                              num);
                prism::SpeakUrgent(buf);
                acclog::Write("Turret", "down: slot=%d number=%d %s",
                              i, num,
                              g_state.slot_damaged[i] ? "(damaged -> kill)"
                                                      : "(no damage seen -> despawn?)");
            }
            g_state.slot_damaged[i] = false;
            g_state.slot_hp_last[i] = -1;
        }
        g_state.slot_alive_last[i] = aliveNow[i];
    }

    // Fire the delayed second tap of a LEAVE cue (the double-tap that marks
    // "window closed"). Runs every tick, independent of selection, so it lands
    // even if the locked fighter has since left view.
    if (g_state.leave_tap_due_ms != 0 &&
        GetTickCount64() >= g_state.leave_tap_due_ms) {
        acc::audio::PlayCue3D(kRangeCueResref, g_state.leave_tap_pos);
        g_state.leave_tap_due_ms = 0;
    }

    // Q/E selects the locked target; the single peg cue tracks it (beacon when
    // off-aim, swelling on-target) and auto-advances to the nearest fighter
    // when the locked one is destroyed. This is the only continuous cue now —
    // the ambient per-fighter hum was dropped (mostly ignored in play-testing).
    HandleTargetCycle(occSlot, occDist, occPos, occCount, listener);
    DriveSelectedPeg(occSlot, occDist, occPos, occCount, aimDir, haveAim,
                     listener, selectedAliveInPool, selectedEnemy);

    // Hum: continuous loop on the LOCKED fighter at its real (compressed)
    // position — loudness = how close that fighter is, pan = where it really
    // is. Read AFTER DriveSelectedPeg so it tracks the final selection this
    // tick (incl. any auto-advance). Ungated by the behind-gate on purpose:
    // approach awareness matters even when the target is behind (the peg stays
    // front-only as the crosshair). Stops when the locked fighter isn't in the
    // census (out of range / none selected).
    {
        const int hidx = CensusIndexOf(occSlot, occCount, g_state.selected_slot);
        if (hidx >= 0) {
            const float realDist = occDist[hidx];
            Vector humPos = occPos[hidx];
            if (realDist > 0.0f) {
                float srcDist = realDist * kFighterDistanceCompression;
                if (srcDist < kFighterMinSourceDistanceM)
                    srcDist = kFighterMinSourceDistanceM;
                const float kk = srcDist / realDist;
                humPos.x = listener.x + (occPos[hidx].x - listener.x) * kk;
                humPos.y = listener.y + (occPos[hidx].y - listener.y) * kk;
                humPos.z = listener.z + (occPos[hidx].z - listener.z) * kk;
            }
            if (g_state.hum_cue.IsActive()) {
                g_state.hum_cue.UpdatePosition(humPos);
            } else if (g_state.hum_cue.Start(kFighterLoopResref, humPos)) {
                acclog::Write("Turret", "hum on locked slot=%d realDist=%.0fm",
                              g_state.selected_slot, realDist);
            }
        } else if (g_state.hum_cue.IsActive()) {
            g_state.hum_cue.Stop();
            acclog::Write("Turret", "hum off (locked not in census)");
        }
    }

    acclog::Trace("Turret", "fighter census: %d in range (range %.0fm)",
                  occCount, kFighterCueRangeM);
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
    g_state.selected_in_range = -1;
    g_state.selected_on_target = false;
    g_state.selected_behind   = false;
    g_state.last_tick_ms      = 0;
    g_state.leave_tap_due_ms  = 0;
    g_state.have_last_pos     = false;
    g_state.have_vel          = false;
    // Fresh stable-number assignment for this round.
    for (int i = 0; i < kMgoArraySlotCount; ++i) {
        g_state.slot_number[i]     = 0;
        g_state.slot_alive_last[i] = false;
        g_state.slot_hp_last[i]    = -1;
        g_state.slot_damaged[i]    = false;
    }
    g_state.next_number       = 1;
    // Fresh aim-quality counters for the session summary.
    g_state.qc_frames = 0;
    g_state.qc_on_total = 0;
    for (int b = 0; b < 3; ++b) { g_state.qc_n[b] = 0; g_state.qc_on[b] = 0; }
    g_state.qc_min_err = 1e9f;
    g_state.qc_sum_err = 0.0f;
    // Fresh aim-assist accuracy telemetry for this round.
    g_state.assist_last_log_ms = 0;
    g_state.shots_fired  = 0;
    g_state.fighter_hits = 0;
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

    // Session accuracy telemetry: fighter hits per player shot.
    {
        const float pct = g_state.shots_fired > 0
                              ? 100.0f * g_state.fighter_hits / g_state.shots_fired : 0.0f;
        acclog::Write("TurretAssist", "session accuracy: hits=%d shots=%d (%.1f%%)",
                      g_state.fighter_hits, g_state.shots_fired, pct);
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

// ============================================================================
// TurretHitGeom diagnostic handler — hooked at CSWMiniGameObject::OnHitBullet
// (@0x0066c190), which fires once per engine-CONFIRMED bullet hit. Measures the
// REAL collision radius (|impact - fighterCentre|) against the 20 m
// sphere_radius our cue assumes, to settle whether the cue is honest or
// over-generous. Diagnostic only: reads + logs, no behaviour change.
// ECX = the hit-event object (see the constants block above for its layout).
// ============================================================================
extern "C" void __cdecl OnTurretBulletHit(void* hitEvent) {
    if (!hitEvent || !g_state.active) return;

    Vector impact;
    impact.x = SafeReadF32(hitEvent, kHitEventImpactXOffset);
    impact.y = SafeReadF32(hitEvent, kHitEventImpactYOffset);
    impact.z = SafeReadF32(hitEvent, kHitEventImpactZOffset);

    void* follower = CallAsCast(hitEvent, kVtableSlotGetVictimFollower);
    if (!follower) return;

    Vector centre;
    if (!ReadFollowerPosition(follower, centre)) return;

    const float dx = impact.x - centre.x;
    const float dy = impact.y - centre.y;
    const float dz = impact.z - centre.z;
    const float impactDist   = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float sphereRadius = SafeReadF32(follower, kFollowerSphereRadiusOffset);

    // Distinguish our shots landing on a fighter (the hitbox we care about)
    // from incoming fire on the player's own ship. CSWMiniPlayer.follower is
    // the first member, so the player follower pointer == the player pointer.
    void* player = SafeReadPtr(g_state.latched_mini_game, kMiniGamePlayerOffset);

    const bool victimIsPlayer = (follower == player);

    // Accuracy telemetry: count fighter hits (our shots landing).
    if (!victimIsPlayer) ++g_state.fighter_hits;

    acclog::Write("TurretHitGeom",
                  "%s impactDist=%.2fm sphereRadius=%.1f frac=%.2f "
                  "impact=(%.1f,%.1f,%.1f) centre=(%.1f,%.1f,%.1f)",
                  victimIsPlayer ? "PLAYER" : "fighter",
                  impactDist, sphereRadius,
                  (sphereRadius > 0.0f) ? impactDist / sphereRadius : 0.0f,
                  impact.x, impact.y, impact.z,
                  centre.x, centre.y, centre.z);
}

// Player fire counter (hooked at CSWMiniPlayer::Fire @0x0066dc50). One call per
// player fire event; counts PLAYER shots only (enemy fire shares AddBullet, not
// this). Drives the session hits/shots accuracy line. ECX = CSWMiniPlayer (unused).
extern "C" void __cdecl OnPlayerFire(void* /*player*/) {
    if (!g_state.active) return;
    ++g_state.shots_fired;
}

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
