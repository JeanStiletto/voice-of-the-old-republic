# KOTOR 2 swoop race — controls, gear ladder and the jump (engine-defined model)

Engine-confirmed model of **what changed in the swoop race between KOTOR 1 and
KOTOR 2**, read from the K2 Ghidra project (Steam `swkotor2.exe`, decompiled
2026-09-14), the K1 project for the twins, and the three K2 track modules'
extracted area GFFs + disassembled scripts (`211TEL`, `371NAR`, `510OND`; scratch
copies were made with `unrim` / `gff2xml` / `ncsdis --kotor2`). When in doubt this
document and the decompile win over runtime theories.

Why it exists: the first K2 swoop round (patch-20260914-143533) ended with the
bike parked at speed 0 for twelve seconds while the player pressed Space three
times. The mod had not swallowed anything — the engine received all three
presses as input event `0xd9` — but on KOTOR 2 that event is the **jump**, and a
jump is refused while the bike is still in gear 0. The gear shift lives on a
different input. Everything below is what a reader needs to finish the K2 swoop
port and to build the vertical-obstacle cue.

## 1. The rebinding in one paragraph

KOTOR 2's CSWTrackFollower carries **thirteen** per-object script slots instead
of ten; the three new ones are `OnAccelerate` (index 10), `OnBrake` (index 11)
and `OnHitWorld` (index 12). All three shipped K2 tracks leave the **OnFire slot
empty** (K1 put the gear ladder there, `onfire.ncs`), put the gear ladder into
**OnAccelerate = `onaccelerate.ncs`** and bind **OnBrake = `onjump.ncs`**. The
client input handler routes the two slot runners from four event ids:

- events `0x16` and `0x19` → OnAccelerate runner `@0x00838e10` (turret: Fire)
- events `0x1a` and `0xd9` → OnBrake runner `@0x00838ec0` (turret: Fire)

`0xd9` = 200 + keymap row 17 = the `MGshoot` row = **Space** (and pad A on the
minigame page). So on KOTOR 2 **Space jumps**; on KOTOR 1 the same two events
`0x19` / `0xd9` both reach `CSWMiniPlayer::Fire` (K1 `HandleInputEvent
@0x00621210`, cases `0x19` and `0xd9`), whose gun bank runs `onfire.ncs`, which
is why Space shifts there. Event `0x16` is what the pad's B button produces on
the minigame keymap page (`k2-controller-support.md`, context row 2). The
keyboard/mouse sources of `0x16` / `0x19` are settled in section 6.

## 2. Slot indices (in-memory order, both games)

The follower's script array starts at `+0xa4`, 16-byte CResRefs. Witnessed live
in the K2 first-fire player dump (patch-20260914-143533, `211TEL`):

- `[0]  +0xa4  OnCreate       = oncreate`
- `[2]  +0xc4  OnHitFollower  = accelpad`
- `[4]  +0xe4  OnHeartbeat    = heartbeat`
- `[8]  +0x124 OnHitObstacle  = enemyhitfollower`
- `[10] +0x144 OnAccelerate   = onaccelerate`   (K2 only)
- `[11] +0x154 OnBrake        = onjump`         (K2 only)
- `[12] +0x164 OnHitWorld     = (empty)`        (K2 only)

The runners are two 173-byte twins: each calls the follower's `GetScriptName`
vtable slot (`vtable+0xc`) with the literal index (10 or 11), validates the
CResRef and runs it through the VM with the follower's MGO index as OBJECT_SELF
— the same shape as K1's `CSWMiniGameObject::OnHitFollower @0x0066c2d0`, which
passes index 2.

## 3. Gear ladder — `onaccelerate.ncs` (identical on all three K2 tracks)

Locals, in push order: two ints (upgrade levels), the min-speed ladder
35/60/100/150/210, the per-gear accelerations 20/8/6/4/2, then
`f0 = 1 + 0.2 * SWMG_GetSwoopUpgrade(0)` and `f1 = 1 + 0.5 * SWMG_GetSwoopUpgrade(1)`,
then `gear = GetGlobalNumber("MIN_RACE_GEAR")` and `speed = SWMG_GetPlayerSpeed()`.

- `gear < -1` → return (countdown; press ignored)
- `gear == -1` → play the `Penalty` sound object, no shift (false start)
- `gear == 0` → **unconditional launch**: min 35·f0, max 70, accel 20·f1,
  tunnel ±20 lateral / +10 vertical (`SWMG_SetPlayerTunnelPos/Neg`)
