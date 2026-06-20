# Swoop accelerator-pad hit detection — engine-defined model (CANONICAL)

Engine-confirmed model of **how the swoop bike "hits" an accelerator pad**, read
directly from Lane's Ghidra DB (decompile, 2026-06-20) plus the extracted area
GFFs (`build/swoop-rim/*.are.xml`) and the boost script (`accelpad.ncs`). When in
doubt, this document and the decompile win over any runtime-measured theory.

Motivation: for a screen-reader-only player the worry was that the "successful
hit" window might be frame-tight or sub-unit precise. **It is not.** The window
is a fixed-radius sphere overlap evaluated with continuous collision detection
(no tunneling). The whole difficulty is *lateral alignment* (knowing the pad's
lane), which is exactly what the spatial-audio pan is for — not timing.

## The model in one paragraph

Accelerator pads are **trigger enemies** (`CSWMiniEnemy`, `Trigger=1`), one per
pad, each riding its own short looping track (`mgt02..mgt31`, `Num_Loops=-1`).
Every engine tick, `CSWMiniGame::DoHitCheck` calls
`CSWMiniPlayer::DoFollowerHitCheck`, which for each pad runs **one swept
sphere-vs-sphere test** between the player bike and the pad. A hit fires the
player's `OnHitFollower` script (= `accelpad.ncs`) **once per tick**. The test
radius is the **sum of the two `Sphere_Radius` values** — Taris **6.0**,
Tatooine/Manaan **5.0** world units. The test is full 3D and *swept* (it checks
the path travelled this frame, not a single snapshot), so even at top speed you
cannot skip past a pad. There is no timing window, no per-frame button press, no
speed gate on the collision itself: be within the combined radius of the pad's
centre as you pass it and it triggers.

## Object layout (what an accelpad is)

From the extracted area GFFs (all three swoop modules agree on structure):
- `<list label="Enemies">` — 30 entries. Each:
  - `Trigger = 1` (collision-only; no guns, no bullets)
  - `Sphere_Radius` = **3.0** (Taris `tar_m03mg`) / **2.0** (Tatooine `tat_m17mg`,
    Manaan `manm26mg`)
  - `Num_Loops = -1` (rides its `mgt##` track forever), `Hit_Points = 1`,
    `Bump_Damage = 0`, `Invince_Period = 0`
  - model `mgf_accelpad01`, death sound `mgs_accelpad`
  - **empty `OnHitFollower`** on the pad itself — the script that runs lives on
    the *player* (see below).
- `<struct label="Player">`:
  - `Sphere_Radius` = **3.0** (all three tracks)
  - `OnHitFollower = accelpad`, `OnHitObstacle = obstacle`, `OnFire = onfire`
  - `Minimum_Speed = 0`, `Maximum_Speed = 0` (the real speed ladder is set at
    runtime by the per-track NWScript, not the GFF)

Engine spawn types: pads are `CSWMiniEnemy` (extends `CSWTrackFollower`),
reachable via the MGO-array downcast `vtable[0x1c] = AsEnemy`. Rocks are a
*separate* pool (`CSWMGObstacle`, `vtable[0x20] = AsObstacle`). See
`swoop_spatial_audio.cpp` for the two-pass sweep we already run.

## Combined hit radius (the only "size" number that matters)

```
hit_radius = player.Sphere_Radius + pad.Sphere_Radius
```
- **Taris (tar_m03mg):** 3.0 + 3.0 = **6.0**
- **Tatooine (tat_m17mg):** 3.0 + 2.0 = **5.0**
- **Manaan (manm26mg):** 3.0 + 2.0 = **5.0**

`Sphere_Radius` is loaded in `CSWTrackFollower::Load @0x66fff0`
(`ReadFieldFLOAT "Sphere_Radius"`, default −1 = "leave as constructor value").
Constructor `CSWTrackFollower::CSWTrackFollower @0x66dee0` sets `sphere_radius =
0` initially, so the GFF value is authoritative. Fields: `sphere_radius` at
`CSWTrackFollower +0x84`; `trigger` at `+0x1a0`; `bump_damage` at `+0x94`.

## The hit test (exact geometry)

`CSWMiniGame::DoHitCheck @0x6732f0` → `CSWMiniPlayer::DoFollowerHitCheck
@0x66eda0`. Per pad, it gathers four points from the bike and the pad model:

- `player_now`  = bike model `Gob::GetPosition`  (model vtable **+0x64**)
- `player_prev` = bike model `Gob::GetPreviousPosition` (model vtable **+0x6c**)
- `pad_now`     = pad model `Gob::GetPosition`
- `pad_prev`    = pad model `Gob::GetPreviousPosition`

then builds two **relative-position** vectors and tests them:

