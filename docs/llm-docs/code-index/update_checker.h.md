# update_checker.h (52 lines)

Public surface for the in-game auto-updater — a port of the sibling
arena project's `UpdateChecker.cs`. Flow: StartBackgroundCheck spawns a
WinHTTP worker thread against GitHub (called once from menus.cpp's main-
menu branch, deliberately AFTER intro-movie/OpenGL bringup finishes, to
avoid competing with Bink playback for the message-loop — an earlier
OnRulesInit call site was a leading suspect for "menu loaded but
unresponsive" reports); Tick() runs per-frame and announces completion +
drives download-task completion; F5 (main-menu-gated) triggers
HandleF5(). On download success, writes a handoff .bat that waits for
swkotor.exe to exit, runs the installer with `--auto-update` under UAC
elevation, relaunches via Steam, then calls ExitProcess.

## Declarations (in source order)

- L33 — `namespace acc::update_checker`
- L37 — `void StartBackgroundCheck()`
  note: idempotent — repeat calls dropped while a check is in flight or completed
- L41 — `void Tick()` — per-frame; cheap when idle
- L50 — `void HandleF5()`
  note: caller must gate on GetPlayerPosition == false (main menu/loading screens only) and announce UpdateNotInMenu otherwise
