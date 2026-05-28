# probe_camera_state.cpp (161 lines)

Implementation of the camera-state probe. Walks the AppManager chain to
CSWCModule and the PlayerControl camera, reads a quaternion from the
module-owned Camera object, derives yaw via atan2(qz, qw), and logs
alongside candidate raw floats at pcCamera+0x90/0x94 and the dead-reckoned
camera yaw from camera_announce.

## Declarations (in source order)

- L15 — `namespace acc::probe_camera_state`
- L30 — `void* GetClientInternal()`
  note: SEH-guarded walk of AppManager→CClientExoApp→CClientExoAppInternal
- L46 — `void* GetCSWCModule(void* clientInternal)`
- L57 — `void* GetPlayerControlCamera(void* clientInternal)`
  note: reads CSWPlayerControl+0x08 (CAurCamera*)
- L72 — `template <typename T> T SafeRead(void* base, size_t offset, T fallback)`
- L85 — `void PollWin32()`
