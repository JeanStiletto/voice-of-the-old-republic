# probe_camera_state.h (36 lines)

Header for the F12 camera-yaw diagnostic dump. Documents the AppManager ->
CSWCModule+0x98 reachability chain and the verified `AcclTurnCamera` decompile
excerpt showing the cached-yaw write.

## Declarations (in source order)

- L33 — `void PollWin32()`
  note: F12, unbound in stock kotor.ini; dumps cached camera yaw + player yaw + dead-reckoned estimate for unit/frame comparison.
