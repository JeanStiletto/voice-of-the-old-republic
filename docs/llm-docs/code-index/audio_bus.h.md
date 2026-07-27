# audio_bus.h (148 lines)

Public API + address table for the audio-cue funnel. Documents the decompile-verified gain chain (`final ≈ priority_group.volume × source_volume × master_SFX_slider`) and that CExoSoundSource::SetVolume hard-clamps source_volume to 127 — "louder than full" is a property of the priority group's volume, not this byte. Also declares the full CExoSoundSource lifecycle address table used by audio_loop.cpp.

## Declarations (in source order)

- L31 — `kCueVolumeFull = 127` — engine per-source unity volume
- L39-40 — `SetGlobalCueVolumePercent(int)` / `GetGlobalCueVolumePercent()` — user-facing hint-sounds slider, [0,100]
- L48 — `uint8_t GetCuePriorityGroup()` — resolved/cached full-volume sentinel group
- L56 — `uint8_t GetSpatialCuePriorityGroup()` — near-field (1m/8m) sentinel group for passive proximity cues
- L70 — `bool PlayCue(const char* resref, uint8_t priorityGroup=0, uint8_t baseVolume=kCueVolumeFull)` — 2D centred one-shot
- L90 — `bool PlayCue3D(const char*, const Vector& worldPosition, uint8_t priorityGroup=0, uint8_t baseVolume=kCueVolumeFull)` — 3D positional; priorityGroup 0xb needed for cues that must survive SetSoundMode pause
- L97 — `kAddrCExoSoundPtr = 0x007A39EC` — CExoSound* facade, nullptr in early DLL-attach
- L100 — `kAddrCExoSoundPlayOneShotSound = acc::addr::R(0x005D5E00)` — __thiscall, RET 0x18
- L104 — `kAddrCExoSoundPlay3DOneShotSound = acc::addr::R(0x005D5E10)` — __thiscall, RET 0x28
- L109 — `kAddrCExoSoundSetListenerPosition = acc::addr::R(0x005D5DF0)` — hook target for view-mode override
- L116 — `kAddrCExoSoundSourceInternalCalculatePitchVarianceFrequency = 0x005DB3D0` — jitter-neutralise hook target
- L119-148 — `CExoSoundSource` lifecycle address table (Ctor, CtorWithResRef, Dtor, SetPriorityGroup, Set3D, Play, SetVolume, SetPitchVariance, SetLooping, SetPosition, Stop, SetFixedVariance, GetLooping, SetDistance, SetResRef) — all __thiscall, ECX=source; used by audio_loop.cpp's LoopSource
