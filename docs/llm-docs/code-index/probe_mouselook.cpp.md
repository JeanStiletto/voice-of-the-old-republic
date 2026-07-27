# probe_mouselook.cpp (195 lines)

Diagnostic probe for the "does forcing engine Mouse Look ON give us view-mode
for free?" question. Shift+AltGr toggles `CClientOptions.mouse_look`
(`engine_options::ToggleMouseLook`) and speaks the new state; toggling ON also
kicks off a synthetic mouse-motion sweep via `SendInput` (park-at-apex shape:
0.3s ramp out, 1.5s hold, 0.3s ramp back, total 1000px dx) so a non-sighted
user can listen for whether the soundscape pans. Captures and restores the OS
cursor position around the sweep because Mouse Look ON does not capture the
cursor to the window (observed: cursor can escape to another monitor over a
1000px sweep).

## Declarations (in source order)

- L37-46 — `struct SweepState { active, started_at, emitted_dx, emit_count, cursor_at_start, cursor_captured }; SweepState g_sweep`
- L48-51 — `constexpr DWORD kRampMs=300; kHoldMs=1500; constexpr int kApexDxPx=1000; constexpr DWORD kSweepEndMs = 2*kRampMs+kHoldMs`
- L57 — `int TargetCumulativeDx(DWORD t)`
  note: piecewise linear ramp-up/hold/ramp-down cumulative-dx curve.
- L72 — `void EmitMouseDelta(int dx)`
  note: SendInput with MOUSEEVENTF_MOVE (relative motion).
- L87 — `void StartSweep()`
- L107 — `void PollWin32()`
  note: Shift+AltGr rising edge; gates on player loaded + successful GetMouseLook/ToggleMouseLook; speaks MouseLookOn/Off.
- L150 — `void TickSweep()`
  note: on completion, emits a residual delta to zero out rounding drift and restores the captured cursor position.
