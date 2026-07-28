# audio_loop.cpp (270 lines)

Implements `LoopSource`, a RAII wrapper over engine-managed `CExoSoundSource` for sustained spatial audio (continuous tones, per-tick position updates, live pitch/volume control) where PlayCue3D's one-shot model doesn't fit. Start() constructs the engine source, sets resref/3D/position/distance-band/priority-group/looping/volume, then Play()s it, caching the sample's natural playback rate for later `SetPitchMultiplier` calls. SetPitchMultiplier reaches into CExoSoundSourceInternal's fields directly and calls the engine's own IAT-resolved `AIL_set_3D_sample_playback_rate` Miles import to push an absolute rate (bypassing the engine's randomised variance) — used by the turret elevation cue and swoop steering cues. All engine calls SEH-guarded; a fault during Start tears down and frees. Talks to audio_bus (address table, BiasForListener mirrors PlayCue3D's camera-relative shift), view_mode, engine_player.

## Declarations (in source order)

- L18 — `struct CResRef { char string[16]; }` — local mirror of audio_bus's tag struct
- L22 — `void FillResRef(CResRef&, const char*)`
- L37 — `kSourceStructSize = 16` — outer struct size we malloc/free (engine owns the 0xa0-byte internal via its own operator new/dtor)
- L46-54 — pitch-control offsets (`kSoundSourceInternalOffset`=0x04, `kInternalBaseFrequencyOffset`=0x48, `kInternalPitchVarFreqOffset`=0x54, `kInternalVoice3DOffset`=0x3c, `kVoiceHandleOffset`=0x04) + `kIatAilSet3DPlaybackRate` IAT slot
  note: decompile-verified against CExoSoundSourceInternal::SetPitchVariance @0x005dba40
- L57-70 — PFN typedefs for the CExoSoundSource lifecycle (Ctor, Dtor, SetResRef, Set3D, SetPosition, SetDistance, SetLooping, SetPriorityGroup, SetVolume, Play, Stop)
  note: PFN_SourceDtor bit0 always passed 0 — engine CRT may not match our DLL's, we free the outer struct with our own free()
- L74 — `Vector BiasForListener(const Vector&)` — camera-relative shift, skipped under view_mode
- L89 — `void TeardownEngineSource(void*, const char* whence)` — SEH-guarded dtor + free, used by Stop and Start's error path
- L103 — `LoopSource::~LoopSource()` — calls Stop()
- L107 — `bool LoopSource::Start(resref, worldPosition, looping, spatial, priorityGroup, volumeByte, maxVolDist, minVolDist)` — full construct/configure/play sequence; caches base_hz_ (0 disables pitch control, 2D sources skip it)
- L195 — `void LoopSource::UpdatePosition(const Vector&)` — SEH-guarded SetPosition; drops the pointer (source_=nullptr) on fault
- L209 — `void LoopSource::UpdateVolume(int volumeByte)` — clamped SetVolume; drops pointer on fault
- L224 — `void LoopSource::SetPitchMultiplier(float multiplier)` — clamps to [0.25,4.0]x, writes internal pitch-var field + calls Miles rate setter directly if the 3D voice exists yet
- L254 — `void LoopSource::Stop()` — idempotent; Stop() then TeardownEngineSource