```
relPrev = player_prev - pad_prev      // relative position last frame
relNow  = player_now  - pad_now       // relative position this frame
hit = SphereIntersect(relPrev, relNow, ORIGIN(0,0,0), hit_radius)
```

`Global::SphereIntersect @0x4abdd0` is a **point-to-segment distance test**:
it returns 1 iff the minimum distance from the ORIGIN to the line segment
`[relPrev → relNow]` is `< hit_radius`. The origin is the point where the two
sphere centres coincide (relative position zero = contact). So geometrically:

> **"Did the gap between bike and pad dip below `hit_radius` at any point on the
> straight path it travelled this frame?"**

This is textbook **continuous collision detection** (swept test). The segment
spans the whole frame's relative motion, so the closest approach *between*
frames is caught. `vector_magnitude` is a true 3D length — x (lateral), y
(forward), z (vertical) all count.

(`SphereIntersect` is generic: param order is `(segStart, segEnd, point,
radius)`. Here segStart/segEnd are the previous/current relative positions and
point is the origin.)

### Why high speed does NOT make pads un-hittable

At 30 fps and Taris gear-3 speed (~190 u/s) the bike moves ~6.3 units **per
frame** — about one whole hit-diameter. A naive *snapshot* test ("is the bike
within 6.0 of the pad right now?") would therefore be genuinely frame-tight: you
could straddle a pad's Y plane between two frames and never register inside the
sphere. The engine avoids this precisely by testing the swept segment instead of
the instant, so the hit is **speed-independent**: only lateral alignment
matters, never timing. This is the single most important finding for
accessibility — there is no reflex/timing component to remove.

## What firing the hit does (the boost) — `accelpad.ncs`

On a hit, `DoFollowerHitCheck` sets `last_follower_hit_index` on both bodies and
calls `OnHitFollower` on the player (and on the pad, whose script is empty). The
player's `OnHitFollower = accelpad` runs as a `StartingConditional`:

1. `gear = SWMG_GetGlobalNumber("MIN_RACE_GEAR")`.
2. If `SWMG_GetIsInvulnerable(player) == 0` (not in post-hit cooldown) **and**
   `gear != 5`:
   - `SWMG_SetPlayerAccelerationPerSecond(current * 1.10)`  (+10% accel)
   - `SWMG_SetPlayerSpeed(current * 1.05)`                  (+5% speed)
3. Always (even while invulnerable): play `boost` animation,
   `SWMG_StartInvulnerability(player)`, `SWMG_AdjustFollowerHitPoints(-100,-100)`
   on the last follower hit.

So the booster is **+5% speed / +10% acceleration**, *gated* on:
- not currently invulnerable (each hit restarts the player invulnerability
  window — player `Invince_Period = 0.3 s`), and
- not already at max gear (`gear == 5` → no speed/accel gain, but the boost
  animation/sound still play).

The 0.3 s invulnerability means rapidly clipping several pads in a row only
boosts on the first; the rest are cosmetic until the window expires.

## Speed effects: what changes your speed (and what doesn't)

Decompiled / script-confirmed 2026-06-20:

- **Accelerator pad** (`accelpad.ncs`): speed ×1.05, accel-per-sec ×1.10. Gated
  on not-invulnerable and `gear != 5`.
- **Obstacle / rock** (`obstacle.ncs`, player `OnHitObstacle`): speed **×0.7**
  (a 30% cut) per hit, plays the "Damage" sound + damage animation. Gated on
  `gear > 0`. **This is the only thing that slows you on a normal run.**
- **Track side / wall: NO speed effect.** `CSWMiniPlayer::KeepInTunnel @0x66cb40`
  only *clamps* the lane offset to `[tunnel_neg+origin, tunnel_pos+origin]`
  (≈ ±20). It never touches `speed`. Grinding the wall costs you steering room,
  not velocity. (Our DLL's "wall impact" cue is a heuristic on lateral-motion
  stall — it does not correspond to any engine penalty.)
- **Enemy ram** (`DoFollowerHitCheck` bump branch, speed → `min_speed`): does
  **not** occur on swoop — every "enemy" is a trigger accelpad, never a hostile
  follower, so the bump branch never runs.

Live gear bands (Tatooine `tat_m17mg`, min..max speed per gear, from snapshots):
gear 1 [35..70], gear 2 [60..120], gear 3 [100..190], gear 4 [150..225] u/s. The
bike auto-accelerates toward the gear max; manual accelerate (`onfire`) raises
the band one notch.

## The real difficulty = lateral alignment (quantified)

Live capture (Taris, `tar_m03mg`, patch log accelpad dump). Each pad sits at a
fixed lane X with Z = 0 (on the road), spread along the forward Y axis:

- Pad X coordinates cluster in **X ≈ 80 .. 120** → drivable lane ≈ **40 units
  wide**, centred near X ≈ 100 (matches the ±20 tunnel half-width plus a +100
  origin). The wall-collision code already uses ±20 lane edges.
- With `hit_radius = 6.0`, the catch zone is **±6 around the pad's X = a
  12-unit-wide band = ~30% of the lane** when the pad is mid-lane. Forgiving —
  *if* you know the pad's lane.
- Pads are deliberately **scattered left/centre/right**. Consecutive examples:
  slot 48 at X≈82.9, then slot 49 at X≈119.5 — a ~36-unit lateral swing
  (nearly the full lane) over ~110 units of forward travel. So you cannot hold
  one lane; each pad demands an active steer to its specific X.
- Forward spacing between pads ≈ 60–145 units. At gear 3 (~190 u/s) that is
  roughly **0.3–0.8 s** of reaction time to be in the right lane — short, but a
  *steering* deadline, not a *timing* deadline (the pad doesn't have to be hit
  on a frame; it just has to be passed within 6 units laterally).

Player lateral agility: area `LateralAccel = 300`, `MovementPerSec = 100`
(`m03mg.are`). Forward speed ladder is script-driven; live-observed maxima
~70 (gear 1) / ~120 (gear 2) / ~190 (gear 3) u/s.

**Accessibility implication:** the cue the player needs is *"the next pad is N
units to your left/right"* with enough lead to steer there before the Y crossing
— precisely the decoupled lateral-pan + real-distance-loudness cue in
`swoop_spatial_audio.cpp::TickAccelpadCues`. The ±6 (or ±5) catch band is the
tolerance budget the pan has to land inside. There is no benefit to any
timing/press cue — the hit is positional and swept.

## Pads move (don't assume stationary)

`CSWTrackFollower::CSWTrackFollower @0x66dee0` defaults `speed = 100.0`, and
`CSWMiniEnemy::Load @0x6705f0` reads **only** `Trigger` (never a speed field), so
each pad keeps the default and rides its `mgt##` track via the `"track"`
animation at rate `speed * 0.01 = 1.0` (`CSWTrackFollower::Go @0x6701c0`),
looping forever. Position is whatever the track animation currently places the
model at (`GetPosition` → model vtable+0x64). The CCD test reads each body's
*previous* and *current* position independently, so pad motion is fully
accounted for — there is no "stationary target" assumption anywhere.

## Key addresses & offsets (Lane's layout; cross-check before relying)

- `CSWMiniGame::DoHitCheck @0x6732f0` — per-tick driver; calls
  `DoFollowerHitCheck` up to 3× (collision resolve passes); the `OnHitFollower`
  fire is gated to the **first** pass (`param_2 = (iter == 0)`).
- `CSWMiniPlayer::DoFollowerHitCheck @0x66eda0` — the per-pad sphere test +
  trigger branch (the accel-pad path) vs. the bump-physics branch (real enemies
  / `DoBumping`).
- `Global::SphereIntersect @0x4abdd0` — point-to-segment distance < radius.
- `Gob::GetPosition @0x449980` (model vtable +0x64) /
  `Gob::GetPreviousPosition @0x4499d0` (vtable +0x6c).
- `CSWTrackFollower::Load @0x66fff0` — reads `Sphere_Radius`, `Num_Loops`,
  `Bump_Damage`, `Invince_Period`.
- `CSWMiniEnemy::Load @0x6705f0` — reads `Trigger` only.
- `CSWTrackFollower::CSWTrackFollower @0x66dee0` — default `speed = 100.0`,
  `hp = 100`, `looping = 1`.
- Struct: `CSWTrackFollower.sphere_radius +0x84`, `.bump_damage +0x94`,
  `.speed +0x98`, `.invulnerability +0x9c`, `.invincibility_period +0xa0`,
  `.trigger +0x1a0`. `CSWMiniPlayer.offset (tunnel x/y/z) +0x1c4`,
  `.min_speed +0x1d8`, `.max_speed +0x1dc`, `.acceleration +0x1e0`.
- Boost script: `accelpad.ncs` (decompiled to `build/swoop-rim/accelpad.dis`):
  +5% speed, +10% accel, gated on not-invulnerable and `gear != 5`.

## What we got right already / didn't need to change

- `swoop_accelpads_are_enemies` (in `camera-and-swoop.md`) was correct: pads are
  the Enemies pool, not obstacles. This doc supersedes its open question about
  the position field — it's the model's `Gob::GetPosition` (vtable+0x64), exactly
  as `swoop_spatial_audio.cpp::ReadTrackFollowerPosition` already reads.
- No engine hook is needed to *detect* a hit for cueing purposes; if we ever
  want a "you hit a pad!" confirmation, hook `OnHitFollower` dispatch or watch
  the player speed step up, rather than re-implementing the sphere test.