- `gear == 1` → shift iff `speed > 35`; sets min 60·f0, max 110, accel 8·f1
- `gear == 2` → shift iff `speed > 60`; sets min 100·f0, max 155, accel 6·f1·f1
- `gear == 3` → shift iff `speed > 100`; sets min 150·f0, max 215, accel 4·f1·f1
- `gear == 4` → shift iff `speed > 150`; sets min 210·f0, max 210·2·f0(?), accel 2·f1·f1
- gate failed → the else branch only re-asserts the current gear's
  acceleration (a no-op), exactly as K1's early press
- on success: `SetGlobalNumber("MIN_RACE_GEAR", gear+1)`, fade the previous
  `Engine0N` sound, play `ShiftN` + `Engine0N`, `gearN` / `gearNl` animations

**The gates are one notch lower than KOTOR 1's** (K1 `onfire.ncs` needs the
NEXT gear's minimum: 60/100/150/210). They compare against the raw constants,
not the upgrade-scaled minimums. `minigame_swoop_race.cpp` carries both ladders
(`kShiftGateSpeedK1` / `kShiftGateSpeedK2`).

## 4. `MIN_RACE_GEAR` is the gear (both games)

Every writer, disassembled:

- `oncreate.ncs` stamps **-5**
- `heartbeat.ncs` countdown writes **-3 / -2 / -1** with the `S3 / S2 / S1`
  sounds (elapsed > 0.1 / 3 / 4 s), **0** at `Go` together with the
  `MIN_TIME_HOUR/MIN/SEC/MIL` start stamp, **6** at the finish
