# audio_bus.cpp (432 lines)

Core audio-cue playback funnel: wraps CExoSound's PlayOneShotSound / Play3DOneShotSound, plus the CExoSound::SetListenerPosition detour used by view mode. Owns the global cue-volume percent (persisted via mod_settings_store), and resolves the dedicated full-volume and near-field "spatial" priority groups by scanning the live CPriorityGroup table for installer-stamped FadeTime sentinels (31337 / 31338), falling back to vanilla group 26 if absent. PlayCue3D shifts the source position by (camera - character) so cues are character-relative against the engine's camera listener, skipped in view mode where the listener detour already substitutes the cursor. Talks to audio_pitch (BeginScopedZero/EndScopedZero around engine calls), engine_player, view_mode, mod_settings_store.

## Declarations (in source order)

- L21 — `struct CResRef { char string[16]; }` — engine resref tag, lowercased at fill
- L25 — `void FillResRef(CResRef&, const char*)` — defensive lowercase copy, truncates >16 chars
- L41 — `typedef PFN_PlayOneShotSound` — __thiscall; corrected field layout: priority_group, delay_ms, volume_byte, fixed_variance, pitch_variance
  note: earlier typedef mislabelled param_4/5/6; verified by decompile of CExoSoundInternal::PlayOneShotSound
- L52 — `typedef PFN_Play3DOneShotSound` — __thiscall, adds Vector position + z_offset
- L63 — `void* GetCExoSound()` — SEH-guarded deref of kAddrCExoSoundPtr
- L76-119 — global cue-volume percent (`g_cueVolumePercent`, `EnsureCueVolumeLoaded`, `EffectiveVolumeByte`) — lazily pulled from acc_settings.ini; volume_byte==0 means "muted, skip call" not engine-full
- L106-134 — priority-group struct offsets (`kSoundInternalOffset`, `kPriorityGroupsPtrOff`, stride 0x18, priority/volume/fade-time byte offsets)
  note: layout mirrors probe_priority_groups.cpp XML type DB (2026-05-14)
- L119 — `kCueGroupSentinelFadeTime = 31337` — installer-stamped flat full-volume group fingerprint
- L125 — `kSpatialCueGroupSentinelFadeTime = 31338` — installer's near-field 1m/8m falloff group
- L131 — `kFallbackFullGroup = 26` — vanilla group used when sentinel row absent
- L137 — `void SetGlobalCueVolumePercent(int)` — clamps, persists via settings::SetInt
- L146 — `int GetGlobalCueVolumePercent()`
- L160 — `int ScanForSentinelGroup(uint16_t sentinel)` — walks CPriorityGroup table up to kMaxGroupScan=40 rows, bails after 4 consecutive garbage rows; returns kScanNotReady(-1)/kScanAbsent(-2)/index
- L197 — `uint8_t GetCuePriorityGroup()` — cached resolve; NotReady returns fallback without caching so early-fire cues don't pin the fallback forever
- L222 — `uint8_t GetSpatialCuePriorityGroup()` — same pattern, falls back to GetCuePriorityGroup() if spatial sentinel absent
- L251 — `bool PlayCue(const char*, uint8_t priorityGroup, uint8_t baseVolume)` — 2D one-shot, pitch::BeginScopedZero/EndScopedZero bracketed
- L282 — `bool PlayCue3D(const char*, const Vector&, uint8_t, uint8_t)` — 3D one-shot with camera-relative bias, skipped under view_mode::IsActive()
- L353 — `typedef PFN_InternalSetListenerPosition` — __thiscall inner CExoSoundInternal setter
- L356 — `kAddrCExoSoundInternalSetListenerPosition = acc::addr::R(0x005D6600)`
- L361 — `kSubstituteCursorForListener` — diagnostic bool toggle (currently true)
- L366 — `extern "C" int __cdecl OnSetListenerPosition(void*, Vector**)` — hook handler; substitutes view-mode cursor position when active, else passes engine position through; edge+30s-heartbeat logging
  note: hook is skip_original_bytes=true; consumed_exit_address routes through the natural RET 4; esp+4 source is address-of-slot per KPatchManager's LEA-vs-MOV bug, deref once
