# Turret / swoop minigame — engine-defined model (CANONICAL)

This is the **engine-confirmed** model of the KOTOR minigame subsystem, read
directly from Lane's Ghidra DB (decompile, 2026-06-05). It supersedes every
runtime-measured theory in `docs/turret-difficulty-investigation.md` and in the
older turret memories. When in doubt, this document and the decompile win over
any statistic gathered by hooking the live game.

The whole subsystem is **155 functions** (query: SARIF `FUNCTIONS` in the
`CSWMiniGame`/`CSWMiniPlayer`/`CSWMiniEnemy`/`CSWTrackFollower`/`CSWMG*` classes).
The aiming/firing/hitting path below is fully read end to end; ~130 peripheral
functions (setters, obstacles, sound, dtors) are not, and don't change the model.

## The model in one paragraph

The turret is a **rotating-emplacement rail shooter** (`CSWMiniGame.type == 2`).
You don't move — you **rotate your aim**. WASD/mouse accumulate into a world
`axis_velocity`, which `CSWMiniPlayer::Control` integrates into the player's
`offset` Vector. For type 2, `KeepInTunnel` wraps `offset` components at 0–360°,
so **`offset` (`CSWMiniPlayer +0x1c4`) IS the aim angle: x = elevation,
z = azimuth, in degrees.** A "camerahook" model is rotated by that aim; the
camera and the gun rotate with it (models flagged `RotatingModel` in the `.are`).
Your gun fires **straight, ballistic** bolts from the gun model's `bullethook`
node along that node's world orientation — no lead, no spread, no homing. A hit
is a per-tick swept test of the bolt segment against the fighter's **model
geometry** (skipping the shooter). Enemy guns, by contrast, auto-target you.

## Minigame types (from `CSWMiniGame::Load @0x6723d0`)

The loader only ever sets `type` to **1 or 2** (`if (Type==1 || Type==2) this->type = Type`):
- **type 1 = swoop bike.** `MovementPerSec` default 6. `UpdateAxis` negates y,z.
- **type 2 = turret.** `MovementPerSec` default 90 (our M12ab area overrides to
  100). Reads `Camera` + `CameraRotate`; `UpdateAxis` swaps x↔z; `KeepInTunnel`
  wraps the aim 0–360°. This is the gunner minigame.

WARNING — struct offset bug in our code: Lane's layout puts `type` at
`CSWMiniGame +0x80` and `axis_x` at `+0x84`. **Our `turret_game.cpp` reads `+0x84`
as "type"** (so it sees `axis_x`, not `type`) and treats turret as `==3`,
swoop as `==0`. It works only by coincidence (the two areas happen to differ at
`+0x84`). The real values are turret `type==2`, swoop `type==1`. Fix the offset
to `+0x80` and the constant to 2 when next touching that file.

## The aim chain (input → where bolts go)

1. **Input.** `CClientExoAppInternal::ProcessInput @0x6227e0` →
   `CSWMiniGame::UpdateMouse @0x671020`: mouse/key deltas × `MovementPerSec` × dt
   accumulate into `axis_velocity` (`CSWMiniGame +0xe4`, a world Vector). Which
   world axis horizontal vs vertical drives — and its sign — come from the `.are`
   `Mouse` struct fields `AxisX`/`AxisY` (+ `FlipAxisX`/`FlipAxisY`), read in
   `CSWMiniGame::Load` into `axis_x` (+0x84) / `axis_y` (+0x88).
2. **Integration.** `CSWMiniGame::Control @0x6710e0` → `CSWMiniPlayer::Control
   @0x66d640`: `offset += (axis velocity this frame)`, then `KeepInTunnel` clamps
   (linear bounds) or wraps (0–360° for type 2). So `offset` is the **current aim
   angles** (az/el degrees). `Start_Offset_*` seeds it; the `Tunnel*` `.are`
   fields bound it (or `TunnelInfinite` → free 360° wrap).
3. **Rotation.** `SetCamera @0x671670` (type 2) attaches the camera to the
   player's camerahook model; the camerahook is rotated by `offset`, carrying the
   camera and the `RotatingModel`-flagged gun with it. Position never changes —
   only orientation. (This is why a creature/position probe reads "fixed at
   origin": the emplacement doesn't translate. The thing that rotates is the
   camerahook/gun, oriented by `offset`.)

## Firing (where a player bolt actually goes)

`CSWMiniPlayer::Fire @0x66dc50` → for each bank `CSWMGGunBank::Fire @0x674090`
plays the gun's "fire" animation. The animation event `fire%d` calls
`FireGunCallback @0x673a40`, which:
- Looks up the gun model part `bullethook%d` (gun-model `gob` vtable `+0x98`) →
  its **world position + orientation**.
- Spawns the bolt there. The lead/spread/homing block is gated on the gun being a
  **targeting** gun (`CSWMGBehaviorTrackAndFire`); for the player's plain gun
  (`CSWMGBehaviorFire`) it is **skipped entirely**.
- Attaches `CSWMGBehaviorBullet` and `CSWMiniGame::AddBullet @0x672fa0`.

