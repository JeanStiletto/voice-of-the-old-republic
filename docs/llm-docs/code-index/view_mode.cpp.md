# view_mode.cpp (746 lines)

Implementation of view mode. Cursor steps via W/S/C/Y keys. Collision via
SegmentCrossesSurface. Hover-pause object narration writes the unified
narrated_target slot. Enter dispatch is deferred one tick via PendingDispatch
so the engine's input-enable state settles before AI dispatch.

## Declarations (in source order)

- L31 — `namespace acc::view_mode`
- L33 — `namespace` (anonymous)
- L35 — `constexpr float kCursorSpeedMps`
- L36 — `constexpr float kMaxDtSec`
- L37 — `constexpr DWORD kHoverPauseMs`
- L38 — `constexpr float kHoverRadiusMeters`
- L39 — `constexpr DWORD kCollisionCueQuietMs`
- L41 — `struct ViewModeState`
  note: cursor pos + yaw, hover pending/spoken handles + obj ptr, region pending/spoken text buffers
- L62 — `ViewModeState g_state`
- L66 — `bool g_enter_consumed_this_tick`
  note: single-tick ownership flag set by PollEnter, read-and-cleared by interact_hotkey
- L79 — `struct PendingDispatch`
  note: deferred Enter dispatch — armed by PollEnter after exiting view mode; fires next tick after engine settles
- L86 — `PendingDispatch g_pending`
- L88 — `constexpr DWORD kPendingDispatchMinElapsedMs`
- L100 — `void EnterViewMode()`
  note: calls SetPlayerInputEnabled(false, armAutoRestore=false); refuses and rolls back on player-position failure
- L141 — `void ExitViewMode()`
- L150 — `void ToggleViewMode()`
- L157 — `void DumpCameraStateProbe()`
  note: Shift+B diagnostic; snapshots CClientOptions bitfield and neighbours to acclog
- L207 — `void StepCursor(float dt)`
  note: W/S/C/Y keys; C=strafe-right, Y=strafe-left; normalises diagonals; stops at SegmentCrossesSurface hit with 5cm backoff
- L297 — `acc::strings::Id CategoryNameId(acc::filter::CycleCategory c)`
- L314 — `bool ResolveCursorRegionLabel(void* area, const Vector& cursor, char* outBuf, size_t bufSize, const char*& outSource)`
  note: three-tier: landmark (15m) → friendly room name → wall_topology shape; matches walking adapter tier order
- L366 — `void AnnounceCursorRegion(void* area, const Vector& cursor)`
  note: text-equality dedup + 300ms hover-pause; world-speech-gated
- L425 — `void NarrateNearestObject(void* area, const Vector& cursor)`
  note: iterates AreaObjectIterator for CycleCategory matches within kHoverRadiusMeters; stamps narrated_target on speak
- L517 — `void PollEnter()`
  note: handles both Enter and Shift+Enter; exits view mode synchronously, then arms PendingDispatch for next-tick dispatch
- L558 — `void ProcessPendingDispatch()`
  note: runs at top of Tick before active gate; reads narrated_target live — a passive_narrate stamp between PollEnter and now wins
- L611 — `bool IsActive()`
- L613 — `bool ConsumedEnterThisTick()`
- L619 — `bool TryGetCursorPosition(Vector& out)`
- L625 — `bool GetEffectiveOrientationYawDegrees(float& out)`
- L640 — `void PollWin32()`
  note: blocks toggle when dialog, sub-screen, modal popup, or blacklisted panel is foreground
- L712 — `void Tick()`
  note: ProcessPendingDispatch runs first (before active gate), then StepCursor, NarrateNearestObject, AnnounceCursorRegion, PollEnter
