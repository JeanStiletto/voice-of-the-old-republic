# bringup_announce.cpp (199 lines)

Loading-phase nag: a dedicated polling thread (100ms Sleep loop) classifies the foreground window class name into a Phase state machine (Booting → MoviesPlaying → Loading → Responsive, latched — never regresses from Responsive) and, while in Loading, watches for held nav keys (arrows/Enter/Space via GetAsyncKeyState) to speak a two-stage nag: stage 1 "please wait" on first nav-key press, stage 2 (15s later, if still pressing and still Loading) "press Alt+F4 and cancel the dialog" workaround. Fully decoupled from diag_focus — classification comes only from GetForegroundWindow + class name + the input-pump-live signal. Talks to prism (Speak), strings, log.

## Declarations (in source order)

- L15 — `enum class Phase` — Booting=0, MoviesPlaying=1, Loading=2, Responsive=3
- L23 — `const char* PhaseName(Phase)`
- L33-38 — atomics: `g_phase`, `g_seenMovie` (sticky), `g_announcedWait`, `g_announcedStuck`, `g_waitNagMs`, `g_thread`
- L43 — `kStuckThresholdMs = 15000` — stage-2 delay after stage-1
- L48 — `bool IsOurWindowOfClass(HWND, const char* targetClass)` — process-id + class-name check, SEH-free (tolerates dead handles)
- L58 — `void TransitionTo(Phase next, const char* reason)` — resets both nag latches + timestamp on any phase change
- L75 — `Phase ClassifyForeground()` — SWMovieWindow→MoviesPlaying (sets g_seenMovie); Render Window→Responsive/Loading/Booting depending on prior state and g_seenMovie; foreign windows leave phase unchanged
- L96 — `bool UserNavKeyHeld()` — GetAsyncKeyState high-bit poll over arrows/Enter/Space
- L110 — `DWORD WINAPI PollProc(LPVOID)` — main loop: classify → transition (blocked once Responsive) → two-stage nag check → Sleep(100)
- L169 — `void Start()` — CreateThread(CREATE_SUSPENDED) + compare_exchange guard against double-start, then ResumeThread
- L187 — `void NotifyInputPumpLive()` — TransitionTo(Responsive)
- L191 — `bool IsInputPumpLive()`
- L195 — `bool IsMovieWindowForeground()` — phase-independent; also catches mid-game cutscene movies, not just startup intros