So **the exact fire line = the gun's `bullethook` node world orientation.** Read
it the same way `FireGunCallback` does. Bolt kinematics from the `.are`
Gun_Banks→Bullet: `Speed = 300`, `Lifespan = 3 s`.

Player gun vs enemy gun (from `LoadGun`/`CreateGunBank`/`SetGunModel`):
- **Player** → `CSWMiniPlayer::CreateGunBank` → plain `CSWMGGunBank` +
  `CSWMGBehaviorFire` (behavior id `0xaaad`). No target. Straight bolts.
- **Enemy** → `CSWMiniEnemy::CreateGunBank` → `CSWMGTargettingGunBank` +
  `CSWMGBehaviorTrackAndFire` (id `0xaaaa`), constructed with the player follower
  as its target, plus sensing radius / horizontal+vertical spread / inaccuracy
  (`CSWMGTargettingParameters`). Enemy bolts home/lead onto you.

## Hit detection (how a bolt scores)

`CSWMiniGame::Update @0x6735d0` → `DoHitCheck @0x6732f0`: for every live bullet,
`CSWTrackFollower::DoBulletHitCheck @0x66d4e0` skips the shooter
(`bullet behavior +0x3c == follower`) then calls `CSWMiniGame::HitCheckBullet
@0x6730a0` against each of the follower's **model parts** — a swept segment test
of the bullet's path this tick vs the fighter's model geometry (bullet `gob`
vtable `+0x88`), filling the impact point. **Pure ballistics.** `sphere_radius`
(`CSWTrackFollower +0x84`) is NOT the collision primitive — it's inert for hits
(see [[project_turret_hitbox_mesh_based]]); the effective ~20 m we measure is the
fighter model's collision volume, not runtime-enlargeable.

`CSWMiniPlayer::DoFollowerHitCheck @0x66eda0` is a different thing — the player
**ship-vs-enemy collision** (ramming / `bump_damage`), a sphere test, not bolts.

## Key struct offsets (Lane's layout; cross-check before relying)

CSWMiniGame: `player +0x24`, `enemy_count +0x30`, `type +0x80`, `axis_x +0x84`,
`axis_y +0x88`, `clip_start +0x68`, `movement_per_Second +0x74`,
`lateral_acceleration +0xbc`, `field48 (CSWMGPhysicsState) +0xc0`,
`axis_velocity +0xe4`.

CSWMiniPlayer (IS-A CSWTrackFollower at +0): `offset (= AIM az/el deg) +0x1c4`,
`min_speed +0x1d8`, `max_speed +0x1dc`, `origin +0x208`,
`field27 +0x234`, `field28 (orientation Quaternion) +0x240`, camerahook obj +0x220.

CSWTrackFollower: `mini_game +0x60`, `models (CExoArrayList) +0x68`,
`gun_banks +0x74`, `gun_bank_count +0x78`, `sphere_radius +0x84`, `hp +0x8c`,
`max_hp +0x90`, `speed +0x98`, `invulnerability +0x9c`, `bullet +0x174`.

CSWMGGunBank: `gun_model_resref +0x14`, `guns array +0x34`, `gun count +0x38`,
`bullet (CSWMGBullet) +0x40`, `owner follower +0x68`.
CSWMGBullet: `speed +0x4`, `rate_of_fire +0x8`, `life_span +0xc`, `target_type +0x10`.

## Implication for the accessibility cue

- **Crosshair = the `bullethook` node world transform**, read directly (walk
  player → `gun_banks` → gun model → query part `bullethook0`). Exact direction
  the next bolt travels. No EMA, no bolt-travel measurement (that read off
  `Gob+0x78` was an artifact ~85° off — never use it again).
- **Hittable** = lead-corrected fighter within its collision radius of the
  bullethook ray (the engine's own ballistic test). Lead matters; fighters move
  ~74 u/s.
- **Which key to swing** = project the fire-line→fighter error onto the
  `axis_x`/`axis_y` world axes (read from the struct).
- **Aim-assist / autoaim** = write `offset` (az/el) **after** `Control` runs each
  tick (else it's re-integrated/clobbered — the reason past one-shot writes to
  `+0x1c4` "did nothing"), steering the aim onto the locked fighter.

## What we got wrong, and why (so we don't redo it)

- "Gun is fixed, the world rotates around it" — **wrong framing**. The emplacement
  doesn't translate, but the camera+gun **rotate** with `offset`. It only *felt*
  like the world sweeping because the camera turns.
- "`+0x1c4` is irrelevant to bolts" — **wrong**. `offset`/`+0x1c4` IS the aim. The
  prior "proof" was a double artifact: `Control` re-integrates `offset` every tick
  (one-shot writes clobbered) AND it was measured through the broken `Gob+0x78`
  bolt read.
- "Live fire line = measured bolt travel" — **artifact**, ~85° off ground-truth
  hit directions. Replaced by reading the `bullethook` transform.

Every one of these came from inferring a model out of noisy runtime statistics
instead of reading the engine. `project_turret_turn_rate` had the correct reading
(+0x1c4 = aim degrees, CameraRotate=1) from the start; a later measurement-driven
session overwrote it. Read the engine first. See
[[feedback_reconstruct_engine_structures_first]].
