# audio_cue_player.h (28 lines)

Single callsite for "play NavCue at position". Wraps audio::PlayCue3D with
two shared gates: per-kind toggle from core_settings, and awareness-range cap.
No per-NavCue debounce — change detector's per-feature cadence is the control.

## Declarations (in source order)

- L20 — `namespace acc::audio`
- L23 — `bool PlayCueAtPosition(NavCue cue, const Vector& worldPos, const Vector& listenerPos, float rangeMax)`
  note: returns true iff audio_bus accepted; false on gate reject or singleton miss.
