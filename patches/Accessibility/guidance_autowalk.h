// Autowalk — wrapper over CSWSCreature::AddMoveToPointAction.
//
// Engine pathfinds + walks the character across walkmesh/transitions.
// We do not implement movement itself.

#pragma once

#include <cstdint>

#include "engine_offsets.h"

namespace acc::guidance {

// True = queued (does not guarantee arrival — pathfinder may fail,
// target may be unreachable, the player may interrupt). False = no
// player loaded or engine call faulted.
//
// Arms the progress watchdog.
bool WalkTo(const Vector& destination);

// Diagnostic alternate via CSWSCreature::ForceMoveToPoint — bypasses the
// per-creature queue but still pathfinds. Used to discriminate "queue
// contention" from "input-mode dominance" failure modes. Log prefix
// "Autowalk: Force-..." for filtering.
bool ForceWalkTo(const Vector& destination);

// Watchdog. Emits one line at t+1s and one at t+3s with displacement +
// stuck verdict. Self-disengages after t+3s.
void TickProgressWatchdog();

// Enqueue ACTION_USEOBJECT (0x28) — same primitive as NWScript's
// ActionInteractObject. Engine handles walk-then-use sequencing.
// Caller owns the SetPlayerInputEnabled(false) toggle around dispatch.
//
// destHint arms the in-flight tracker so IsAutowalkInFlight returns
// true; pass zero vector to skip arming (e.g. interact_hotkey's picker
// fallback where the engine picks the geometry).
bool UseObject(unsigned long targetHandle,
               const Vector& destHint = {0.0f, 0.0f, 0.0f});

// Clears the full action queue via CSWSObject::ClearAllActions —
// acceptable because the typical Shift+- stop case has only our move
// queued. The per-action primitive (CSWSCreature::RemoveAction by id)
// would need action-id tracking we don't yet have.
//
// Also clears in-flight state. Caller should follow with
// SetPlayerInputEnabled(true) to restore manual control immediately.
bool CancelMovement();

// In-flight set on successful dispatch; cleared on CancelMovement,
// arrival (<1m), or player un-load.
bool IsAutowalkInFlight();

// Cancel an in-flight autowalk on a fresh W/S/A/D/C/Y rising edge.
// Engine-initiated autorun (NPC perception, cutscenes) is untouched —
// IsAutowalkInFlight is only true for our own dispatches.
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
