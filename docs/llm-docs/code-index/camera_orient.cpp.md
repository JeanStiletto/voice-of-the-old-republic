# camera_orient.cpp (361 lines)

Camera-orient hotkey (N key) state machine: either rotates to face the active guidance beacon's current target, or advances one cardinal CW (N→E→S→W→N) otherwise. Drive mechanism is synthesised SendInput keypresses of the player's *bound* turn key (scancode from engine_keymap::TurnScancode, honouring rebinds) — calling the engine's AcclTurnCamera directly from an out-of-band tick was tried and does not reliably move the chase-cam, since its accumulator is only consumed inside the engine's own per-frame UpdateCamera. A single in-flight `Rotation` struct tracks target yaw with rate-based predictive release: each tick samples yaw+timestamp and projects time-to-target, releasing the key ~40ms early (kReleaseLookaheadMs) to absorb SendInput→DirectInput→engine pipeline latency. Handles overshoot (engine ignored input, wrong-direction travel) and timeout (3s) as release reasons too. Talks to camera_announce (yaw source + arbitration), engine_keymap, guidance_beacon, hotkeys.

## Declarations (in source order)

- L22-23 — chain offsets (`kClientInternalModuleOffset`=0x18, `kCSWCModuleCameraOffset`=0x40) — CClientExoAppInternal→CSWCModule→camera
- L41 — `struct Rotation` — active flag, holdScan, debugKey, targetEngineYawRad, initialAbsDeltaRad (overshoot ref), startedMs, prevYawRad/prevTickMs/haveRateSample (rate window)
- L56-72 — release/arrival tuning (`kReleaseLookaheadMs`=40ms, `kFallbackArrivalRad`≈2.9°, `kMinRateRadPerMs`, `kTimeoutMs`=3000)
- L81 — `void* SafeDeref(void* base, size_t offset)` — SEH-guarded pointer chase
- L91 — `void* GetModule()` — walks AppManager→ClientApp→ClientExoAppInternal→CSWCModule chain
- L106 — `bool ReadCurrentEngineYawRad(void* camera, float& out)` — prefers camera_announce's position-derived yaw (single-valued); falls back to GetCameraYawRadians quaternion path
  note: quaternion path historically misread wrong fields (engine y/z instead of correct ones), which is why it looked "multi-valued" and was abandoned before being fixed
- L123 — `void* GetCamera()`
- L129 — `void SendKey(WORD scan, bool down)` — SendInput with KEYEVENTF_SCANCODE
- L137 — `float NormaliseRad(float r)` — wraps to (-π, π]
- L143 — `float CompassDegToEngineRad(float compassDeg)`
- L150 — `float NextCardinalCompassDeg(float currentCompassDeg)` — next CW cardinal (N→E→S→W)
- L162 — `void ReleaseAndDisarm(const char* reason, float curYawRad)` — SendKey up + logs final direction (camera_announce speaks it once IsActive() drops)
- L190 — `bool IsActive()` — true if g_rot.active OR the hotkey is currently held
  note: covers the rising-edge tick itself, since camera_announce::Tick runs before this in core_tick order
- L195 — `void Tick()` — in-flight: camera-lost abort, rate-projected arrival/overshoot/timeout decision; rising-edge: arms rotation toward beacon target or next cardinal, computes turn direction from engine-frame delta sign, synthesises keydown
