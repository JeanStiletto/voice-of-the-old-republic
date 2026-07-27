# core_tick.cpp (451 lines)

The canonical "what fires per tick" list — ~50 subsystem calls in explicit, load-bearing order inside `Dispatch()`, each wrapped in a `PHASE(name, call)` macro that feeds a watchdog (`kSlowDispatchMs=200`, `kSlowGapMs=750`) logging a single labelled line on a stall instead of a silent multi-second freeze. Also owns `RetryColdStartReacquire`, a bounded (50 attempts / 200ms cadence) re-drive of `ForceReacquireInput` for the cold-start DirectInput keyboard-dead bug, stopping once `bringup_announce::IsInputPumpLive()`. A block comment documents one hard ordering constraint: camera_announce → door_announce → locked_recall → camera_orient → camera_spin_diag → spatial::change_detector → transitions → view_mode, because each reads state the previous one just built (camera yaw, wall cache, wall_topology). Combat subsystems (combat.TickCombatMode/TickCombatLog/TickCombatAbsorb/TickCombatDeflect/TickCombatEffects, combat::query::TickLeaderChangeAutoAnnounce, combat::queue::Tick, combat_diag::Tick, combat::special_watch::Tick) all fire from one contiguous block after footstep_suppress.

## Declarations (in source order)

- L61 — `namespace acc::tick`
- L87-88 — `constexpr double kSlowDispatchMs = 200.0`, `kSlowGapMs = 750.0`
- L90-97 — watchdog statics: `g_qpcFreq`, `g_lastEnd`, `g_haveLast`, `g_worstName`, `g_worstMs`, `g_totalMs`
- L99 — `LARGE_INTEGER QpcNow()`
- L105 — `double QpcMs(const LARGE_INTEGER&, const LARGE_INTEGER&)`
- L111 — `void PhaseMark(const char* name, double ms)`
- L118 — `#define PHASE(name, call)`
- L126 — `LARGE_INTEGER WatchdogBeginTick()`
  note: logs "SLOW FRAME gap=...ms" when the inter-tick wall gap exceeds kSlowGapMs — catches stalls OUTSIDE our Dispatch
- L147 — `void WatchdogEndTick(const LARGE_INTEGER& tickStart)`
  note: logs "SLOW TICK total=...ms" naming the single slowest phase
- L189-190 — `constexpr ULONGLONG kReacquireRetryMs = 200`, `constexpr int kReacquireMaxAttempts = 50`
- L192 — `void RetryColdStartReacquire()`
  note: only while game owns foreground; stops permanently once input pump proven live; gives up + logs after 50 attempts
- L230 — `void Dispatch()`
  note: ~50 PHASE()-wrapped subsystem calls in fixed order; see file summary for the one documented hard-ordering block (camera/door/locked_recall/spatial/transitions/view_mode)
- L443 — `#undef PHASE`
- L449 — `extern "C" void __cdecl OnUpdate(void*)`
  note: the CSWGuiManager::Update detour; just calls acc::tick::Dispatch()
