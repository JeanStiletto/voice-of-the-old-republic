# probe_camera_distance.cpp (370 lines)

Diagnostic probe testing whether the camera's orbit-radius field can be
force-clamped to ~0 so the engine's camera-anchored audio listener collapses
onto the player ("Option B" for character-centric audio, avoiding a
SetListenerPosition/Orientation detour). Walks the AppManager -> CClientExoApp
-> CClientExoAppInternal -> CSWCModule -> Camera chain, calls
`Camera::vtable[0x80]` to get the active `CAurBehavior*`, and (for the
free-roam `CSWCameraOnAStick` behavior) writes its target-distance field
every tick while logging pre/post readback to detect the engine "stomping"
the write (auto-fit recompute). Also zeroes the auto-fit flag to suppress the
most likely stomp source. Ctrl+F12 does a one-shot full snapshot dump
(distance/yaw/pitch, camera-tuning globals like `cameraPersonalSpace`,
`cameraInterpDistAmt`, `cameraFreeStyle`); Ctrl+F11 cycles the clamp mode.

## Declarations (in source order)

- L21-22 — `constexpr size_t kClientInternalModuleOffset=0x18; kCSWCModuleCameraOffset=0x40`
- L27 — `constexpr size_t kCameraVtblGetBehaviorOffset = 0x80`
  note: Camera::vtable[32], called with 0xFFFFFFFF to fetch the active behavior.
- L31 — `constexpr size_t kBehaviorVtblAsStateOffset = 0x1c`
  note: CAurBehavior::vtable[7], zero-arg thiscall returning the inner state struct.
- L36-38 — `kBehAutoFitFlagOffset=0x84; kBehTargetDistOffset=0x110; kBehZOffsetOffset=0x120`
  note: verified via Ghidra decomp of Control_ComputeDesiredPosition; field46_0x110 is the smoothed-toward target orbit radius.
- L41-44 — engine addresses `kAddrCameraGetDist=0x0045C1D0; kAddrCameraGetYaw=0x0045C170; kAddrCameraGetPitch=0x0045C1A0; kAddrZoomCamera=0x006401D0`
- L61-64 — camera-tuning globals `kAddrCameraPersonalSpace, kAddrCameraInterpAmt1, kAddrCameraInterpDistAmt, kAddrCameraFreeStyle`
  note: read-only in the probe, logged to correlate a stomped clamp against e.g. a non-zero personal-space bubble.
- L72-91 — `void* SafeDeref(...); template<T> T SafeRead(...); template<T> bool SafeWrite(...)`
- L105 — `void* GetCSWCModule()`
- L116 — `void* GetCamera()`
- L125 — `void* GetActiveBehavior(void* camera)`
- L144 — `void* GetBehaviorStateStruct(void* behavior)`
- L162 — `float SafeCallGetFloat(uintptr_t addr, void* this_)`
- L172 — `void SafeCallZoom(void* module, float delta)`
- L189-198 — `enum class ClampMode { Off, Zero, Half, Two, COUNT }; ClampMode g_clampMode; float g_clampTarget`
- L203-205 — stomp-rate accounting: `DWORD s_lastRateLogMs; int s_ticksSinceRate, s_stompsSinceRate`
- L207 — `const char* ModeName(ClampMode m)`
- L217 — `float ModeTarget(ClampMode m)`
- L226 — `void AdvanceClampMode()`
  note: speaks the new mode via Prism so the user knows the mode without checking the log.
- L251 — `void DumpSnapshot()`
  note: one-shot, Ctrl+F12.
- L304 — `void TickClamp()`
  note: writes g_clampTarget every tick, zeroes the auto-fit flag, logs a stomp-rate summary once per second.
- L356 — `void Tick()`
  note: Ctrl+F12 = ProbeCameraDistDump; Ctrl+F11 = ProbeCameraDistClampToggle; TickClamp runs whenever mode != Off.