- `onaccelerate.ncs` (K2) / `onfire.ncs` (K1) write **1..5** on each upshift
- K2 `enemyhitfollower.ncs` (the player's OnHitObstacle) writes **0** on a hit:
  speed × 0.35 clamped to [10, 50], min speed to that value, acceleration 0,
  `gear0` animation, engine fades to `Idle`, HP -1, invulnerability, then the
  bike is re-seated on the track (`SWMG_GetTrackPosition` /
  `SWMG_SetFollowerPosition`). **A K2 hit drops you to gear 0 and you must
  re-launch.** K1's `obstacle.ncs` only multiplies speed by 0.7.

Values 0..5 are therefore the gear; anything else means countdown / finished /
no information. `ReadGlobalNumber` returns -1 on a fault, which deliberately
lands in the same bucket. The mod now reads this global as the primary gear
source on both games (the max_speed-plateau heuristic stays as the fallback and
cannot see K2's downward resets).

## 5. The jump — physics and state

`onjump.ncs` (OnBrake slot) returns early if `GetLocalNumber(racedialog, 12)`
is set or the bike's tunnel offset Z is already > 2.0; otherwise it calls
`SWMG_SetJumpSpeed(30.0)`. The VM handler `@0x006f8c50` writes that float to
**`CSWMiniGame + 0x118`** (K2's struct tail; K1's CSWMiniGame ends at 0xf0).
A sibling handler `@0x006f8ca0` writes the same slot plus the physics-state
block at +0xc0 / +0xd8.

K2 `CSWMiniPlayer::Control` twin `@0x00837960` (K1 `@0x0066d640`), swoop
branch only:

```
if (mini_game->type == 1) {
    v = mini_game->+0x118;
    offset_delta.z += v * dt;          // integrated into offset.z (+0x1fc)
    mini_game->+0x118 = v - dt * 45.8; // gravity, units/s^2
}
KeepInTunnel(&offset);                 // +Z bound = 10 (script-set)
```

So a hop is `v0 = 30`, `g = 45.8`: **~1.31 s airborne, apex ~9.8 units**, the
tunnel ceiling of 10 sitting just above it. The K2 player layout is K1 + 0x30
throughout: offset `+0x1f4/1f8/1fc`, skip-accel byte `+0x204`, min `+0x208`,
max `+0x20c`, acceleration `+0x210`, tunnel pos `+0x214..21c`, tunnel neg
`+0x220..228`, infinite flags `+0x22c..234`, origin `+0x238..240`. K1's
`KeepInTunnel` clamps offset.z to 0, which is why the jump watch never arms
there.

Obstacle hits are not Z-aware in the script layer: K1
`CSWTrackFollower::DoObstacleHitCheck @0x0066e6b0` hands the follower model's
world position and radius to the obstacle's own model test (`gob vtable+0x88`),
so a bike whose model has been lifted by the offset simply clears anything
lower than its sphere — there is no separate "jumped over" flag to read. The
mod logs jump start / landing (speed, gear, air time, peak Z, camera position)
so the vertical-obstacle cue can be tuned from real runs.

## 6. Which physical inputs produce events 0x16 / 0x19 / 0x1a

Event ids below 200 are registered by hand in
`CClientExoAppInternal::SetEventDescriptions` (K1 `@0x005eeb10`, K2 twin
`@0x00785df0`); ids ≥ 200 are `200 + keymap.2da row` (MGshoot row 17 → 0xd9,
LeftShift row 21 → 221, both seen live). The registrations that matter, both
games identical:

- `0x17` ← InputIndex 3 `MOUSE_XAXIS`, `0x18` ← 4 `MOUSE_YAXIS` (class 1 — the
  `NUMPADPLUS(23)=1440 / LEFTSHIFT(24)=900` pairs in the race logs are these,
  mis-named by the raw-index table)
- **`0x19` ← InputIndex 0 `MOUSE_BUTTON0` — the left mouse button**
- **`0x1a` ← InputIndex 1 `MOUSE_BUTTON1` — the right mouse button** (K1 does
  not handle it in the minigame; K2 routes it to the jump)
- `0x16` is not registered here at all — it is what K2's pad table
  (`GetEvents @0x0072c7e0`, context row 2) emits for **pad B** on the minigame
  page (pad A emits 0xd9 there)

So the native KOTOR 2 PC scheme is **left click = shift, Space or right click =
jump**, and no keyboard key shifts. (The `InputIndex → DIK` table the K2
`CExoInputInternal` ctor `@0x0072a130` fills is the other direction —
`KEYBOARD_P = 0x19` and friends — and is unrelated to these event ids; a
scalar search for 0x19 lands there first, which cost one round.)

The mod's resolution (input_pipeline.cpp + minigame_swoop_race.cpp): on a
K2 race, event `0xd9` (Space / pad A) is consumed at the client input hook and
`TryShift` runs the OnAccelerate runner itself; event `0x16` (pad B) is consumed
and runs the OnBrake runner; the `SwoopJump` hotkey (bare W, polled in Tick
because W is an axis poll, not an event, inside the race) runs the OnBrake
runner from the keyboard. Left / right mouse keep their native meaning.

## 7. What the tracks are made of (hazard atlas, offline + round 2)

Settled 2026-09-14 from the second K2 round (patch-20260914-163810) plus an
offline parse of every obstacle model: `keybif_extract.py` (KEY/BIF by resref)
pulled each track's `.lyt` and `<track>_mg*.mdl`; `mdl_nodes.py` walks the
binary MDL node tree (names, positions, per-mesh bounding boxes);
`hazard_atlas.py` joins them with the `.lyt` obstacle origins. Scripts kept in
`tools/re-scripts/`; the results are what matters.

**Every obstacle model is a dummy root plus ONE trimesh named `hitbump`**, and
K1's `CSWTrackFollower::DoObstacleHitCheck` asks the gob for exactly that part
(the string is literal in the decompile) — the hitbump box IS the collision.
The mesh can sit far from the object's origin (MGC01: origin y=928, bar at
y=880), so origin-based cues are wrong for these.

Three shapes:

- **Lane-edge posts** (`211TEL_MGC010..099`): 90 of Citadel's 105 objects, one
  pair every ~100 units, origins x=-30.4 / +28.2, hitbump at |x| 18.3..24.7,
  16 units tall. Only touchable when hugging a wall (the tunnel clamps |x| to
  20, bike radius 2). These were the "enormous amount of obstacles" — the
  position-based loop sounded them all, on both sides, all race.
- **Low bars** (`211TEL_MGC01..09`, `371NAR_MGC15..30`, `510OND_MGC01..13`,
  Nar's `mgfl/mgfr` fan arms): 4–7 units tall (jump apex ~9.8), spanning
  half or all of the lane. Citadel: y 880 (x −13..13), 1090 (−20..0), 1692
  (−1..19), 1880 (−1..19), 2702 (−19..19), 2798 (−1..19), 3991 (−20..19), 4602
  (−1..19), 5001..5051 (−13..13). Round 2's third hit was MGC01 at y=880 with
  the bike at x=2, no jump.
- **Blocks**: taller than a jump — Nar Shaddaa's centre posts (x ±4, 13
  tall) and side pairs, its `mgfd/mgfu` fan bodies, Onderon's staggered wall
  pairs (gap in the middle) and its climbing track (box Z rises 111→384 with
  the road), the `mge` shuttles/hangars near each finish (17 tall).

**Mines are enemies, not obstacles.** Citadel's `Enemies` list is 23 pads
(`mgf_acpad`, Trigger=1) + 24 mines (`mgf_mine01`, Trigger=0, OnHitFollower
`mine.ncs`: VFX, the mine is destroyed, the bike drops to the gear its
0.35×speed allows). They ride one-node tracks (`211TEL_MGM01..24` in the
`.lyt`), i.e. they do not move. Pads sit at z≈40.0, mines at z 44–47, but the
robust discriminator is the model name. Round 2's first two hits were mines
the pad co-pilot had locked onto (`mag=1 slot=126/127`, `fwdGap 3.9 / 3.3` at
the gear drop).

**Runtime geometry** (both games, `minigame_aim.h`): Gob vtable slot 59
`GetBoundingBox(min,max)` copies the model-local box; slot 67
`GetPart("hitbump")` + Part slot 29 `GetMinimumBoundingBox(min,max,scale)`
folds that part's vertices into a caller-initialised local box (K1
`@0x00440960`, K2 `@0x0045C810`; RTTI slot map). World box = gob position +
local box for these unrotated objects. The mod resolves each obstacle once per
race and logs `box=hitbump|gob x[..] y[..] z[..] class=post|bar|block` in the
first-fire inventory — compare that line against the atlas above when a track
misbehaves.

