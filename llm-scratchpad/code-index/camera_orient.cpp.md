# camera_orient.cpp (356 lines)

Implementation of the closed-loop camera orient state machine with rate-based
predictive key release.

## Declarations (in source order)

- L16 — `namespace acc::camera_orient`
- L18 — `namespace` (anonymous)
- L21 — `constexpr size_t kClientInternalModuleOffset`
- L22 — `constexpr size_t kCSWCModuleCameraOffset`
- L27 — `constexpr size_t kModCameraQuaternionOffset`
  note: NOT used for arrival check (quaternion gives antipodal readings); only fallback yaw source
- L32 — `constexpr WORD kDikA`
- L33 — `constexpr WORD kDikD`
- L39 — `struct Rotation`
  note: single in-flight rotation; rate-based predictive release via prevYawRad/prevTickMs
- L50 — `Rotation g_rot`
- L54 — `constexpr DWORD kReleaseLookaheadMs`
- L57 — `constexpr float kFallbackArrivalRad`
- L61 — `constexpr float kMinRateRadPerMs`
- L65 — `constexpr DWORD kTimeoutMs`
- L67 — `constexpr float kPi`
- L68 — `constexpr float kTwoPi`
- L69 — `constexpr float kRadToDeg`
- L70 — `constexpr float kDegToRad`
- L74 — `void* SafeDeref(void* base, size_t offset)`
- L84 — `void* GetModule()`
- L95 — `void* GetCamera()`
- L105 — `bool ReadCurrentEngineYawRad(void* camera, float& out)`
  note: prefers camera_announce's position-derived yaw; quaternion fallback is multi-valued and only used before first anchor
- L125 — `void SendKey(WORD scan, bool down)`
- L133 — `float NormaliseRad(float r)`
- L139 — `float CompassDegToEngineRad(float compassDeg)`
- L146 — `float NextCardinalCompassDeg(float currentCompassDeg)`
  note: advances N→E→S→W→N (CW); maps compass-degree input to next 90-degree step
- L158 — `void ReleaseAndDisarm(const char* reason, float curYawRad)`
- L187 — `bool IsActive()`
- L192 — `void Tick()`
