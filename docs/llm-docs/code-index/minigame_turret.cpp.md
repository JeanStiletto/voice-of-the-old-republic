# turret_game.cpp (2073 lines)

Implements the turret/gunner minigame accessibility layer: entry/exit
narration, a single continuous "peg" targeting tone on the Q/E-selected
fighter (pulses off-target with a rising tick rate, goes solid inside the
range-scaled hitbox cone, pitch-glides for elevation error), a locked-
fighter "hum" approach loop, per-kill/destroyed announcements (every
fighter, not just the locked one), and aim-assist that WRITES the aim
field directly — proximity-ramped "magnetism" by default, full lock-on
under the TurretAutoAim toggle. Aim targets the INTERCEPT point via a
listener-relative closed-form lead solve (bolt speed 300u/s) plus a
curve-aware centripetal-turn correction, both self-validated by a
deferred LeadCheck probe ring logged for offline tuning. Full engine
model: docs/llm-docs/turret-minigame-model.md. Talks to: audio_bus
(PlayCue3D), audio_loop (LoopSource), hotkeys (Q/E), menus_modsettings
(TurretAutoAim), minigame_aim (shared magnet math + offset read/write),
engine_area/engine_player, prism, strings.

## Declarations (in source order)

- L65-119 — CSWMiniGame/CSWTrackFollower shared offsets (type=2 turret,
  player+0x24, enemy_count+0x30) + combat scalars (sphere_radius+0x84,
  hp+0x8c, max_hp+0x90, speed+0x98, invuln+0x9c)
  note: sphere_radius is what OUR cue subtends against but is NOT the engine's real hit primitive (mesh-geometry hit test); confirmed via the OnTurretBulletHit diagnostic, ~20m hitbox not runtime-patchable
- L142-164 — OnHitBullet impact-point diagnostic offsets + census range (600m) + hum loop tuning (1/30 distance compression, acc_turret_loop)
- L166-236 — targeting "peg" cue design constants (kPegMinDist/EdgeDist/MaxDist,
  kPegRampDeg=25, kFallbackOnTargetDeg=6, behind-gate hysteresis 95/85 deg)
  note: on-target is DISTANCE-SCALED to the hitbox subtend angle, not a fixed cone
- L238-296 — rising-tick-rate crosshair (fast/slow ms, 90 deg ramp), behind-arc
  pinned pan, elevation-pitch channel, "fire now" one-shot on cone entry
- L298-338 — killable-range window cue (130m) + predictive intercept lead
  constants (kBoltSpeed=300, kMaxLeadSeconds=3, range-gated by drift/radius)
- L340-355 — curve-aware centripetal-turn lead correction constants
- L357-413 — aim-assist magnetism tuning (engage 60 deg, near/far gain, per-tick
  cap) + full-autoaim makeability gate (flight-time <=1.6s) + switch-on-hit
  damage-spreading (avoids re-hitting an invuln-window fighter)
- L427-578 — `struct State` — latch/vtable, peg_cue + hum_cue LoopSources,
  selected_slot + stable per-slot numbering, per-slot kill/HP tracking,
  velocity/acceleration EMA for lead, passive offset-sign calibration,
  LeadProbe ring (16 slots), session QC/accuracy counters
- L620-625 — `void* ReadMiniGameViaArea()`
- L628 — `bool IsTurretMiniGame(mg)` — type==2 check
- L646 — `void* ResolveMgoArray()` — same AppManager chain as swoop_spatial_audio
- L674 — `bool ReadFollowerPosition(follower, out)` — CSWTrackFollower model-position mirror
- L742 — `Vector QuatRotate(q, v)`
- L756 — `void* ResolveGunModel()` — player→gun_banks[0]→guns[0] CAurObject*
- L778 — `bool ReadAimLine(outDir, outOrigin)` — THE CROSSHAIR: bullethook0 node transform
  note: local +Y confirmed as true fire-line axis via full-autoaim live test (93% on-target, 6 kills/13s)
- L806/815 — `bool ReadOffset(az, el)` / `void WriteOffset(az, el)` — CSWMiniPlayer.offset read/write
- L837/843 — `PushLeadProbe`/`DrainLeadProbes` — LeadCheck self-measuring diagnostic
- L867 — `int NumberForSlot(slot)` — stable "Fighter N" numbering, never reused
- L879 — `Vector PlayRangeCueAt(targetPos, dist, listener)` — compressed always-audible position
- L900 — `void AnnounceSelectedTarget(idx, ..., speak=true)` — locks + speaks/locator-pings;
  speak=false used for silent re-pick after a kill
- L958 — `void HandleTargetCycle(...)` — Q/E cycle (wrap, nearest-first census order)
- L991 — `void DriveSelectedPeg(...)` — the core per-tick targeting function: kill vs
  out-of-view distinction, range-window enter/leave cue, velocity/accel EMA,
  intercept solve + curve correction, on-target/behind gates, peg Start/Update/Stop,
  aim-assist steer (magnetism or full-autoaim), makeability retarget + hit-spread
- L1646 — `void TickFighterCues()` — Pass-1 MGO census + destroyed-fighter
  announce (every slot, via SpeakUrgent) + Q/E dispatch + hum loop
- L1842/1854 — `void AnnounceEntry()` / `void AnnounceExit()` — combined SpeakUrgent utterances
- L1864 — `void HandleEnter(mg)` — full per-round state reset
- L1914 — `void HandleExit()` — session QC + accuracy summary log, stop loops
- L1965 — `extern "C" void OnTurretBulletHit(hitEvent)` — hook @0x0066c190;
  measures real impact-vs-centre distance, counts fighter_hits
- L2008 — `extern "C" void OnPlayerFire(player)` — hook @0x0066dc50; counts shots_fired
- L2015 — `void Tick()` — top-level state machine (mirrors swoop_race.cpp's
  latch/debounce), enemy_count-drop kill surfacing, delegates to TickFighterCues
