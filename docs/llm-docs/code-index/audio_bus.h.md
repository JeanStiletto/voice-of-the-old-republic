# audio_bus.h (109 lines)

Thin wrappers over CExoSound's one-shot entry points. Goes through the engine's
own audio pipeline so the SFX slider, focus-pause, EAX reverb, and voice budgeting
apply automatically. All functions nothrow, SEH-guarded.

## Declarations (in source order)

- L18 — `namespace acc::audio`
- L31 — `bool PlayCue(const char* resref, uint8_t priorityGroup = 0, uint8_t volumeByte = 0)`
  note: 2D one-shot cue (centred, no spatial position).
- L38 — `constexpr float kAccCueGain = 4.0f`
  note: linear amplitude scalar applied as the volume argument to PlayCue3D; 4.0x lands cues audible-but-clean.
- L51 — `bool PlayCue3D(const char* resref, const Vector& worldPosition, float volume = kAccCueGain)`
  note: shifts source by (camera - character) so spatial pan is character-relative without touching the listener for other sounds.
- L57 — `constexpr uintptr_t kAddrCExoSoundPtr = 0x007A39EC`
- L60 — `constexpr uintptr_t kAddrCExoSoundPlayOneShotSound = 0x005D5E00`
- L63 — `constexpr uintptr_t kAddrCExoSoundPlay3DOneShotSound = 0x005D5E10`
- L68 — `constexpr uintptr_t kAddrCExoSoundSetListenerPosition = 0x005D5DF0`
- L76 — `constexpr uintptr_t kAddrCExoSoundSourceInternalCalculatePitchVarianceFrequency = 0x005DB3D0`
- L94 — `constexpr uintptr_t kAddrCExoSoundSourceCtor = 0x005D5870`
- L95 — `constexpr uintptr_t kAddrCExoSoundSourceCtorWithResRef = 0x005D60E0`
- L96 — `constexpr uintptr_t kAddrCExoSoundSourceDtor = 0x005D60A0`
- L97 — `constexpr uintptr_t kAddrCExoSoundSourceSetPriorityGroup = 0x005D5900`
- L98 — `constexpr uintptr_t kAddrCExoSoundSourceSet3D = 0x005D5910`
- L99 — `constexpr uintptr_t kAddrCExoSoundSourcePlay = 0x005D5930`
- L100 — `constexpr uintptr_t kAddrCExoSoundSourceSetVolume = 0x005D5950`
- L101 — `constexpr uintptr_t kAddrCExoSoundSourceSetPitchVariance = 0x005D5980`
- L102 — `constexpr uintptr_t kAddrCExoSoundSourceSetLooping = 0x005D59D0`
- L103 — `constexpr uintptr_t kAddrCExoSoundSourceSetPosition = 0x005D59E0`
- L104 — `constexpr uintptr_t kAddrCExoSoundSourceStop = 0x005D5A20`
- L105 — `constexpr uintptr_t kAddrCExoSoundSourceSetFixedVariance = 0x005D5A30`
- L106 — `constexpr uintptr_t kAddrCExoSoundSourceGetLooping = 0x005D6190`
- L107 — `constexpr uintptr_t kAddrCExoSoundSourceSetDistance = 0x005D61A0`
- L108 — `constexpr uintptr_t kAddrCExoSoundSourceSetResRef = 0x005D61C0`
