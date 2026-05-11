// Camera-state probe — F12 reads the engine's cached camera yaw at
// CSWCModule + 0x98 and logs it alongside character yaw and our
// dead-reckoned camera estimate.
//
// Purpose: characterise the relationship between the engine's true
// camera yaw (the value updated by CSWCModule::AcclTurnCamera @0x640090
// on every A/D keyboard rotation) and our compass-frame dead-reckoning
// in camera_announce. Logged data lets us deduce the unit (radians vs
// degrees), the frame (CCW from +X vs CW from N vs other), and any
// constant offset.
//
// Reachability chain (per swkotor.exe.h):
//   *kAddrAppManagerPtr -> AppManager
//     +0x04 -> CClientExoApp
//       +0x04 -> CClientExoAppInternal
//         +0x18 -> CSWCModule
//           +0x98 -> float (cached camera yaw, written by AcclTurnCamera)
//
// AcclTurnCamera decomp (verified 2026-05-11):
//   fVar2 = param_1 + *(float *)(iVar3 + 0x40);
//   *(float *)(iVar3 + 0x40) = fVar2;       // authoritative yaw at deeper struct
//   this->field31_0x98 = fVar2;              // cache on CSWCModule (this probe)
//
// Hotkey: F12 (unbound in stock kotor.ini).

#pragma once

namespace acc::probe_camera_state {

// OnUpdate per-tick poll. F12 rising edge dumps cached camera yaw +
// player yaw + dead-reckoned camera compass estimate so we can match
// units/frame.
void PollWin32();

}  // namespace acc::probe_camera_state
