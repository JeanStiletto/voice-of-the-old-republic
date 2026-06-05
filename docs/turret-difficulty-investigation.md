# Turret minigame — difficulty / aim-by-ear investigation

Status: **open**. The turret (Taris-escape gunner) minigame is still effectively
"beatable only with luck" for an aim-by-ear player. This doc captures what we
know so we can resume without re-deriving it.

## The engine turn-speed values (found + confirmed)

WASD aiming and Space firing are **native** KOTOR behaviour — the mod adds no
synthesized turning. The turret swing speed lives in the area `.are`'s MiniGame
struct (dumped from `M12ab.rim` via `build/gffdump`), and is read into the
runtime `CSWMiniGame` struct:

- `MovementPerSec = 100` → azimuth swing-rate cap, in **degrees/second**.
  - Offset in the live struct: `CSWMiniGame +0x74` (float).
  - Empirically confirmed: peak sustained A/D azimuth rate measured ~100–103°/s
    across four sessions (TurretAim `aim(az=..)` per-second deltas).
- `LateralAccel = 1200` → angular acceleration, deg/s². Ramp to full speed =
  100/1200 ≈ 0.083 s, i.e. effectively instant.
  - Offset in the live struct: `CSWMiniGame +0xbc` (float).
- Offsets validated against the GFF dump cross-check: `bump_plane@0x78=3`,
  `depth_of_field@0x7c=5`, `axis_x@0x84=3` (the field our `type==3` discriminator
  actually reads is `axis_x` = Mouse.AxisX, not the GFF `Type` which is at +0x80
  and = 2). See `docs/llm-docs/re/swkotor.exe.h` struct `CSWMiniGame`.
- Other potentially relevant fields: `Player.Accel_Secs = 0.01` (separate ramp,
  possibly the vertical/elevation axis), `Player.Maximum_Speed/Minimum_Speed = 0`,
  `CameraViewAngle = 65`, Gun_Banks→Bullet `Speed = 300`, `Lifespan = 3` s.
- NOTE: other turret sequences (e.g. Leviathan escape) are separate modules with
  their own `.are` and would carry their own copies of these values.

Either edit the `.are` MiniGame struct (Override / module patch; confirm load
order — `M12ab.mod` is ERF and may take precedence over `M12ab.rim`) or overwrite
the live `+0x74` / `+0xbc` floats at runtime in `turret_game.cpp` (the reverted
experiment did exactly this; see git history for the diff).

## Experiment 2026-06-04: halve the turn speed — REVERTED

Scaled the live struct to `movement 100→50 deg/s`, `accel 1200→300 deg/s²`
(runtime overwrite on turret entry, re-asserted per tick). Confirmed applied
(log line `turn-speed refine: movement 100.0->50.0...`; measured peak rate fell
to ~50°/s).

**Result: it made azimuth aiming measurably WORSE.** Compared a full 50°/s
session against an earlier 100°/s session (azimuth error is a clean comparison —
unaffected by elevation behaviour or bolt lag):

- Mean azimuth error when target roughly ahead: 48.5° → **54.7°** (worse)
- Frames within 15° of the target: 14% → **10%** (worse)
- On-target frames: 2.1% → **1.7%** (worse)
- Whole-session mean azimuth error: 91° → **109°**

Interpretation: the bottleneck is **acquisition**, not fine settling. The gun is
pointing ~100° away from the selected target most of the time because fighters
are spread around the full arc and keep moving; a slower gun is worse at whipping
onto a new target, so the player perpetually chases. Slowing was the wrong lever.
Reverted. (If revisited, turn speed should be a live slider so it can be felt
directly, and may need to go *up*, not down, for acquisition.)

## Hitbox RE + measurement (2026-06-04): hits are MESH-based, the cue is HONEST

The long-standing assumption in `turret_game.cpp` — "the engine scores hits
against the fighter's 20 m `sphere_radius`" — was tested and is now **resolved
with a correction and an empirical confirmation**.

### What the decompile shows (the bullet-hit pipeline)

Per frame, `CSWMiniGame::Update` (@0x006735d0) → `CSWMiniGame::DoHitCheck`
(@0x006732f0) walks every live bullet and, for each follower, calls:

- `CSWTrackFollower::DoBulletHitCheck` (@0x0066d4e0) — iterates the follower's
  **model parts** (`this->models`) and calls
- `CSWMiniGame::HitCheckBullet` (@0x006730a0) — a **geometric intersection of
  the bullet against the model-part geometry**.

