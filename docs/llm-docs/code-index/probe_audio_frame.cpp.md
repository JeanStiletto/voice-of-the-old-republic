# probe_audio_frame.cpp (105 lines)

Diagnostic probe characterising the relationship between the mod's compass
frame and the engine's 3D audio listener frame. F10 advances through 8
compass sectors, firing a 3D positional cue ("gui_open") 5m from the player
in each direction while speaking the expected direction name; F11 always
fires fixed-North, used to test whether listener orientation is camera- or
character-anchored (compare pans across camera rotation). Talks to
`audio_bus` (PlayCue3D), `engine_compass` (SectorString), `engine_player`
(GetPlayerPosition), `hotkeys`, `prism`.

## Declarations (in source order)

- L21-22 — `constexpr float kProbeDistance = 5.0f; kPi = 3.14159265358979f`
- L27 — `constexpr const char* kProbeResref = "gui_open"`
  note: chosen distinct from any nav cue and from BeaconActive's "gui_check" so probe fires are audibly distinguishable.
- L29 — `int g_nextDirection`
  note: advances 0..7 each F10 press.
- L39 — `Vector OffsetFor(int sector)`
  note: compass convention 0=N(+Y) CW; X=sin(compass), Y=cos(compass).
- L53 — `void FireProbe(int sector, const char* tag)`
  note: no-ops silently (logged) when no player is loaded.
- L85 — `void PollWin32()`
  note: F10 = ProbeAudioCycle (advancing sector), F11 = ProbeAudioFire (fixed-North).
