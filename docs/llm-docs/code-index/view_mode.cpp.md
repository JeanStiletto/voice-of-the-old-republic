# view_mode.cpp (744 lines)

Implementation of view mode. Cursor steps via W/S (forward/back) and A/D
(strafe left/right) polled directly with GetAsyncKeyState, normalized
against camera yaw so diagonals don't move faster than cardinals.
Collision via SegmentCrossesSurface, with a 5cm backoff so the next tick's
step doesn't re-trigger the same crossing. Hover-pause object narration
writes the unified narrated_target slot. Enter dispatch is deferred one
tick via PendingDispatch, because WalkTo/AI dispatch silently no-ops
without a fresh 1->0 input-enable transition that the engine only
processes on the NEXT tick after view mode exits.

## Declarations (in source order)

- L32 — `namespace acc::view_mode`
- L36-40 — `kCursorSpeedMps`, `kMaxDtSec`, `kHoverPauseMs`, `kHoverRadiusMeters`, `kCollisionCueQuietMs`
- L42 — `struct ViewModeState`
  note: cursor pos + yaw, hover pending/spoken handles + obj ptr, region pending/spoken text buffers
- L63 — `ViewModeState g_state`
- L67 — `bool g_enter_consumed_this_tick`
  note: single-tick ownership flag set by PollEnter, read-and-cleared by interact_hotkey
- L80 — `struct PendingDispatch`
  note: deferred Enter dispatch — armed by PollEnter after exiting view mode; fires next tick after engine settles
- L87 — `PendingDispatch g_pending`
- L89 — `constexpr DWORD kPendingDispatchMinElapsedMs`
- L103 — `void EnterViewMode()`
  note: calls SetPlayerInputEnabled(false, armAutoRestore=false); refuses and rolls back on player-position failure
- L144 — `void ExitViewMode()`
- L153 — `void ToggleViewMode()`
- L160 — `void DumpCameraStateProbe()`
  note: Shift+B diagnostic; snapshots CClientOptions bitfield and neighbours to acclog
- L210 — `void StepCursor(float dt)`
  note: W/S forward/back, D/A strafe right/left (raw GetAsyncKeyState polls); normalises diagonals; stops at SegmentCrossesSurface hit with 5cm backoff
- L300 — `acc::strings::Id CategoryNameId(acc::filter::CycleCategory c)`
- L317 — `bool ResolveCursorRegionLabel(area, cursor, outBuf, outSource)`
  note: three-tier: landmark (15m) → friendly room name → wall_topology shape; matches walking adapter tier order
- L368 — `void AnnounceCursorRegion(area, cursor)`
  note: text-equality dedup + 300ms hover-pause; world-speech-gated
- L417 — `void NarrateNearestObject(area, cursor)`
  note: iterates AreaObjectIterator for CycleCategory matches within kHoverRadiusMeters; stamps narrated_target on speak
- L509 — `void PollEnter()`
  note: handles both Enter and Shift+Enter; exits view mode synchronously, then arms PendingDispatch for next-tick dispatch
- L550 — `void ProcessPendingDispatch()`
  note: runs at top of Tick before active gate; reads narrated_target live — a passive_narrate stamp between PollEnter and now wins
- L603 — `bool IsActive()`
- L605 — `bool ConsumedEnterThisTick()`
- L611 — `bool TryGetCursorPosition(Vector& out)`
- L617 — `bool GetEffectiveOrientationYawDegrees(float& out)`
- L637 — `void PollWin32()`
  note: blocks toggle when dialog, sub-screen, modal popup, or blacklisted panel is foreground
- L709 — `void Tick()`
  note: ProcessPendingDispatch runs first (before active gate), then StepCursor, NarrateNearestObject, AnnounceCursorRegion, PollEnter
