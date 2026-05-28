# audio_footstep_suppress.cpp (423 lines)

Implements footstep suppression, stuck-direction probe (8-cardinal walkmesh +
body clearance raycast), and circling detection. Two independent stuck detectors:
TickStuckAnnounce (< 0.5m over 2s window while walk-anim fresh) and
TickCirclingAnnounce (path/net-displacement ratio >= 2.5 over 4s window).
Both funnel into RunStuckProbe which speaks free cardinal directions.

## Declarations (in source order)

- L16 — `namespace acc::audio::footstep_suppress`
- L18 — `namespace` (anonymous)
- L83 — `struct ProbeDir`
  note: pairs a unit direction vector with a strings::Id for speech and a short log tag.
- L95 — `void RunStuckProbe(const Vector& pos)`
  note: probes 8 cardinals against cached walkmesh walls and nearby creature/placeable bodies; speaks free directions via prism::SpeakUrgent.
- L217 — `void TickCirclingAnnounce(const Vector& pos, uint64_t now_ms, bool walking)`
  note: detects oscillating motion (high path/net ratio over 4s); fires RunStuckProbe once per circling episode.
- L275 — `void TickStuckAnnounce(const Vector& pos, uint64_t now_ms)`
  note: detects motionless-while-walking (< 0.5m over 2s); fires RunStuckProbe once per stuck episode.
- L315 — `bool WasStuckLastTick()`
- L317 — `void NoteLeaderFootstep()`
- L321 — `void Tick()`
- L393 — `extern "C" int __cdecl OnPlayFootstep(void* creature)`
  note: hook handler for CSWCCreature::PlayFootstep detour; returns 1 to suppress (jump to early-out), 0 to play; also calls NoteLeaderFootstep for the stuck probe gate.
