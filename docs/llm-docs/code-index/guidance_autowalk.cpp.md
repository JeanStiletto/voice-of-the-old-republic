# guidance_autowalk.cpp (562 lines)

Implementation of the autowalk primitives. Includes WalkTo (AddMoveToPointAction with detailed diagnostic field reads), ForceWalkTo (ForceMoveToPoint queue-bypass), UseObject (AddUseObjectAction — the primary player-walk path), CancelMovement, in-flight tracking, movement-key cancel polling, and the two-checkpoint progress watchdog.

## Declarations (in source order)

- L15 — `namespace acc::guidance`
- L17 — `namespace { // anonymous`
- L25 — `typedef unsigned int (__thiscall* PFN_AddMoveToPointAction)(...)`
  note: full 17-arg signature for CSWSCreature::AddMoveToPointAction
- L54 — `struct CSWSForcedAction`
  note: 28-byte layout (static_assert-verified) for CSWSCreature::ForceMoveToPoint argument
- L65 — `typedef void (__thiscall* PFN_ForceMoveToPoint)(void* this_, CSWSForcedAction* action);`
- L73 — `struct WatchdogState`
  note: diagnostic-only; captures dispatch baseline and fires t+1s/t+3s log lines; self-disengages
- L83 — `WatchdogState g_watchdog;`
- L98 — `struct InFlightState`
  note: distinct from watchdog; tracks whether an acc-dispatched autowalk is still in flight for the toggle-cancel gate
- L102 — `InFlightState g_inFlight;`
- L107 — `void ArmWatchdog(const Vector& startPos, bool haveStart, const Vector& dest, const char* tag)`
- L119 — `float HorizontalDistance(const Vector& a, const Vector& b)`
- L125 — `} // namespace (anonymous)`
- L127 — `bool WalkTo(const Vector& destination)`
- L282 — `bool ForceWalkTo(const Vector& destination)`
- L334 — `bool UseObject(unsigned long targetHandle, const Vector& destHint)`
- L376 — `bool CancelMovement()`
- L412 — `bool IsAutowalkInFlight()`
- L416 — `void PollMovementKeysCancel()`
  note: rising-edge gate prevents cancellation if W was already held at dispatch time
- L473 — `void TickProgressWatchdog()`
  note: also handles in-flight arrival check (dist < 1m clears g_inFlight); watchdog self-disengages after t+3s
