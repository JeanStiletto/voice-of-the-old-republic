# probe_audio_frame.cpp (105 lines)

Implementation of the audio-frame diagnostic probe. Fires 3D-positional
cues at computed world offsets from the player position.

## Declarations (in source order)

- L17 — `namespace acc::probe_audio_frame`
- L40 — `Vector OffsetFor(int sector)`
  note: converts a 0..7 compass sector to a world-space XY offset at kProbeDistance (5m); compass convention 0=N(+Y), 2=E(+X), etc.
- L54 — `void FireProbe(int sector, const char* tag)`
  note: internal helper — speaks direction name, plays 3D cue, logs result; not declared in header
- L86 — `void PollWin32()`
