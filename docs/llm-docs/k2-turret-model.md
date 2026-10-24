# KOTOR 2 turret minigames — data-side model (2026-10-24)

Companion to `turret-minigame-model.md` (the K1 engine model). Read that first
for the shared mechanics: aim = `CSWMiniPlayer.offset` (x = elevation,
z = azimuth, degrees; +0x30 on K2), bolts fly straight from the gun model's
`bullethook0`, hits are a swept test against the target's model geometry.

Everything here was read from the game files, not from a live session:
the `.are` MiniGame structs, the module scripts (xoreos `ncsdis`), the `.git`
object lists and the track models' position keyframes. Tools:
`tools/re-scripts/mgscan.py` (find + dump every MiniGame struct in a modules
folder), `mdlanim.py` (MDL animation keyframes), `trackstats.py` (range /
speed / direction per fighter track). Track positions treat animation keys as
offsets from the node's rest position; that convention reproduces K1's
measured ~74 u/s fighter speed exactly, which is the check that it is right.

Status: Known = read from files. Suspected = inferred, not yet confirmed in
game or in a decompile. Open = not looked at.

## Summary

- KOTOR 1 has ONE turret area (`M12ab`), reused for every Ebon Hawk space
  encounter. Losing it is a game over (`EndGame`) outside the HK-47
  simulation.
- KOTOR 2 has THREE, and none of them can end the game:
  - **107PER, Peragus hangar** — a different game. No space fighters: the
    targets are Sith troopers running across the hangar floor, plus explosive
    barrels. Limited aim arc. The player cannot be hurt. The score picks the
    next area.
  - **421DXN, Dxun blockade** — K1's design almost unchanged. The six fighter
    paths are K1's own paths, number for number.
  - **505OND, Onderon** — K1's design with new paths: eight fighters, two to
    three times farther away, up to four times faster.
- Both space turrets (Dxun, Onderon) end on a timer as well as on "all
  destroyed", and losing just ends the sequence. They also print an engine
  kill counter after every kill.

## Per-area data

### K1 reference: M12ab (Ebon Hawk turret)
- Turn rate (`MovementPerSec`): 100 degrees per second. View angle 65.
- Aim range: full 360 azimuth (`TunnelInfinite` on z), elevation 2 to 45.
- Player: 3000 HP; the encounter is lost below 2000, so 1000 effective HP.
- Player gun: two banks, 30 damage per bolt, one bolt per 0.3 s per bank,
  bolt speed 300, lifespan 3 s, so about 900 units of reach.
- Fighters: 6, each 100 HP (4 hits), `Sphere_Radius` 20, model
  `mgf_sithfighter`. Paths loop forever (`Num_Loops` -1).
- Fighter gun: 10 damage, one bolt per 0.4 s, bolt speed 200, only fires
  within 200 units (`Sensing_Radius`), 70 degree spread cone.
- Fighter paths (loop length / distance min-median-max / speed):
  - mgt02: 43 s / 44-296-526 / 73 u/s
  - mgt03: 49 s / 19-288-529 / 73 u/s
  - mgt04: 51 s / 17-276-555 / 91 u/s
  - mgt05: 62 s / 25-261-561 / 74 u/s
  - mgt06: 84 s / 25-238-396 / 64 u/s
  - mgt07: 55 s / 23-234-467 / 88 u/s
  - Fighters spend about a third of the time within 200 units, half within 300.
- Ends when every fighter is destroyed (`ebo_num_fighters` reaches 0).

### 421DXN: Dxun blockade turret (Known)
- Turn rate 150 degrees per second (K1: 100). View angle 65.
- Aim range: identical tunnel to K1 (full azimuth, elevation 2 to 45).
- Player: 3000 HP, lost below 2000 (same script shape as K1's
  `k_pebo_hawkhit`).
- Player gun: two banks, 30 damage, one bolt per 0.3 s, **bolt speed 600**
  (K1: 300), lifespan 3 s, so about 1800 units of reach. Needed lead halves.
- Fighters: 6, 100 HP, radius 20, own model (`421dxn_14`).
- **Fighter paths `421dxn_4` to `421dxn_9` are K1's `m12ab_mgt02` to `mgt07`**:
  identical rest positions, loop lengths, distances and speeds.
- Fighter gun: **25 damage**, one bolt per 1.0 s, speed 200, fires only within
  **100 units** (K1: 200). So 40 hits lose the encounter (K1: 100).
- End: `421DXN_Turret_Done` is set when dead >= total, or by
  `k_421_modheart` once a timer passes 114 (same `MIN_TIME_*` globals the
  swoop race clock uses; most likely seconds — Suspected, confirm in game).
  Then `StartNewModule("003EBO")`.
