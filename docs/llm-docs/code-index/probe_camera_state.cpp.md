# probe_camera_state.cpp (161 lines)

One-shot F12 diagnostic dump that reads the engine's cached camera yaw
(`CSWCModule + 0x98`, written by `AcclTurnCamera`) plus the camera's world
orientation quaternion, player yaw, and this mod's own dead-reckoned camera
estimate (`camera_announce::TryGetCameraEngineYawDegrees`), all converted to
compass degrees via `engine_compass::EngineYawToCompass`. Purpose: cross-check
units/frame/offset between the engine's true camera yaw and the mod's
dead-reckoning so `camera_announce` can be validated or corrected.

## Declarations (in source order)

- L19-27 — chain-walk offsets `kClientInternalModuleOffset=0x18; kCSWCModuleCameraYawOffset=0x98; kCSWPlayerControlCameraOffset=0x08; kCameraYawOffsetA=0x90; kCameraYawOffsetB=0x94; kCameraYawOffsetC=0x40`
  note: A/B/C are candidate yaw-offset fields referenced in Control_UpdateCameraDesiredOrientation, logged for back-comparison rather than trusted.
- L30 — `void* GetClientInternal()`
- L46 — `void* GetCSWCModule(void* clientInternal)`
- L57 — `void* GetPlayerControlCamera(void* clientInternal)`
- L72 — `template<T> T SafeRead(void* base, size_t offset, T fallback)`
- L85 — `void PollWin32()`
  note: reads camera quaternion at modCamera+0x88 (engine layout w,x,y,z — w first) and position at +0x7c; yaw computed via the shared engine_compass::GetCameraYawRadians helper (an earlier `2*atan2(qz,qw)` read the wrong quaternion fields).
