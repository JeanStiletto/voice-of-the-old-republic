# camera_spin_diag.cpp (256 lines)

Camera-spin guard + diagnostic for the "character spins endlessly with no input" bug. Root cause (decompiled `UpdateCamera` @0x5f5e10): when the mouse cursor sits in the left/right screen-edge band, the engine calls `AcclTurnCamera` every frame regardless of actual input. The fix (guard): once an edge-turn spin is confirmed live (in-world, foreground, cursor in band, camera actually rotating ≥3°/s), the engine's GUI-manager mouse-X field is written directly to viewport centre — bypassing a known-broken injected-motion path (SetCursorPos/SendInput both failed post-load per earlier log evidence). The detection band is floored at 6px rather than trusting `screenFramePercentage` config arithmetic, which under-reported the real band and let one session spin unrecoverably. Also runs a quaternion-vs-position-derived yaw cross-check as a regression tripwire for `GetCameraYawRadians`. Logging is episode-based (one START, one END-summary) rather than per-frame. Talks to engine_options (GetMouseLook), engine_player, hotkeys.

## Declarations (in source order)

- L22-26 — engine global addresses (`kAddrGuiManagerPtr`=0x007A39F4, `kAddrScreenFramePercent`=0x007A2444, `kAddrRightClickHeld`=0x008338F0) + GUI struct offsets (`kGuiMouseXOffset`=0x00, `kGuiViewportWidthOffset`=0x6C)
- L41 — `kGuardRateThresh = 3.0f` — deg/s onset threshold, keeps guard quiet on a static paused menu
- L47 — `kEpisodeQuietMs = 500` — gap before closing an edge-guard episode
- L52 — `kReadAnomalyDeg = 2.0f` — quat-vs-position yaw divergence tripwire threshold
- L54 — `template<T> T SafeRead(uintptr_t addr, T fallback)` — SEH-guarded absolute read
- L63 — `template<T> T SafeReadOff(void* base, size_t off, T fallback)`
- L74 — `template<T> bool SafeWriteOff(void* base, size_t off, T value)` — used for the direct mouse-X centre-write fix
- L86 — `float NormDeg(float d)`
- L93 — `float AngularDelta(float a, float b)` — signed shortest delta in (-180,180]
- L97 — `struct State { bool have; float prevYaw; DWORD prevMs; }` — rotation-rate sample state
- L106 — `struct Episode` — active, startMs, lastEdgeMs, corrections, edgeFrames, maxRate, startYaw
- L118 — `void EndEpisode(DWORD now, float endYaw)` — logs summary line
- L129 — `void Tick()` — in-world gate; quaternion yaw + position yaw cross-check; rotation-rate calc; cursor edge-band detection (floored at kMinBandPx=6); fires the guard write when edge+rotating+foreground; episode-based START/active-tracking/END logging; read-anomaly tripwire log (rate-limited to 1/s)
