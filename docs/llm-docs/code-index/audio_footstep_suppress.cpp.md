# audio_footstep_suppress.cpp (507 lines)

Stuck-detection + footstep suppression + directional probe. Tick() samples player position each frame into a 32-slot ring buffer and judges NET displacement over a rolling ~500ms window (not per-frame instantaneous speed — wall-grinding bounces between dead-stop and 1-14 m/s per frame) with hysteresis (enter stuck <0.8 m/s, exit >1.2 m/s) to set `g_was_stuck`. `OnPlayFootstep` (hook handler) suppresses the leader's footstep audio when stuck and not in combat, and also suppresses non-leader footsteps while the leader is stuck (nearby NPC steps would mask the silence cue). Two independent stuck-probes run on top: `TickStuckAnnounce` (walking-but-no-progress over 5s) and `TickCirclingAnnounce` (high path-length/net-displacement ratio over 5s, catches rotation-in-place cases the displacement check misses). Both funnel into `RunStuckProbe`, an 8-cardinal walkmesh + nearby-body raycast that speaks free directions via `prism::SpeakUrgent`. Talks to combat (IsCombatActive), engine_area, spatial_change_detector (cached walls), engine_player, strings.

## Declarations (in source order)

- L37-40 — stuck hysteresis constants (`kWindowMs`=500, `kMinHistoryMs`=300, `kStuckEnterSpeed`=0.8, `kStuckExitSpeed`=1.2)
  note: replaced per-frame 0.3 m/s sample 2026-07-15 — instantaneous threshold flapped ~60% of footsteps during wall-grinding
- L49-54 — position ring buffer (`g_samples[32]`, head/count/last_tick_ms, `g_was_stuck`)
- L57-134 — stuck-direction probe gating constants + state (`kFootstepFreshnessMs`=1200, `kStuckWindowMs`=5000, `kAnnounceDisplacementMeters`=0.5, `kProbeDistanceMeters`=1.5, `kBodyClearanceMeters`=0.5, `kMaxNearbyBodies`=32)
  note: freshness window raised 200ms→1200ms — original was below normal footstep cadence, so "walking" flickered false and the no-progress window never completed despite 20s wall-grinding sessions
- L110-120 — circling-detector constants/state (`kCirclingWindowMs`=5000, `kCirclingMinPathMeters`=5.0, `kCirclingMaxNetMeters`=2.5, `kCirclingPathRatio`=2.5)
- L125 — `kProbeDirs[8]` — 8 cardinal unit vectors + string IDs (N/NE/E/SE/S/SW/W/NW), +X=East +Y=North
- L136 — `void RunStuckProbe(const Vector& pos)` — walkmesh raycast (SegmentCrossesWalkmesh) + nearby creature/placeable body clearance check per direction, builds "free: N, E" or "all blocked" message, speaks via prism::SpeakUrgent
- L258 — `void TickCirclingAnnounce(const Vector&, uint64_t now_ms, bool walking)` — path-length vs net-displacement ratio gate; re-anchors on real progress
- L316 — `void TickStuckAnnounce(const Vector&, uint64_t now_ms)` — checkpoint + displacement-since-checkpoint gate, latches announce once per stuck episode
- L356 — `bool WasStuckLastTick()` — public read of g_was_stuck
- L358 — `void NoteLeaderFootstep()` — stamps g_last_leader_footstep_ms (walk-anim freshness signal)
- L362 — `void Tick()` — position sampling, hysteresis state machine, dispatches both stuck detectors; edge-logged (not per-tick spam)
- L465 — `extern "C" int __cdecl OnPlayFootstep(void* creature)` — hook handler: mimics engine's field6_0x20==0 early-out, suppresses (returns 1) when leader is stuck and not in combat, or when ANY creature footsteps while leader is stuck
  note: hook REPLACES engine's JZ at mid-function cut (skip_original_bytes=true); return 1 jumps to engine's natural early-out epilogue, return 0 resumes normal audio path — see hooks.toml comment for why other cut points clobbered EFLAGS/EAX
