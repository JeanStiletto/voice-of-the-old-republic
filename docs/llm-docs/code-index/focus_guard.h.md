# diag_focus.h (65 lines)

Public surface for diag_focus.cpp's focus/window diagnostics and cold-start foreground-guard fix. Called from OnRulesInit (early) and MainMenu first-sight (belt-and-braces); all entry points are idempotent.

## Declarations (in source order)

- L29 — `void LogComApartment(const char* tag)`
  note: checks whether prism's SAPI backend CoInitialize'd MTA (can conflict with Bink/engine COM expecting STA)
- L33 — `void StartFocusProbe()`
  note: idempotent; spawns the 100ms window-subclass poll thread
- L47 — `void ArmStartupForegroundGuard()`
  note: bounded reclaim window (Game Bar-style overlay theft); requires StartFocusProbe running; disarms permanently after window/cap
- L54 — `bool GameOwnsForeground()`
  note: gates cold-start DirectInput acquire so we never acquire while an overlay is in front
- L62 — `void DrainInputBlockedWarning()`
  note: call once per tick on the engine main thread (COM-safe); speaks queued Big-Picture-blocked warning
