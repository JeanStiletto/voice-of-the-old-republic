// Autowalk — wrapper over CSWSCreature::AddMoveToPointAction.
//
// Engine pathfinds + walks the character across walkmesh/transitions.
// We do not implement movement itself.

#pragma once

#include <cstdint>

#include "engine_offsets.h"

namespace acc::guidance {

// True = queued (does not guarantee arrival — pathfinder may fail, target may
// be unreachable, the player may interrupt). False = no player loaded or engine
// call faulted. Pure dispatch primitive: watching the walk to completion is the
// approach tracker's job — arm it (guidance_approach.h) after a true return.
bool WalkTo(const Vector& destination);

// Diagnostic alternate via CSWSCreature::ForceMoveToPoint — bypasses the
// per-creature queue but still pathfinds. Used to discriminate "queue
// contention" from "input-mode dominance" failure modes. Log prefix
// "Autowalk: Force-..." for filtering.
bool ForceWalkTo(const Vector& destination);

// Enqueue ACTION_USEOBJECT (0x28) — same primitive as NWScript's
// ActionInteractObject. Engine handles walk-then-use sequencing. Caller owns
// the SetPlayerInputEnabled(false) toggle around dispatch, and arms the
// approach tracker (guidance_approach.h) for the in-flight / way-blocked
// lifecycle.
bool UseObject(unsigned long targetHandle);

// Clears the full action queue via CSWSObject::ClearAllActions — acceptable
// because the typical Shift+- stop case has only our move queued. The
// per-action primitive (CSWSCreature::RemoveAction by id) would need action-id
// tracking we don't yet have. Caller should follow with SetPlayerInputEnabled(
// true) to restore manual control immediately, and CancelApproach() to clear
// the tracker (this primitive no longer owns that state).
bool CancelMovement();

// Cancel ANY mod-armed autowalk — Shift+- discovery (Cycle) or Enter interact
// (Interact) — while a W/S/A/D/C/Y movement key is held. Level-triggered (a key
// already held when the walk dispatched still cancels, after the tracker's arm
// grace), so the user can always turn/walk their way out. Engine-initiated
// movement (autorun, area onEnter scripts, cutscene moves) never arms the
// tracker, so guidance::IsAnyModApproachInFlight() is false for it and it is
// structurally untouched. Delegates the owner-aware teardown to
// guidance::CancelByMovement().
void PollMovementKeysCancel();

}  // namespace acc::guidance

// CSWSCreature::AddMoveToPointAction — __thiscall, 17 stack args.
constexpr uintptr_t kAddrCSWSCreatureAddMoveToPointAction = 0x004F8B60;

// CSWSCreature::ForceMoveToPoint — __thiscall(CSWSForcedAction*).
// Bypasses the queue; still pathfinds.
constexpr uintptr_t kAddrCSWSCreatureForceMoveToPoint = 0x004EDBA0;

// CSWSObject::AddUseObjectAction — __thiscall(ulong, ulong) → int.
// Forwards to AddAction(this, 0x28=ACTION_USEOBJECT, ...).
constexpr uintptr_t kAddrCSWSObjectAddUseObjectAction = 0x0057C810;

// CSWSObject::ClearAllActions — __thiscall(int) → void. param=0 here;
// semantics not fully decoded.
constexpr uintptr_t kAddrCSWSObjectClearAllActions = 0x004CCD80;
