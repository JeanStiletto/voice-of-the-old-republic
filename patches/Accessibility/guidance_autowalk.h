// Autowalk — wrapper over CSWSCreature::AddMoveToPointAction.
//
// Engine pathfinds + walks the character across walkmesh/transitions.
// We do not implement movement itself.

#pragma once

#include <cstdint>

#include "engine_offsets.h"
#include "engine_rebase.h"

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
// K2 witnessed by parallel decompile: identical flag-packing math, sets
// creature+0x11b0=2 (K1 +0xa8c), resets path_find_info(+0x380)->0x26c/270/274,
// then AddAction(1, group, 13 identical typed pairs) and
// SetLockOrientationToObject(0x7f000000, 0).
const uintptr_t kAddrCSWSCreatureAddMoveToPointAction = acc::addr::Pick(0x004F8B60, 0x00564770);

// CSWSCreature::ForceMoveToPoint — __thiscall(CSWSForcedAction*).
// Bypasses the queue; still pathfinds.
// K2 twin found by its 9 AddActionNodeParameter call sites (7 point-branch +
// 2 object-branch, unique in the binary) and confirmed by decompile: same
// 0x74 node, id 5 vs 0x30 on target==0x7f000000, JumpToPoint/JumpToObject
// tails, ret 4. K2 reads the CSWSForcedAction as {+4 group, +8 area,
// +0xc..0x14 loc, +0x18 target_object} — keep our struct layout in sync.
const uintptr_t kAddrCSWSCreatureForceMoveToPoint = acc::addr::Pick(0x004EDBA0, 0x00581070);

// CSWSObject::AddUseObjectAction — __thiscall(ulong, ulong) → int.
// Forwards to AddAction(this, 0x28=ACTION_USEOBJECT, ...).
// K2 witnessed by decompile: guard [this+0xec]!=0 (K1's commandable gate at
// +0xe8, the uniform +4 CSWSObject shift), then
// AddAction(0x28, 0xffff, 3, &target, 0...) — byte-for-byte K1 semantics.
// (K2 AddAction=0x0053F7F0, found via the push 0xffff; push 0x28 byte scan.)
const uintptr_t kAddrCSWSObjectAddUseObjectAction = acc::addr::Pick(0x0057C810, 0x005E0F30);

// CSWSObject::ClearAllActions — __thiscall(int) → void. param=0 here;
// semantics not fully decoded.
// K2 twin witnessed by TWO independent caller shapes matching KOTOR 1's:
// the K2 ActionInitiateDialog does GetServerObject → ClearAllActions(1) and
// the K2 DoPersonalAction does GetServerCreature → ClearAllActions(0),
// exactly where KOTOR 1's bodies call 0x004CCD80.
const uintptr_t kAddrCSWSObjectClearAllActions = acc::addr::Pick(0x004CCD80, 0x00541080);
