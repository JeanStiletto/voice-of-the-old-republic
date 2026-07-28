# probe_audio_frame.h (44 lines)

Header for the audio-frame diagnostic probe (F10/F11). Explains the purpose:
disambiguating whether the engine's audio listener orientation is
camera-driven or character-driven, motivated by a Pillar-3 beacon-testing
mismatch between perceived cue direction and the spoken compass description.

## Declarations (in source order)

- L41 — `void PollWin32()`
  note: Win32-polled (unbound in stock kotor.ini); self-gates on foreground window + player loaded.