**Round-2 physics facts:** a collision runs `enemyhitfollower.ncs` → speed ×
0.35 clamped to [10, 50], acceleration 0 (the bike does NOT pick up speed
again until the next shift), lateral acceleration 300, and a re-seat
`SetFollowerPosition(track position + normalised(bike − obstacle) × 1.1 ×
radii)`. Against a post that shove pins the bike at x=20; every relaunch
(speed ~6) hit the post again within 8 units, gear 1 → 0, for twenty seconds.
The magnet made it worse by pulling a bike the player had steered to x=−10
back to the pad lane (x=0) into the bar — hence the gear-0 magnet gate. The
"finish" the mod spoke at 23 s was the coast-to-a-stop fallback misreading
that first stop.

## 8. What the mod does with this (2026-09-14, two builds, awaiting round 3)

Round-2 additions (`minigame_swoop_audio.cpp` / `minigame_swoop_race.cpp`):

- Enemies split by model name: mines never become steering targets and are
  sounded as blocks through the warn loop; the inventory logs `mine`/`accelpad`
  per slot and an `enemy pool: accelpads=N mines=M` summary.
- K2 obstacles classified from the hitbump box: posts skipped, bars get the
  `SwoopJumpNow` cue (`mgs_warnbust`) at 1.35 s before the bar when the bike's
  lane is inside the bar's span (the press stays the player's: a co-pilot
  auto-jump was built and removed the same day at the user's request — cues
  inform, they do not play);
  blocks keep the warn loop, panned to the box centre, timed from the leading
  edge. `hazards: posts=… bars=… mines=…` trace per tick.
- Magnet disabled while `MIN_RACE_GEAR` is 0 (post-hit relaunch).
- Finish: `MIN_RACE_GEAR == 6` (the heartbeat's own finish write) is the
  primary trigger on both games; the coast-to-a-stop fallback is K1-only.
- Shift-ready cue resolves per game (`GetNavCueResrefForGame`): K2 plays
  `c_drdasasin_atk1` because `c_drdastro_atk1` does not exist in K2's archives
  (the whole first K2 round had a silent cue, not a speed problem).
- Stuck hint: no forward progress for 6 s in gear 0 after the launch → one
  spoken escape instruction (steer aside, then Space and W together).

Round-1 additions:

- `input_pipeline.cpp`: Space / pad A (`0xd9`) → `TryShift`, pad B (`0x16`) →
  `TryJump`, both consumed so the engine's own jump case never runs; no-ops
  outside an active K2 race.
- `minigame_swoop_race.cpp`: `TryShift` / `TryJump` call the engine's slot
  runners on the player bike; `SwoopJump` hotkey (W) polled in Tick; gear from
  `MIN_RACE_GEAR` (both games, plateau fallback); per-game shift-ready gate
  ladder; K2 jump watch (log-only); the 1 Hz snapshot shows the gear global and
  the jump velocity.
- `hotkeys` / `menus_keybinds` / `strings`: `SwoopJump` action (rebindable,
  listed under Minigames), `SwoopRaceControlsK2` cheat sheet, both in all
  seven languages.
- GOG K2: the two runner addresses are `Kotor2Only()` literals, so the
  relocation table must be regenerated (`kdev sigscan --k2` + the two
  re-scripts) whenever they change.
