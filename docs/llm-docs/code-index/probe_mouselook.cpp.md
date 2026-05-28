# probe_mouselook.cpp (194 lines)

Implementation of the Mouse Look probe. Contains the sweep state machine
and SendInput-based mouse-motion emission. Key finding captured in
comments: Mouse Look ON does NOT capture the cursor — OS cursor escapes
the window over a +1000px sweep, requiring pre-sweep GetCursorPos capture
and post-sweep SetCursorPos restore.

## Declarations (in source order)

- L14 — `namespace acc::probe_mouselook`
- L37 — `struct SweepState`
  note: holds active flag, started_at, emitted_dx, emit_count, cursor_at_start, cursor_captured
- L57 — `int TargetCumulativeDx(DWORD t)`
  note: computes cumulative dx target for elapsed ms; park-at-apex curve: ramp-up 0→1000px over 300ms, hold 1500ms, ramp-down 300ms
- L72 — `void EmitMouseDelta(int dx)`
  note: calls SendInput with MOUSEEVENTF_MOVE (relative); logs on failure
- L87 — `void StartSweep()`
  note: initialises g_sweep, captures cursor position, logs sweep parameters
- L107 — `void PollWin32()`
- L150 — `void TickSweep()`
