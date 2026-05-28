# audio_cue_player.cpp (111 lines)

Implementation of PlayCueAtPosition. Applies per-kind enabled checks against
core_settings (and the Mod Settings wall-sounds toggle for Wall cues). Landmark
cue is permanently disabled here — it announces via TTS only. Guidance/view-mode
cues (Collision, Beacon*) always pass.

## Declarations (in source order)

- L11 — `namespace acc::audio`
- L13 — `namespace` (anonymous)
- L16 — `const char* CueLabel(NavCue cue)`
  note: English log labels only — used in acclog::Write calls, not for speech.
- L37 — `bool IsCueEnabled(NavCue cue)`
- L73 — `float DistanceSquared(const Vector& a, const Vector& b)`
- L82 — `bool PlayCueAtPosition(NavCue cue, const Vector& worldPos, const Vector& listenerPos, float rangeMax)`
