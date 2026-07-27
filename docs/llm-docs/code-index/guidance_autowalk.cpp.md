# guidance_autowalk.cpp (344 lines)

Pure dispatch primitives over CSWSCreature's movement/use action queue:
WalkTo (AddMoveToPointAction, primed via ActionManager(8) — the missing step
that made earlier standalone dispatches bail), ForceWalkTo (ForceMoveToPoint,
bypasses the queue), UseObject (AddUseObjectAction / ACTION_USEOBJECT), and
CancelMovement (ClearAllActions). Watching a dispatched walk to completion is
NOT this file's job — that is guidance_approach's tracker, which callers arm
after a successful dispatch. PollMovementKeysCancel reclaims manual control
from any mod-armed walk when a bound movement key is held, gated on
foreground-game and not self-tripping on camera_orient's snap-turn SendInput.

## Declarations (in source order)

- L21 — `namespace acc::guidance`
- L31 — `PFN_AddMoveToPointAction` — 17-arg __thiscall typedef
  note: full signature decoded in investigation §Q3; internally releases any facing lock via SetLockOrientationToObject.
- L60 — `struct CSWSForcedAction` — 28-byte layout (action_id/group_id/target_area/target_loc/target_object)
- L71 — `PFN_ForceMoveToPoint` typedef
- L81 — `float HorizontalDistance(const Vector&, const Vector&)`
- L88 — `constexpr size_t kServerObjectAreaIdOffset = 0x8c`
- L97 — `kAddrCSWSCreatureActionManager` (0x004f8770), `void PrimeActionManager(void*, int mode)`
  note: mode 8=move/walk; the native click-to-move handler always calls this before AddMoveToPointAction — omitting it left field427 stuck at 2.
- L109 — `int ReadServerObjInt(void*, size_t off, int dflt)`
- L121 — `bool WalkTo(const Vector& destination)` — public
  note: passes secondary=(0,0,0) WITH priming to force full A* pathfind instead of the LOS direct-move shortcut.
- L211 — `bool ForceWalkTo(const Vector& destination)` — public
- L257 — `bool UseObject(unsigned long targetHandle)` — public
- L285 — `bool CancelMovement()` — public; ClearAllActions(0)
- L310 — `void PollMovementKeysCancel()` — public
  note: engine-initiated movement never arms the tracker, so this can only ever cancel a mod-dispatched walk.
