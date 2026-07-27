# swoop_spatial_audio.cpp (1426 lines)

Implements the two per-tick spatial sweeps for the swoop race: obstacle
proximity loops (one LoopSource per in-range rock, from the 22-slot
Obstacles pool) and an accelpad "steering guide" (one panned tone toward
the next gate, plus a lateral steering-magnet that writes the bike's
tunnel-offset.x directly, from the 30-slot Enemies pool). Both pools are
read off one global `CSWMiniGameObjectArray` (255 handle slots) resolved
via AppManager→ClientApp→ClientAppInternal, classified per-slot by calling
the object's own `AsObstacle`/`AsEnemy` vtable-downcast slots. Cue pan is
decoupled from loudness: lateral+vertical position stays true but forward
depth is pinned to a fixed reference so pan encodes only lane error (real
3D distance drives volume via a manual linear ramp inside a flat
attenuation band). Also implements a predictive wall-overshoot cue and a
PD-damped lateral steering magnet (shared `minigame_aim` port from the
turret assist). Talks to: audio_bus (PlayCue3D), audio_cues (NavCue
resrefs), audio_loop (LoopSource), engine_player (camera/player position),
minigame_aim (MagnetGain/MagnetStep/WriteOffsetVector), log.

## Declarations (in source order)

- L42-76 — CSWMGObstacle/AurObject offsets + MGO array layout (index@+0x0,
  objects[255]@+0x4) + vtable downcast slot offsets (AsObstacle=0x20, AsEnemy=0x1c)
  note: first pass misread +0x44 as a flat array and crashed; corrected via decompiling ExecuteCommandSWMG_GetFollowerPosition
- L123 — `kAccelpadLoopResref` — trimmed acc_boost.wav (Override), tail-cut to avoid pitch-drop read
- L137 — `kAccelpadCueRangeM = 300.0f`
- L162-171 — CSWTrackFollower model-position-read offsets + sphere_radius(+0x84)/speed(+0x98)
- L227-230 — obstacle cue range/forward-margin/loop-resref constants
- L295-320 — decoupled pan/volume tuning constants (kSwoopPanForwardRefM=6.0f,
  flat-band min/max, near/far volume bytes, guide-specific pan depth/silence/floor)
- L361-380 — retired co-pilot steering constants (lead ticks, EMA smoothing, deadzone,
  clamp guards) — kept as the smoothing/velocity state the magnet now consumes
- L431 — `kCoastTicks = 2.5f` — velocity lead for the co-pilot coast/aligned model
  note: engine-grounded (CSWMiniPlayer decompile): swoop steering is a bank-level clamp, not momentum — real coast is ~1 unit, not the ~8x-inflated old estimate
- L441-456 — predictive wall-cue constants + fallback combined hit-radius literal
- L482 — `kSwoopAssistGapU = 16.0f` — the SINGLE difficulty knob: magnet engage radius == cue release band
- L508-519 — retired discrete steer-tick resrefs/volume (superseded by continuous guide)
- L557-590 — lateral steering-magnet tuning: engage radius, near/far gain, per-tick
  step cap, PD brake-ticks, hold-through-crossing window, min race speed gate
- L618 — `int SwoopVolumeByte(dist, range)` — linear near/far volume ramp
- L633 — `struct SpatialAudioState` — per-slot obstacle LoopSource array, guide loop,
  steering EMA/velocity state, crossing-scoring previous-gate cache, diag guards
- L743 — `void* ResolveMgoArray()` — AppManager+0x4→ClientApp+0x4→ClientAppInternal+0x0
- L777 — `const char* ReadAurObjectName(aurObject)` — CAurObject vtable[0xc] GetName
- L800 — `bool ReadTrackFollowerPosition(follower, out)` — mirrors
  CSWTrackFollower::GetPosition (models.data[0]→vtable[0x64])
- L830 — `void* CallAsCast(obj, vtableSlotOffset)` — generic AsXxx downcast thunk
- L849 — `void TickObstacleCues(miniGame)` — per-slot obstacle scan, forward-only
  filter, range cull, LoopSource start/update/stop, first-fire inventory log
- L1049 — `void TickAccelpadCues(miniGame)` — finds nearest ahead gate, computes
  co-pilot proj/coast diagnostics, drives the lateral magnet write, predictive
  wall-overshoot cue, and the panned steering-guide LoopSource
  note: crossing-event log interpolates exact lateral miss at fwdGap=0 for ground-truth hit scoring against the live-read combined sphere radius
- L1395 — `void TickSpatialAudio(miniGame)` — calls both Tick* functions
- L1400 — `void ResetSpatialAudio()` — clears all per-race state + stops every loop
