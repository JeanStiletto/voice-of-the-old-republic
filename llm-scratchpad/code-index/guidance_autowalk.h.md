# guidance_autowalk.h (79 lines)

Autowalk surface header. Wraps CSWSCreature::AddMoveToPointAction, CSWSCreature::ForceMoveToPoint, CSWSObject::AddUseObjectAction, and CSWSObject::ClearAllActions. Also declares engine address constants and the kInvalidObjectId sentinel.

## Declarations (in source order)

- L12 — `namespace acc::guidance`
- L19 — `bool WalkTo(const Vector& destination);`
  note: queues AddMoveToPointAction (NPC-only for the leader — silently no-ops in practice; kept for diagnostic use); arms the progress watchdog
- L25 — `bool ForceWalkTo(const Vector& destination);`
  note: diagnostic path via ForceMoveToPoint; used to discriminate queue-contention from input-mode failure
- L29 — `void TickProgressWatchdog();`
  note: fires one log line at t+1s and one at t+3s with displacement + stuck verdict; self-disengages after t+3s
- L38 — `bool UseObject(unsigned long targetHandle, const Vector& destHint = {0.0f, 0.0f, 0.0f});`
  note: enqueues ACTION_USEOBJECT (0x28) — the proven leader path; caller owns SetPlayerInputEnabled toggle
- L48 — `bool CancelMovement();`
  note: calls ClearAllActions; also clears in-flight state; caller should follow with SetPlayerInputEnabled(true)
- L53 — `bool IsAutowalkInFlight();`
- L57 — `void PollMovementKeysCancel();`
  note: cancels in-flight autowalk on fresh W/S/A/D/C/Y rising edge; engine-initiated autorun is untouched
- L62 — `constexpr uintptr_t kAddrCSWSCreatureAddMoveToPointAction = 0x004F8B60;`
- L66 — `constexpr uintptr_t kAddrCSWSCreatureForceMoveToPoint = 0x004EDBA0;`
- L70 — `constexpr uintptr_t kAddrCSWSObjectAddUseObjectAction = 0x0057C810;`
- L74 — `constexpr uintptr_t kAddrCSWSObjectClearAllActions = 0x004CCD80;`
- L78 — `constexpr unsigned long kInvalidObjectId = 0x7F000000;`
