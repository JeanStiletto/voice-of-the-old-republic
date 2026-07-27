# swoop_race.cpp (882 lines)

Implements the swoop-race lifecycle: detects entry/exit via a latched
`CSWMiniGame` pointer (area-chain lookup churns mid-transition, so the
struct is latched and revalidated by vtable each tick with a debounce),
narrates entry (opener + keybind cheat sheet combined into one utterance
so SpeakUrgent's interrupt=true can't truncate itself), tracks gear via
`max_speed` plateau jumps, fires a "shift ready" cue at the exact
onfire.ncs gate speed, and reconstructs the real race clock from the
engine world-timer + NWScript `MIN_TIME_*` globals (the engine has no
elapsed-race-time field). Delegates continuous obstacle/accelpad spatial
cues to `swoop_spatial_audio::TickSpatialAudio`/`ResetSpatialAudio`.
Talks to: engine_area, engine_player (AppManager chain), audio_bus,
audio_cues, prism (SpeakUrgent), strings, log.

## Declarations (in source order)

- L45 — `constexpr size_t kClientAreaMiniGameOffset = 0x264` — CSWCArea.mini_game
- L51-62 — CSWMiniGame offsets: vtable/resref/player(+0x24)/enemy_count/
  obstacle-data(+0x44)/obstacle-count/type(+0x80, 1=swoop 2=turret)
  note: type mis-read at +0x84=axis_x in an earlier pass, matched 0/3 by coincidence
- L71 — `kFollowerSpeedOffset = 0x98` — CSWTrackFollower.speed
- L76-78 — CSWMiniPlayer offset(+0x1c4)/min_speed/max_speed
- L116 — `kGearShiftMaxDeltaMs = 5.0f` — noise floor for "shift up" detection
  note: engine has no gear concept; gear ladder lives entirely in per-track NWScript
- L133 — `kShiftGateSpeed[]` — per-gear onfire.ncs upshift-legal speed gate
- L142 — `kShiftReadyCueResref` — reuses turret's "entered killable range" cue
- L150 — `kExitDebounceTicks = 60` — ~2s hold before announcing EXIT
- L174 — `struct State` — active/latched ptr+vtable/gear/race-timer fields
- L239 — `State g_state`
- L245-285 — `SafeReadPtr/SafeReadU32/SafeReadFloat/SafeReadVector` — SEH-guarded reads
- L290 — `void* ReadMiniGameViaArea()` — GetCurrentArea → GetClientArea → mini_game
- L303 — `bool LatchedStillValid()` — vtable re-read match = struct not freed/reused
- L333-336 — engine addresses for world-timer/global-var-table read (race clock)
  note: addresses decompiled 2026-06-23; rebased via acc::addr::R
- L352 — `void* GetServerApp()` — AppManager+0x8 → CServerExoApp
- L366 — `int ReadGlobalNumber(server, name)` — reads one NWScript global NUMBER
- L386 — `bool ReadWorldClockMs(server, outMs, outMph)`
- L437 — `void TickRaceTimer(miniGame)` — caches MIN_TIME_* start stamp once fresh,
  freezes elapsed at max_speed→0 (finish) or coast-to-stop fallback
  note: fallback gated on peak ACTUAL speed not envelope peak, else the launch ramp false-triggers a ~0s race
- L538 — `void EmitDiagnosticDump(miniGame)` — one-time full byte dump (process-once)
- L570 — `void EmitEntrySummary(miniGame)` — brief one-line per-entry log
- L587 — `void EmitDiagSnapshot(miniGame)` — 1Hz speed/tunnel-offset trace
- L613 — `void TickGearWatch(miniGame)` — detects max_speed plateau jump, speaks gear
- L651 — `void TickShiftReady(miniGame)` — fires shift-ready cue once per gear
- L696 — `void AnnounceEntry()` — combined opener+controls single utterance
- L709 — `void SpeakRaceEndMessage()` — idempotent; time-bearing or plain exit variant
- L742 — `void HandleEnter(mg)` — latches pointer, resets all per-race state
- L781 — `void HandleExit()` — stops spatial loops, fallback race-end speak
- L803 — `bool IsActive()`
- L805 — `void Tick()` — top-level state machine: idle detect (type==1 only) /
  latch-revalidate with debounce / gear+shift+timer+spatial-audio ticks
  note: obstacle/accelpad cues gated behind have_start_ms so they stay silent through the pre-race countdown
