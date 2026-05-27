# audio_footstep_suppress.h (41 lines)

Stuck-detection via footstep suppression. Tick() samples position; when
displacement is sub-epsilon the OnPlayFootstep detour suppresses the footstep
audio (silence = stuck cue for blind players). Gates on player leader identity
so NPC footsteps are never suppressed.

## Declarations (in source order)

- L28 — `namespace acc::audio::footstep_suppress`
- L31 — `void Tick()`
  note: self-gates on player resolved; idempotent.
- L35 — `bool WasStuckLastTick()`
  note: reflects the previous tick's displacement (~33ms lag at 30Hz); read by OnPlayFootstep.
- L39 — `void NoteLeaderFootstep()`
  note: stamps the timestamp the stuck-direction probe gates on; called from OnPlayFootstep without exposing internal state.
