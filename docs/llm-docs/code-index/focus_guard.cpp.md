# diag_focus.cpp (632 lines)

Focus/foreground diagnostics + fixes: subclasses every game-owned top-level window (Render Window, SWMovieWindow-excluded, legacy Exo window) from a 100ms poll thread to log WM_ACTIVATE/ACTIVATEAPP/SETFOCUS/KILLFOCUS, drives DirectInput reacquire/release on foreground gain/loss (via engine_input), runs a bounded cold-start foreground-reclaim guard against Game Bar-style overlay theft, and detects+warns when Steam Big Picture is silently eating keystrokes. Talks to engine_input.h (RequestInputReacquire/Release), prism (SpeakUrgent), strings, log. Lock-free single-writer/many-reader append-only subclass table.

## Declarations (in source order)

- L32 — `struct SubclassedWindow { HWND hwnd; WNDPROC origWndProc; char tag[40]; }`
- L44 — `constexpr int kMaxSubclassedWindows = 64`
- L45-47 — `SubclassedWindow g_subclassed[64]`, `std::atomic<int> g_subclassedCount`, `std::atomic<HANDLE> g_pollThread`
  note: append-only; writer publishes via release-store on g_subclassedCount, readers acquire-load then scan
- L65-70 — cold-start guard consts/state: `kGuardWindowMs=10000`, `kMaxReclaims=6`, `g_guardDeadlineTick`, `g_guardReclaims`, `g_liveRenderWindow`
- L72 — `const SubclassedWindow* FindSubclassed(HWND hwnd)`
- L80/90 — `ApartmentTypeName`/`ApartmentQualifierName` — COM apartment enum-to-string
- L111 — `void LogForegroundThief(DWORD activatingThread)`
  note: on focus-loss, resolves + logs the exe/class/title of whoever stole the foreground (Steam/Discord/Game Bar)
- L159 — `LRESULT CALLBACK SubclassProc(...)`
  note: on WM_ACTIVATEAPP gain, calls RequestInputReacquire (deferred, not inline — mid-engine-activation-handling); on loss, RequestInputRelease + thief log (gated to "Render Window" tag to avoid one-log-per-window)
- L227 — `bool IsGameWindowClass(const char* cls)`
  note: excludes MSCTFIME UI/IME, SAPI's transient "CSpThreadTask Window" (flooded the table at 57/64 slots in one session), and SWMovieWindow (subclassing risks aborting Bink's movie queue)
- L250/271/273/284 — `LogOneWindow`, `EnumDiagState`, `EnumDiagProc`, `LogAllProcessWindows` — one-shot window inventory dump
- L298/300 — `ScanState`, `BOOL CALLBACK ScanProc(...)`
  note: single writer of g_subclassed[]; dedups via `acclog::Once` when table is full
- L365 — `void ForceForeground(HWND target, HWND fg)`
  note: AttachThreadInput dance, SEH-guarded; no synthetic Alt keypress
- L385 — `void MaybeReclaimForeground()` — drains the cold-start guard window/reclaim cap
- L440-533 — Big Picture input-blocked warning: `kWarnCooldownMs=20000`, `g_warnPending`, `ContainsNoCase`, `AnyInteractionKeyDown`, `ForegroundIsBigPicture`, `MaybeFlagInputBlockedWarning`
  note: edge+cooldown gated; queues rather than speaks inline (poll thread is not COM-safe)
- L535 — `DWORD WINAPI PollProc(LPVOID)` — 100ms loop: scan windows, reclaim, flag warning, heartbeat every 600 iters
- L568 — `void LogComApartment(const char* tag)` (public)
- L584 — `bool GameOwnsForeground()` (public)
- L592 — `void StartFocusProbe()` (public) — idempotent thread launch via CAS on g_pollThread
- L611 — `void DrainInputBlockedWarning()` (public) — speaks via `prism::SpeakUrgent` (bypasses NVDA typed-char cancel)
- L621 — `void ArmStartupForegroundGuard()` (public)
