# probe_mouselook.h (47 lines)

Header for the view-mode "Mouse Look" feasibility probe. Notes the file is
intended to go away (or the hotkey rebinds to a real view-mode toggle) once
the probe has answered its question. Documents the Shift+AltGr key choice
(AltGr alone is `announce_degrees`; Shift+AltGr avoids collision and is
unbound/rare in German typing).

## Declarations (in source order)

- L34 — `void PollWin32()`
  note: toggles engine Mouse Look, speaks on/off, kicks off TickSweep when landing ON.
- L44 — `void TickSweep()`
  note: emits SendInput relative-motion deltas at tick cadence over ~2.1s total; listener is camera-anchored, so audible panning during the sweep confirms mouse motion drives camera rotation.