**None of these reads `sphere_radius`.** `CSWTrackFollower::SetSphereRadius`
(@0x0066c8d0) just does `this->sphere_radius = radius;` and `Load` (@0x0066fff0)
reads `Sphere_Radius` from the GFF into that field — but the model is loaded and
attached on a separate path. So the hit test is **mesh-based**; `sphere_radius`
is inert for collision. The engine's own `SWMG_SetSphereRadius` script command
(and editing the `.are`) would therefore only move OUR cue, not the hitbox.

### What the measurement shows (the `TurretHitGeom` diagnostic)

We hooked `CSWMiniGameObject::OnHitBullet` (@0x0066c190) — fires once per
engine-CONFIRMED hit — and logged `|impact − fighterCentre|` per hit (impact
point at the hit-event object `+0x54/+0x58/+0x5c`; victim follower via
`vtable[0x14]`). One session, 135 hits:

- **Fighter hits (our shots), n=30:** impactDist median **17.3 m**, p90 **20.3 m**,
  max **21.3 m**; frac (÷20 m) median **0.86**, max **1.06**. Hits spread across
  the whole sphere, peaking at the **edge** (frac 0.7–1.0).
- **Player ship (incoming), n=105:** `sphere_radius = 40 m`, sits at world origin
  `(0,0,0)` (the world rotates around the fixed gun); impactDist ≤ 15.2 m, frac
  ≤ 0.38. Twice the fighters' hitbox — why enemies tag you easily.

**Conclusion: the ~20 m hitbox is REAL and the cue is HONEST.** Shots score all
the way out to ~20–21 m, so the cue subtending against 20 m is accurate.
Tightening the cue would make things *worse* (it'd suppress shots that would
connect). The "cue is over-generous" hypothesis is **disproven**.

### Implication for difficulty

20 m is genuinely tight at range (≈5.7° across at 400 m, ≈2.9° at 800 m) — the
difficulty is real geometry, not a miscalibrated cue. To make fighters reliably
hittable by ear we must enlarge the **real** hitbox, and the only lever is
**scaling the fighter model** (hits are mesh-based; `sphere_radius` is inert).
A 2× model scale → ~40 m effective → ~4× hittable area / ~2× angular width
(matches the original "double it to 40" instinct, via the right knob).

Alternative under consideration: **aim-assist** (snap/bias the aim toward a
fighter centre once the ray is within the hitbox). The write surface already
exists (auto-aim writes `CSWMiniPlayer.aim +0x1c4`), so this is engine-cheap but
design-tricky (multi-target arbitration, fairness, fighting manual input).

### Model-scale lever — RE'd 2026-06-04, CONFIRMED viable + LOW complexity

The model-hit pipeline threads an object scale into the geometry test, so
enlarging a fighter's model enlarges its real hitbox:

- `Gob::HitCheck` (@0x0043eb80) calls the root part's HitCheck passing
  **`this->scale`** as the scale argument.
- `Part::HitCheck` (@0x004429d0) combines it with the part-local scale
  (`param_5 * field7_0x24`) and threads it into HitCheckGeom + every child part.
- `PartTriMesh::HitCheckGeom` (@0x00440ff0) multiplies every triangle vertex and
  the bbox by that scale (the `param_5 != 1.0` branch).

`Gob.scale` is exactly the field that `Gob::SetObjectScale` writes — so one call
scales render AND hit geometry together. Engine setters:

- `Gob::SetObjectScale(float scale, bool inherit)` @ 0x00444d90 — `__thiscall`
  on a Gob; also propagates to attachments (vtable[0xcc]).
- `Scene::SetObjectScale(CAurObject*, float)` @ 0x0044fcf0 — one-line wrapper
  that calls the above with inherit=true.

To apply: walk each enemy follower's `models.data[i]` (CAurObject; its Gob is at
offset 0) and call SetObjectScale(2.0). **Verification is free** — the
`TurretHitGeom` diagnostic already logs impactDist; if scaling works, fighter
impactDist should roughly double (frac vs the unchanged 20 m sphere_radius rising
toward ~2.0). Open risks to watch: also scales the fighter's gun/bullet-spawn
offsets (enemy fire may originate farther out); must (re)apply per fighter on
spawn (or per-tick); confirm the bullet path routes through Gob::HitCheck (the
diagnostic settles this instantly).

