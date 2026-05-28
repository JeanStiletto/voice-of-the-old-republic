# probe_camera_state.h (35 lines)

Camera-state probe — F12 reads the engine's cached camera yaw at
CSWCModule+0x98 and logs it alongside character yaw and our dead-reckoned
camera estimate.

Purpose: characterise the relationship between the engine's true camera
yaw (updated by AcclTurnCamera on every A/D rotation) and our compass-frame
dead-reckoning in camera_announce. Logged data lets us deduce unit
(radians vs degrees), frame (CCW from +X vs CW from N), and any constant
offset.

Reachability chain: AppManagerPtr → AppManager → CClientExoApp →
CClientExoAppInternal+0x18 → CSWCModule+0x98 (float cached yaw).

AcclTurnCamera write pattern documented inline.

Hotkey: F12 — unbound in stock kotor.ini.

## Declarations (in source order)

- L28 — `namespace acc::probe_camera_state`
- L33 — `void PollWin32()`
  note: F12 rising edge dumps cached camera yaw + player yaw + dead-reckoned estimate for unit/frame matching
