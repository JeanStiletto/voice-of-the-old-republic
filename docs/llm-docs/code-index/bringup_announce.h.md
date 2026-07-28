# bringup_announce.h (56 lines)

Header for the loading-phase nag. Documents why `IsInputPumpLive()` is the stop signal for core_tick's cold-start DirectInput reacquire retry (immune to the mislabeled cursor-position channel that floods the input hook), and why `IsMovieWindowForeground()` must stay phase-independent (KOTOR's movie player aborts its play queue if focus churns during playback — cf. the Alt+Tab-during-intros bug).

## Declarations (in source order)

- L23 — `void Start()` — idempotent; call once from OnRulesInit after Prism init
- L28 — `void NotifyInputPumpLive()` — called from menus.cpp on first detected arrow-key nav on MainMenu panel
- L39 — `bool IsInputPumpLive()` — true once the input pump has provably delivered an event to a panel
- L54 — `bool IsMovieWindowForeground()` — true iff a SWMovieWindow owned by this process is foreground; safe from any thread
