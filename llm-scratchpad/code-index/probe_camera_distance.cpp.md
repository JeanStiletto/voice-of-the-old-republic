# probe_camera_distance.cpp (368 lines)

Implementation of the camera-distance probe. Walks the
AppManager→CClientExoApp→CClientExoAppInternal→CSWCModule→Camera→ActiveBehavior
chain, reads/writes field46_0x110 (target orbit radius), and tracks per-second
stomp rate to determine whether the engine's auto-fit branch overwrites our
clamp.

## Declarations (in source order)

- L15 — `namespace acc::probe_camera_distance`
- L65 — `typedef float (__thiscall* PFN_CameraGetFloat)(void* this_)`
- L66 — `typedef void (__thiscall* PFN_ZoomCamera)(void* this_, float delta)`
- L67 — `typedef void* (__thiscall* PFN_CameraGetBehavior)(void* this_, unsigned int tag)`
- L71 — `void* SafeDeref(void* base, size_t offset)`
  note: SEH-guarded pointer dereference at base+offset
- L81 — `template <typename T> T SafeRead(void* base, size_t offset, T fallback)`
- L92 — `template <typename T> bool SafeWrite(void* base, size_t offset, T value)`
- L104 — `void* GetCSWCModule()`
  note: walks AppManager chain to CSWCModule
- L115 — `void* GetCamera()`
- L124 — `void* GetActiveBehavior(void* camera)`
  note: calls Camera::vtable[0x80](0xFFFFFFFF) to get active behavior; vtable-safe
- L143 — `void* GetBehaviorStateStruct(void* behavior)`
  note: calls behavior::vtable[7]() — returns inner state struct used by AcclTurnCamera
- L161 — `float SafeCallGetFloat(uintptr_t addr, void* this_)`
- L171 — `void SafeCallZoom(void* module, float delta)`
- L188 — `enum class ClampMode : int`
  note: Off/Zero(0.0m)/Half(0.5m)/Two(2.0m) — per-tick clamp target modes
- L206 — `const char* ModeName(ClampMode m)`
- L216 — `float ModeTarget(ClampMode m)`
- L225 — `void AdvanceClampMode()`
  note: cycles g_clampMode, speaks new mode name via Prism, resets stomp accounting
- L250 — `void DumpSnapshot()`
  note: one-shot full state dump on Ctrl+F12 — logs chain pointers, GetDist/GetYaw/GetPitch, behavior fields, tuning globals
- L303 — `void TickClamp()`
  note: per-tick write+readback of kBehTargetDistOffset; zeroes auto-fit flag; logs 1-second rate summary with stomp counts
- L355 — `void Tick()`
