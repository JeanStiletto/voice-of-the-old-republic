# audio_loop.cpp (155 lines)

Implements LoopSource over the CExoSoundSource lifecycle (ctor, SetResRef,
Set3D, SetPosition, SetLooping, Play, Stop, dtor). Applies the same
character-relative listener bias as PlayCue3D so loops + one-shots land at
consistent world positions. The outer 16-byte struct is caller-owned; engine
owns the internal 0xa0-byte CExoSoundSourceInternal.

## Declarations (in source order)

- L12 — `namespace acc::audio`
- L14 — `namespace` (anonymous)
- L17 — `struct CResRef`
  note: local mirror of the 16-byte tag from audio_bus.cpp (not shared to avoid header coupling).
- L21 — `void FillResRef(CResRef& out, const char* tag)`
- L38 — `typedef void* (__thiscall* PFN_SourceCtor)(void* this_)`
- L40 — `typedef void (__thiscall* PFN_SourceDtor)(void* this_, unsigned char free_flag)`
  note: free_flag bit 0 = "engine _free(this) after destruct"; always pass 0 — caller owns outer alloc.
- L43 — `typedef void (__thiscall* PFN_SetResRef)(void* this_, const CResRef* res, int param2)`
- L44 — `typedef void (__thiscall* PFN_Set3D)(void* this_, int enabled)`
- L45 — `typedef void (__thiscall* PFN_SetPosition)(void* this_, const Vector* pos, float z_offset)`
- L46 — `typedef void (__thiscall* PFN_SetLooping)(void* this_, int looping)`
- L47 — `typedef int (__thiscall* PFN_Play)(void* this_)`
- L48 — `typedef void (__thiscall* PFN_Stop)(void* this_)`
- L52 — `Vector BiasForListener(const Vector& worldPosition)`
  note: mirrors PlayCue3D's camera-minus-character offset so loops and one-shots pan consistently.
- L67 — `void TeardownEngineSource(void* src, const char* whence)`
  note: SEH-guarded dtor + free; used by Stop and Start error path.
- L81 — `LoopSource::~LoopSource()`
- L85 — `bool LoopSource::Start(const char* resref, const Vector& worldPosition)`
- L125 — `void LoopSource::UpdatePosition(const Vector& worldPosition)`
- L139 — `void LoopSource::Stop()`