**Complexity verdict: model-scale is LOW (one engine call per fighter, hit-geom
respects it, instantly verifiable). Aim-assist is engine-trivial but design-hard.
Recommend trying the runtime scale experiment first.**

### Experiment 2026-06-04: 2× model scale — APPLIED but NO hitbox change

Ran `Gob::SetObjectScale(2.0, inherit)` per-tick on every enemy follower's model
(`TurretScale` log confirms 2.0× on all 6 fighters, 1 model each). The
`TurretHitGeom` diagnostic shows the hit radius **unchanged**: fighter impactDist
max **20.3 m** (vs 21.3 m baseline), frac max 1.0; hits 32 (vs 30 baseline). No
improvement.

So scaling the model mesh does **not** enlarge the hitbox — which means the hit
boundary is NOT the mesh after all, despite the `Part::HitCheck` scale plumbing.
The decisive clue: hits cap at **~20 m = `sphere_radius`** in BOTH builds. That
strongly implicates a **sphere broad-phase** gating bullets before the mesh test
(cf. `Gob::HitCheck`'s `SphereIntersect` on `field73_0x138` *before* the part
HitCheck) — i.e. `sphere_radius` (or a bounding sphere) IS the gate, and the
"mesh-based, scale the model" theory was wrong. Hedge: not yet certain whether
the gate field is CSWTrackFollower `sphere_radius (+0x84)` itself or a separate
Gob bounding sphere — the next experiment disambiguates.

### Next experiment (wired 2026-06-04): sphere_radius → 40 (+ keep 2× scale)

