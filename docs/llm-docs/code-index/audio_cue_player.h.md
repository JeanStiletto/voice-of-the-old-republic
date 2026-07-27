# audio_cue_player.h (28 lines)

Header for the Pillar-1 cue-playback single callsite. Documents its two shared gates (per-kind toggle + per-call awareness-range cap) and explicitly notes there is no per-NavCue debounce here — the spatial change detector's per-feature last-cued-distance is the cadence control.

## Declarations (in source order)

- L23 — `bool PlayCueAtPosition(NavCue cue, const Vector& worldPos, const Vector& listenerPos, float rangeMax)` — true iff audio_bus accepted; false on gate reject or singleton miss
