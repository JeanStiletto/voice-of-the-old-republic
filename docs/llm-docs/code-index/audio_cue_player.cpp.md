# audio_cue_player.cpp (117 lines)

Pillar-1 single callsite for "play a NavCue at a world position": gates on the per-kind Mod Settings toggle (core_settings pillar1 + WallSounds menu toggle for Wall specifically), gates on squared-distance range, then calls `PlayCue3D` riding the near-field spatial priority group so loudness tracks distance across the ~5m awareness band. Landmark cues are permanently disabled here (TTS-only). Talks to audio_bus, audio_cues, core_settings, menus_modsettings.

## Declarations (in source order)

- L16 — `const char* CueLabel(NavCue cue)` — English log-only label switch
- L37 — `bool IsCueEnabled(NavCue cue)` — per-kind toggle dispatch
  note: Wall additionally ANDs the Mod Settings WallSounds switch; Landmark always returns false (TTS-only, avoids empty-resref PlayCue3D call); Collision/Beacon* always true (owned upstream)
- L73 — `float DistanceSquared(const Vector&, const Vector&)` — 3D squared distance, avoids sqrt on cold path
- L82 — `bool PlayCueAtPosition(NavCue, const Vector& worldPos, const Vector& listenerPos, float rangeMax)` — enabled-gate → range-gate → PlayCue3D with GetSpatialCuePriorityGroup(); full-fidelity logging on every drop/play path