- Losing (HP below 2000): fade out, bark 127829 ("We're hit! Critical damage!
  The Hawk won't make it!"), then the same `003EBO` jump. No game over.
- After every kill the death scripts bark 127828, "Enemies destroyed:
  <CUSTOM10> of <CUSTOM11>", an engine-side kill counter.

### 505OND: Onderon turret (Known)
- Turn rate 150 degrees per second. View angle 65.
- Aim range: full azimuth, elevation 2 to 45.
- Player: 3000 HP, lost below 2000.
- Player gun: two banks, 30 damage, one bolt per 0.25 s, **bolt speed 600,
  lifespan 6 s**, so about 3600 units of reach.
- Fighters: **8**, 100 HP, **`Sphere_Radius` 30** (K1: 20), model
  `505ond_fghtr`.
- Fighter gun: 20 damage, one bolt per 1.0 s, speed 200, fires within 100
  units. 50 hits lose the encounter.
- Fighter paths are new and much wider (loop / distance min-median-max /
  speed median-max):
  - mgt02: 49 s / 71-579-1768 / 114-182 u/s
  - mgt03: 49 s / 56-595-1742 / 114-189
  - mgt04: 20 s / 30-366-1214 / 172-230
  - mgt05: 20 s / 39-381-1229 / 172-240
  - mgt06: 50 s / 79-793-1451 / 115-254 (covers about 240 degrees of azimuth)
  - mgt07: 25 s / 60-871-1512 / 177-288 (about 150 degrees)
  - mgt08: 25 s / 44-924-1549 / 194-287 (about 210 degrees; climbs to +86)
  - mgt09: 27 s / 60-608-1418 / 139-333 (about 180 degrees)
  - mgt10: 50 s / 33-630-1767 / 102-210 (unused by the .are; climbs to +77)
  - Fighters are within 200 units only 7 to 24 percent of the time (K1: about
    a third). Several paths keep to one side of the turret rather than
    circling it.
- End: timer (114, same as Dxun) or all destroyed -> `504OND`. Losing sets
  `505OND_Turret_Done` and goes the same way. Same kill-counter bark.
- The player track model carries a `hitbullet` node about 150 units above
  the turret (`505ond_mgt01`). Purpose Open.

### 107PER: Peragus hangar turret (Known unless marked)
- The minigame struct has **no enemies and no obstacles**. Turn rate 75
  degrees per second, view angle 45, far clip 400.
- Aim range is **bounded**: elevation -15 to +15, azimuth -135 to +70 (about
  205 degrees). No `TunnelInfinite`.
- Player: 1 HP, but nothing in the area shoots at the player.
- Gun: one bank, 300 damage, one bolt per 0.06 s, **bolt speed 0.2**,
  lifespan 4 s. A bolt that barely moves cannot reach anything through the
  normal swept bolt test, so hits must come from a different path —
  Open: decompile the K2 bullet hit check (the loader that reads
  `OnHitWorld` is the entry point to find).
- The targets are ordinary area objects in `107per.git`:
  - Sith troopers `g_sithtroop008` (tag `G_SITHTROOP07`, 50 HP, faction 1).
    `a_hangar_timer` spawns one at `WP_TARGET_ENTER` (-23.8, 8.5) every 2 s
    until 25 have spawned (`107PER_MG_LEFT` counts down from 25). Four
    pre-placed troopers use `g_sithtroop009` (tag `ExtraSith`).
  - Each trooper runs one of four waypoint routes `WP_01_*` to `WP_04_*`
    across the hangar (x from -22 to +27, y from 7 to 33) towards the Ebon
    Hawk. Walk rate 7 (run).
  - 12 explosive barrels (`plstccrt001`, tag `ExpBarrel`, 15 HP) along those
    routes. Their `OnUserDefined` runs `a_barrelexplode`: area damage,
    stun, poison and horror on enemies in a radius.
  - One `TargetDummy` placeable (plot, cannot die) at (2.95, 14.08): the
    troopers' heartbeat `ActionAttack`s it, so they shoot at the ship.
  - Trigger `SithExit` (tag) at (6.5, 7.7), by the ship: a trooper that
    reaches it counts as boarded (`107PER_MG_EBON` +1) and is destroyed.
- Damage path (Suspected): the troopers' and barrels' `OnUserDefined`
  scripts apply `EffectDamage(GetUserDefinedEventNumber())` to themselves,
  so the engine apparently signals a user-defined event whose number is the
  bolt damage (300) when a bolt hits an area object. One hit kills a trooper.
- End: when killed + boarded reaches 25, Atton starts `turend.dlg`.
  - All 25 killed (`c_check_alldead`): clean escape, movie, `003EBO`.
  - Otherwise: "[The Sith are aboard the ship. Prepare to repel them.]",
    then `005EBO`, a fight aboard the Ebon Hawk.
- Turret position relative to the hangar: Open. The minigame track
  (`107pera`) sits at its model origin; whether that is the area origin, and
  so whether the troopers are 10 to 40 units away, needs a live read.

## Differences that matter for the accessibility layer

- Nothing in K2 is a hard gate. K1's turret is (game over on loss), which is
  why K1's aim-assist was built around winning. In K2 a missed turret costs a
  boarding fight (Peragus) or nothing (Dxun, Onderon).
- Dxun can reuse K1's tuning almost as is: same paths, same distances. Bolts
  are twice as fast, so any lead term halves. Fighters only shoot inside 100
  units.
- Onderon breaks K1's range assumptions: range cues and the auto-pick are
  tuned for targets within about 560 units; Onderon's sit at a median of
  600 to 900 and reach 1770, moving up to 330 u/s. Bolt reach (3600) is not
  the limit; following the fighter is.
- Peragus needs its own mode: ground targets from the creature list (not
  minigame followers), a bounded aim arc, barrels as area-effect targets,
  and the "boarded" count as the stake. K1's fighter census, Q/E cycle and
  range cue do not apply as they are.
- The engine's own kill counter bark (127828) on Dxun and Onderon is a
  ready-made progress announcement if barks are spoken.
- Turn rates differ per area (75 / 100 / 150 degrees per second): any
  "hold the key for N ms" timing must read `MovementPerSec`, not assume K1's.

## Port status (2026-10-24): built, NOT tested in game

`minigame_turret` runs on both games (the `if (k1)` gate in `core_tick` is
gone). What changed, with the witnesses at each declaration in the code:

- **Engine offsets.** Shared minigame fields reuse the swoop port's witnesses.
  Turret-only fields were witnessed fresh, each in both games' own code:
  gun banks (follower +0x74/+0x78), guns (bank +0x34/+0x38), bolt speed and
  lifespan (bank +0x44/+0x4c, via each game's setter), aim limits (tunnel
  pos/neg/infinite K1 +0x1e4/+0x1f0/+0x1fc, K2 +0x214/+0x220/+0x22c; origin
  K1 +0x208, K2 +0x238), the crosshair's Gob::GetPartPosition (slot 38 both),
  the invulnerability timer (+0x9c, below the grown script array).
  New engine-wide: `kObjectPlotOffset` (K1 +0xf8, K2 +0xfc) from each game's
  GetPlotFlag script command (K2's found as the one store into command-table
  slot 455), with `IsPlotObject` / `ReadObjectCurrentHitPoints` in
  `engine_reads`.
- **Arena.** Bolt speed / lifespan and aim limits are read off the player at
  entry (`ReadArena`, retried until both halves read) and logged as
  `Turret: arena: ...`. K1's constants are only the fallback.
- **Range model.** The four K1 distance constants became formulas whose
  constants ARE those K1 values (comment block "Range model" in the code):
  make horizon `1.6 s * sqrt((r/20)/(v/74))`; killable range
  `min(6.5 r, boltSpeed * t_make)`; census `min(boltSpeed * life,
  1.25 * boltSpeed * 1.6 s)`; hum compression `20 m / census`. Starting
  values: K1 130 / 480 / 600 (unchanged), Dxun 130 / 960 / 1200,
  Onderon 195 / 720-960 / 1200.
- **Aim limits.** Steering clamps to bounded axes and, on a bounded azimuth,
  takes the long way round when the short way would leave the arc (107PER's
  -135..+70). Free azimuth wraps as before.
- **Hitscan (107PER).** Bolt reach below 10 units switches off the lead, the
  flight-time gate and the range limits, and measures the aim from the barrel
  instead of the camera.
- **Area targets (107PER).** A turret whose minigame declares no enemies
  censuses the area instead: hostile creatures and non-plot placeables. Each
  is announced by the engine's own name with a per-name number ("Sith
  trooper 3, 18 meters"); "Target destroyed: <name> <n>" on a kill or a
  vanished placeable, "Escaped: <name> <n>" when a creature vanishes alive
  (reached the ship). Nominal target radius 1.0 (starting value).
- **Not ported:** the OnTurretBulletHit / OnPlayerFire telemetry hooks (no
  K2 twin found by vtable; their callers are a hit check, a script-command
  default handler and a ProcessInput case). On K2 the session accuracy line
  says n/a; per-target hits still log as `TurretHit`.

### First test round (one save per area)

1. **Dxun (421DXN)** — entry speaks; log shows `arena: bolt speed=600.00
   life=3.00`, `azimuth free`, `elevation bounded 2.0..45.0`. Q/E locks
   "Fighter N"; the crosshair tone goes solid on target; kills announce.
   Check the `TurretQC` session line against K1's numbers.
2. **Onderon (505OND)** — same, plus: fighters out to ~1200 units enter the
   Q/E list; the range cue fires at ~195. `LeadCheck` lines say whether the
   lead keeps up with the faster paths.
3. **Peragus (107PER)** — log shows `area-target mode`, `HITSCAN`, and
   `azimuth bounded -135.0..70.0, elevation bounded -15.0..15.0` (Suspected;
   origin not yet seen live). Each trooper and barrel logs an `area target:`
   line with its name; Q/E names them; kills / escapes are spoken; the aim
   assist never pins at an arc edge. Open: whether the aim ray and the
   troopers share a coordinate frame (watch `TurretAim` errAngle on a kill).
4. **KOTOR 1 regression (M12ab)** — `arena: bolt speed=300.00 life=3.00`,
   range cue at 130, census 600, full autoaim clears a wave as before.
