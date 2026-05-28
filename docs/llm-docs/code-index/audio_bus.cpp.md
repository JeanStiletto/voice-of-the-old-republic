# audio_bus.cpp (253 lines)

Implements PlayCue / PlayCue3D and the CExoSound::SetListenerPosition detour
used to substitute the virtual cursor as the listener during view mode. The
volume parameter on PlayCue3D is vestigially ignored (passed as 0) due to a
resolved typedef mislabelling — noted inline.

## Declarations (in source order)

- L13 — `namespace acc::audio`
- L15 — `namespace` (anonymous)
- L19 — `struct CResRef`
  note: local 16-byte engine resource tag; FillResRef lowercases the tag defensively.
- L23 — `void FillResRef(CResRef& out, const char* tag)`
- L39 — `typedef void (__thiscall* PFN_PlayOneShotSound)(...)`
  note: param layout corrected from earlier mislabelled version; real fields are priority_group, delay_ms, volume_byte, fixed_variance, pitch_variance.
- L50 — `typedef void (__thiscall* PFN_Play3DOneShotSound)(...)`
- L61 — `void* GetCExoSound()`
- L71 — `bool PlayCue(const char* resref, uint8_t priorityGroup, uint8_t volumeByte)`
- L98 — `bool PlayCue3D(const char* resref, const Vector& worldPosition, float volume)`
- L170 — `namespace acc::audio` (re-opened for SetListenerPosition detour locals)
- L171 — `namespace` (anonymous)
- L173 — `typedef void (__thiscall* PFN_InternalSetListenerPosition)(void* internal_, Vector* pos)`
- L176 — `constexpr uintptr_t kAddrCExoSoundInternalSetListenerPosition = 0x005D6600`
- L181 — `constexpr bool kSubstituteCursorForListener = true`
  note: diagnostic toggle; false = always passthrough regardless of view mode.
- L186 — `extern "C" int __cdecl OnSetListenerPosition(void* exoSound, Vector** posSlot)`
  note: hook handler — substitutes virtual cursor position for the engine listener when view mode is active; consumed_exit_address routes to RET 4.