Write `sphere_radius = 40` per-tick on each enemy (kept the model 2× too, so a
two-phase sphere+mesh gate would still register). Verify via TurretHitGeom
**impactDist absolute metres** (NOT frac — frac's denominator now reads 40):
impactDist reaching ~40 m → sphere_radius is the knob (ablate the model scale
next); still ~20 m → the gate is a separate Gob bounding sphere, back to RE.

**RESULT: NO change.** impactDist max 21.4 m, median 17.2 m — identical to
baseline. So BOTH obvious levers are falsified: neither the model mesh scale nor
`sphere_radius (+0x84)` moves the hit boundary. The ~20 m hit sphere is robust.

### Status: both hitbox levers dead; candidates + pivot

The gate is neither field we can poke. Remaining hitbox candidates (would need
more RE, and may be LOAD-TIME only — i.e. only changeable via a modified model in
Override, not at runtime):
- `Model.radius` (Model+0x80) — bounding sphere computed from the mesh at load;
  `SetObjectScale` likely doesn't recompute it.
- `Gob.field73_0x138` / `field74_0x13c` — the sphere `Gob::HitCheck`'s
  `SphereIntersect` broad-phase uses; trace whether the bullet path even routes
  through `Gob::HitCheck` (HitCheckBullet's geometry call is on the BULLET's
  gob vtable[0x88] — may bypass the target's Gob::HitCheck entirely).

**Pivot under consideration (the dev's own earlier idea): aim-assist / magnetism**
instead of enlarging collision. Bias the aim toward the Q/E-LOCKED fighter's
centre once the ray is within a magnetism zone (~40 m-equivalent angle), pulling
the player into the real 20 m hitbox — doubling the EFFECTIVE catch radius
without touching collision. Single-target (no arbitration — player already
locked it), engine-cheap (write `CSWMiniPlayer.aim +0x1c4`, infra exists in the
TurretAutoAim easy-mode path), tunable strength (partial pull ≠ "plays itself"),
opt-in. NOTE: the experimental 2× scale + sphere=40 writes in turret_game.cpp are
confirmed no-ops with side effects (visual scale; sphere overwrite widens the cue
subtend so the cue now lies) — REVERT them whichever path we take.

The diagnostic hook + handler live behind no gate but `g_state.active`; they are
read-only and safe to leave in while we decide. See `OnTurretBulletHit` in
`turret_game.cpp` and the `hooks.toml` block at `0x0066c190`.

### Centering improved across S1→S2→S3 — but it was SKILL, not the change

Build-independent centering metric (raw aim error in deg, from TurretAim — immune
to the cue/sphere changes), forward frames only:
- S1 (baseline):       within 6° = 0.3%, within 15° = 5.5%, mean 48.8°
- S2 (2× scale):       within 6° = 1.1%, within 15° = 15%,  mean 40.9°
- S3 (2× + sphere=40): within 6° = 3.8%, within 15° = 19.6%, mean 40.0°

S3 *felt* much better and objectively centred better. BUT S1→S2 also improved a
lot with NO perceptible change (scale inert, cue unchanged) — a strong
skill/warm-up/variance trend that alone explains S3. And the dev's own logic
clinched it: sphere=40 makes the cue LIE (solid at 40 m while real hits need
20 m), which should *hurt* firing — so the gain was the player improving, not the
change. Conclusion: runtime hitbox enlargement is dead; keep the cue honest @20 m.

### PIVOT (built 2026-06-04): aim-assist magnetism

Reverted the 2× scale + sphere=40 writes (and SafeWriteF32). Added partial-pull
magnetism in `DriveSelectedPeg`: when aim error to the locked fighter's intercept
is within `kAssistZoneDeg` (12°), blend the aim toward it by `kAssistPull` (0.35)
per tick and write via `WriteAim` (+0x1c4). Gated behind the existing
`TurretAutoAim` toggle ("Auto-aim turret (easy mode)"). Replaces the old
full-snap auto-aim that "didn't work". A `TurretAssist` readback line logs last
tick's written az/el vs this tick's live read: **small delta = write HELD; large
delta = engine clobbers +0x1c4 each frame** (would explain the old breakage and
mean we need a hook at the engine's aim-apply point, not a poll write). Verify
next session from `TurretAssist` lines + objective within-6°/15° centering.

### Readback verdict + A/B proof: the magnet works (~14×), and the write HOLDS

Readback (311 samples): |dAz| mean 2.4°/tick (our pull + the engine's swing
momentum, not a clobber — pull sequences converge 11.6°→6.8°→3.6°→1.7°, matching
the 35%/tick math, which only happens if writes accumulate), |dEl| mean 0.06°
(elevation holds tightly). So the old "auto-aim didn't work" was NOT a broken
write path. Then a within-session A/B (20 s blocks, magnet auto-alternated ON/OFF,
hits/shots tallied per mode via the `CSWMiniPlayer::Fire` @0x0066dc50 shot hook
that counts PLAYER shots only):

- **Assist ON:** 35 hits / 309 shots = **11.3%**
- **Assist OFF:** 2 hits / 240 shots = **0.8%**

A **~14× accuracy gain at constant in-session skill** — luck and skill ruled out.
Also confirms raw aim-by-ear is ~0.8% (effectively unplayable), which is why this
minigame was "beatable only by luck".

### FINAL DESIGN (shipped 2026-06-04)

Two modes, A/B scaffolding removed (no more 20 s alternation in normal play):
- **Default (always on): magnetism** — `kAssistPull=0.5`, `kAssistZoneDeg=15°`.
  Skill-based but playable by ear.
- **"Autoaiming" toggle (`TurretAutoAim`, OFF by default): full lock-on** — same
  code path with pull=1 / no zone limit, so the turret permanently tracks the
  locked fighter. For no-challenge / stronger hearing impairment.
Toggle label renamed to "Autoaiming" (all 5 locales). `TurretAssist` now logs a
throttled (~1/s) heartbeat + a session `hits/shots` accuracy line at exit. The
`OnPlayerFire` + `OnTurretBulletHit` hooks stay for that telemetry.

## Two findings that are NOT what they look like (artifacts)

1. **Hits logged with the gun 100–177° "wrong" in azimuth** are **bolt-travel
   lag**, not an engine mystery. Bolts are slow (Speed 300, Lifespan 3 s); a shot
   at a 230 m fighter takes ~0.8 s to arrive, and the hp-drop (our `TurretHit`
   diagnostic) is logged on *arrival*, by which time the gun has swung away. The
   hit-frame `errAz` therefore does NOT measure firing alignment — don't tune off
   it. This is also why long range is so unforgiving: it's lead + timing, not
   point-and-shoot.

2. **Aim elevation "frozen at 9.0°"** in the 2026-06-04 test sessions is
   **behavioural, not a bug**. Earlier sessions show elevation actively worked
   from 9° up to ~52°, so W/S elevation control functions. In the test runs the
   player simply aimed azimuth-only. Targets span −7° to +73° elevation, so not
   using the vertical axis guarantees misses on high/low fighters — but the axis
   is available.

## Cue health check (2026-06-04 log) — lead + pan are NOT broken

The developer wondered whether the lead and behind-pan cues had regressed. Log
evidence says no:

- **Dynamic range-gated lead — working as designed.** `TurretVel` over the
  session: mean relative speed 69 m/s (healthy EMA, not zero/garbage), and
  `leadFrac` ramps cleanly by range — 0.19 close (<100 m, lead correctly
  suppressed), 0.91 mid, 0.93 far; applied lead distance 5 m → 47 m → 104 m.
  This is exactly the `kLeadGateLowFrac`/`kLeadGateHighFrac` design.
- **Behind-pan steering — firing correctly.** 12 behind-pin transitions, each
  picking LEFT/RIGHT sensibly by angle (95–139°). The code path runs and chooses
  the right side. (Whether the *audible* pan reaches the hard L/R extreme is an
  ear-test, not a log fact — verify by ear if redesigning — but nothing in the
  log suggests it's misbehaving.)

## The likely real culprit: target is BEHIND 64% of the time

In the test session the selected target's aim error was **>90° (behind) in 64%
of frames** (only 36% forward). By our own design the peg tone is SILENCED in the
rear hemisphere (behind-gate) and we fall back to sparse directional tick pulses.
So for ~two-thirds of the round the player was working from intermittent pings —
the *quietest* version of the cue — while needing to swing 90–180°. That is a
design consequence, not a regression, and is a more plausible reason it "felt
uncontrolled" than the turn speed or any broken cue.

Contributing factor: auto-select locks the NEAREST fighter, which is frequently
beside/behind the gun, so the player is repeatedly told "swing all the way
around" via the least-informative cue. Candidate directions to explore (NOT yet
tried): bias target selection toward fighters already in the forward arc; keep a
(quieter) continuous tone audible while behind so the rear swing isn't flown on
pings alone; or make the behind tick rate carry more "how far still to swing"
information.

## How to re-measure (screen-reader-friendly)

- Per-session aim quality: grep `TurretQC` for the exit summary (within-hitbox %
  by range band, mean/min errAngle).
- Per-frame: grep `TurretAim` (errAngle, errAz, errEl, aim/tgt az+el, lead,
  onTarget). `TurretVel` for velocity/lead. `TurretHit` for engine-scored hits
  (remember the bolt-lag caveat above).
- Turn rate: per-second deltas of `aim(az=..)` from the `TurretAim` lines.

## 2026-06-05 — DECISIVE: writing +0x1c4 does not steer the bolts

A probe round forced full-autoaim to write the aim ~65–70° UP (at the sky).
Bolts still hit fighters **30×** — unchanged from the ~31 of an honest-aim
autoaim round. So `CSWMiniPlayer +0x1c4` (the aim value we read for cues and
write for autoaim/magnetism) is **not** what the firing path reads.

Decompile of the fire path (Ghidra, Lane's DB):
- `FireGunCallback @0x00673a40` is the sole caller of `CSWMiniGame::AddBullet
  @0x00672fa0`. It spawns the bolt from the gun MODEL's **"bullethook" node**
  (gun CAurObject `vtable+0x98` query → world pos+orient of that barrel node),
  and — when the gun is a homing gun (`ctx->vtable[2]() != 0`) — bends the bolt
  toward an **engine target** (`gun->vtable[0x108]()`) with random spread.
- `CSWMiniPlayer::Fire @0x0066dc50` (our `OnPlayerFire` shot-counter hook) is
  the player-fire entry that iterates the gun banks into this path.
- `CSWMiniGame::UpdateAxis @0x00670fb0` only handles type 1/2 (not turret=3).

**Implication:** the entire +0x1c4-write aim-assist (full autoaim AND the
"14×" magnetism) steers a value the bolts ignore. The barrel is driven by the
player's real view/input (WASD, ~100°/s native). Assisted mode works via the
**cue** guiding the player's own aim, not the write. The curve-aware lead
(committed) sharpens that cue and is independently valuable.

**Open task:** find the real turn channel — the gun-orientation field the
barrel/bolt actually reads, or the engine target the bolt homes on
(`gun vtable[0x108]`), or synthesize the input that moves the real aim. The
flight-time makeability gate + switch-on-hit spreading (committed on
`turret-curve-lead`) are correct and unblock the moment the barrel turns.
Re-arm the probe via `kDiagElevBias = 60.0f` in turret_game.cpp to re-confirm.

## Related

- `patches/Accessibility/turret_game.cpp` — all turret accessibility logic.
- Memory: `project_turret_turn_rate.md`,
  `project_turret_aim_write_does_not_steer_bolts.md`.
