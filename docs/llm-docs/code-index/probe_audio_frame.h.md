# probe_audio_frame.h (43 lines)

Audio-frame diagnostic probe.

F10 advances through the 8 compass sectors (N through NW), emitting a
3D-positional cue at 5m from the player in each direction and speaking
the spoken-direction name via Prism. Purpose: characterise the
relationship between our compass frame and the engine's 3D audio listener
frame. Pillar 3 beacon testing uncovered a possible mismatch where the
user perceived the heartbeat from a different direction than the
description implied.

F11 emits a fixed probe cue always at compass-North to test whether the
listener orientation is camera-driven vs character-driven.

Cue resref: "gui_open" (distinct from BeaconActive's "gui_check").
Hotkey: F10/F11 — unbound in stock kotor.ini; Win32-polling.

## Declarations (in source order)

- L27 — `namespace acc::probe_audio_frame`
- L41 — `void PollWin32()`
  note: reads VK_F10 (cycle sectors) and VK_F11 (fixed-North) via GetAsyncKeyState; self-gates on foreground + player loaded
