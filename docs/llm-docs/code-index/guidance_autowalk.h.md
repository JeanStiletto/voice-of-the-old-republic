# guidance_autowalk.h (67 lines)

Header for the CSWSCreature::AddMoveToPointAction wrapper family. Documents
that this module is a pure dispatch layer — the engine pathfinds and walks;
watching arrival/stall is guidance_approach's job. Also declares the engine
addresses used by the .cpp.

## Declarations (in source order)

- L13 — `namespace acc::guidance`
- L19 — `bool WalkTo(const Vector& destination)`
- L25 — `bool ForceWalkTo(const Vector& destination)`
- L32 — `bool UseObject(unsigned long targetHandle)`
- L40 — `bool CancelMovement()`
- L50 — `void PollMovementKeysCancel()`
- L55 — `kAddrCSWSCreatureAddMoveToPointAction` (0x004F8B60) — __thiscall, 17 stack args
- L59 — `kAddrCSWSCreatureForceMoveToPoint` (0x004EDBA0)
- L63 — `kAddrCSWSObjectAddUseObjectAction` (0x0057C810)
- L67 — `kAddrCSWSObjectClearAllActions` (0x004CCD80)
  note: param semantics not fully decoded; currently called with 0.
