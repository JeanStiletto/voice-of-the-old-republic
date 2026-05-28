# probe_mouselook.h (46 lines)

Phase 4 lay-off 2 — view-mode "Mouse Look" probe.

Diagnostic-only hotkey: Shift+AltGr toggles `CClientOptions.mouse_look`
and announces the new state. Intended to answer the long-term-plan question
"does forcing engine Mouse Look ON give us 90% of view mode for free?".

Once the probe has informed the view-mode design, this file goes away or the
hotkey rebinds to the actual view-mode toggle.

Key choice rationale: AltGr alone is `announce_degrees`; Shift+AltGr is
unbound by stock kotor.ini and rare in German typing. Same Win32-polling
rationale as other unbound probes.

## Declarations (in source order)

- L26 — `namespace acc::probe_mouselook`
- L34 — `void PollWin32()`
  note: reads Shift+AltGr rising edge, toggles mouse_look, logs pre/post/readback, speaks cue, starts synthetic sweep on toggle-to-ON
- L44 — `void TickSweep()`
  note: per-tick driver for the synthetic mouse-sweep state machine (park-at-apex shape: 0.3s ramp → 1.5s hold → 0.3s ramp back); cheap when idle
