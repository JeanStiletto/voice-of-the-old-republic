# camera_announce.cpp (282 lines)

Per-tick camera-direction announcer: derives compass yaw from atan2(player - camera) each tick (the orbital camera always looks at the character), buckets into 8×45° sectors with 5° hysteresis, and speaks sector changes via `prism::SpeakUrgent` (routes around NVDA's typed-char cancel since the user is often holding A/D). Detects active rotation from the camera's own angular velocity (not specific key reads, so it's key-binding-independent) to decide between a "stable for kQuietMs" announce and a "held-interval" announce during sustained turning, plus a release-edge announce of the final sector when rotation stops. Mutes while an engine cinematic drives the camera (`HasActiveDialogPanel`) or while `camera_orient` is auto-driving, and silently re-anchors after either ends so the scripted/auto direction is never spoken as a player turn. Also exposes `AnnounceCurrentFacing` for one-shot readouts (e.g. after a door autoturn) with a dedup window against the per-tick announcer's own recent announce. Talks to camera_orient, engine_compass, engine_panels, engine_player, hotkeys, prism.

## Declarations (in source order)

- L23-46 — sector/timing constants (`kSectorSize`=45, `kHalfSector`=22.5, `kHysteresis`=5, `kQuietMs`=250, `kMinIntervalHeldMs`=300, `kRotatingThresholdDps`=30, `kMinXYDistance`=0.1)
- L48 — `float AngularDelta(float a, float b)` — signed shortest delta in (-180,180]
- L53-65 — module statics: last/pending spoken sector, change/spoken timestamps, cached compass, velocity-tracking prev compass+timestamp, `s_prevRelevantHeld`, `s_mutedByCutscene`
- L67 — `bool ReadCameraCompass(float& outCompass)` — atan2(player-camera) → EngineYawToCompass; false if XY distance < kMinXYDistance
- L87 — `void Tick()` — main state machine: reset on invalid read, mute+re-anchor around HasActiveDialogPanel and camera_orient::IsActive(), first-tick anchor, angular-velocity-derived rotation detection, release-edge announce, sticky-hysteresis sector tracking, quiet/held-interval speech gate
- L231 — `bool AnnounceCurrentFacing(unsigned int dedupMs)` — one-shot readout with dedup against s_lastSpokenSector/s_lastSpokenAt; false during dialog panel or unresolved camera
- L273 — `bool TryGetCameraEngineYawDegrees(float& out)` — exposes s_lastCamCompass converted back to engine frame; false until Tick has anchored
