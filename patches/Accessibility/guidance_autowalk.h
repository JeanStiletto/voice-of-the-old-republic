// Cross-cutting autowalk — wrapper over CSWSCreature::AddMoveToPointAction.
//
// Layer: guidance/ (depends on engine_offsets.h's Vector and on
// engine_player for resolving the player's server-side creature pointer).
// Phase 2 lay-off 5 foundation; consumers land in lay-off 6 (Pillar 4
// Shift+- → autowalk binding) and later in Pillar 2 view mode + Pillar 3
// pathfind selection.
//
// AddMoveToPointAction queues a move action on the engine's per-creature
// action queue; the engine's existing AI pathfinder + move-to loop then
// drives the character across walkmesh/inter-area transitions. We do not
// implement movement itself.
//
// Decoded 17-arg signature in investigation §Q3 (2026-05-03). Most params
// are AI hints / follow-leader knobs / packed flag bits; the
// minimum-viable click-to-move call uses INVALID_OBJECT_ID (0x7f000000)
// for the two object-ref slots, zeroes for every flag/timeout/radius, and
// the destination as both primary and secondary point.
//
// Self-gates and SEH-wraps on the same model as engine_player / audio_bus:
// false return on no-player-loaded (main menu, area-load) or any raised
// exception during the engine call.

#pragma once

#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::guidance {

// Enqueue an auto-walk action for the player creature toward a world
// position. True = action queued (does not guarantee arrival — pathfinder
// may fail, target may be unreachable across the walkmesh, the player
// may interrupt with directional input). False = no player creature
// loaded or the engine call faulted.
//
// Side effect: arms the progress watchdog. Every call logs a single
// "Autowalk: dispatch ..." line including the engine's return value;
// subsequent OnUpdate ticks emit watchdog lines (see TickProgressWatchdog).
bool WalkTo(const Vector& destination);

// Diagnostic alternate path: bypass the per-creature action queue via
// CSWSCreature::ForceMoveToPoint @0x004EDBA0. Same destination semantics
// as WalkTo (engine still pathfinds; this is NOT a teleport), but
// dispatches directly without going through the queue. Used to
// discriminate "queue contention" from "input-mode dominance" / "missing
// flag bit" failure modes when WalkTo is silently dropped.
//
// Signature decoded via SARIF (`void __thiscall ForceMoveToPoint(
// CSWSForcedAction*)`). Caller constructs a CSWSForcedAction with the
// destination embedded; INVALID_OBJECT_ID for area/object handles since
// the same-area "go to a world position" use case has no specific
// area/object to reference.
//
// Same watchdog behaviour as WalkTo. Logs prefix "Autowalk: Force-..."
// instead of "Autowalk: ..." so the two paths are easy to discriminate
// in the log stream.
bool ForceWalkTo(const Vector& destination);

// Per-tick progress watchdog. Call from OnUpdate. When a WalkTo was
// dispatched recently, emits up to two diagnostic log lines:
//   t+1s  — "Autowalk: t+1s moved=X.XXm dist=Y.YYm (stuck|moving)"
//   t+3s  — "Autowalk: t+3s moved=X.XXm dist=Y.YYm (still stuck|reached|moving)"
// "stuck" = displacement < 0.1m at the 1s mark. The canonical signature
// for "engine accepted the AddMoveToPointAction call but didn't dispatch
// the queued action" — separates engine-acceptance from engine-execution.
//
// Watchdog disengages after t+3s. Idle when no recent WalkTo — no per-tick
// noise when autowalk isn't active. Cheap when idle (one bool check).
void TickProgressWatchdog();

// Enqueue an ACTION_USEOBJECT (0x28) action on the player creature, with
// the supplied target handle. Wraps `CSWSObject::AddUseObjectAction
// @0x0057c810` — the same primitive `ExecuteCommandActionInteractObject`
// (NWScript's ActionInteractObject) calls. Engine handles walk-to-then-
// use sequencing internally: pathfinds to target, then triggers the
// kind-appropriate USE callback (open door, loot container, ...).
//
// Returns true if the engine call dispatched cleanly. False on no
// player loaded or SEH fault. Caller is responsible for the
// SetPlayerInputEnabled(false) toggle around the dispatch — without it,
// the per-tick input loop clobbers the queued move (see
// project_player_control_toggle.md).
bool UseObject(unsigned long targetHandle);

// Cancel any in-flight autowalk by clearing the player creature's full
// action queue. Wraps `CSWSObject::ClearAllActions @ 0x004ccd80` (named
// in Lane's DB; bytes verified 2026-05-04 via DumpBytes).
//
// Trade-off: ClearAllActions clears the *entire* action queue, not just
// the autowalk. Acceptable for v1 because (a) the typical "I want to
// stop" case is during a Shift+- autowalk where the queue holds only
// our dispatched move, and (b) the more precise primitive
// `CSWSCreature::RemoveAction(ulong action_id) @ 0x004f76c0` would need
// us to track engine-side action ids (the value returned from
// `AddMoveToPointAction` — semantics not yet fully decoded; safer to
// avoid until needed).
//
// Side-effect: also clears `g_inFlight.active` (so `IsAutowalkInFlight`
// returns false on the next call) and disarms the diagnostic watchdog.
// Caller should follow with `acc::engine::SetPlayerInputEnabled(true)`
// to restore manual control immediately rather than waiting for the
// 3-second auto-restore.
//
// Returns true if the dispatch succeeded; false on no player loaded or
// SEH fault. The internal state is cleared either way — even if the
// engine call faults, we treat the in-flight tracking as stale.
bool CancelMovement();

// Reports whether an autowalk dispatch is currently in flight — i.e.
// `WalkTo` / `ForceWalkTo` was called and the player has neither
// reached the destination nor been cancelled. Used by `cycle_input`
// to decide whether the next Shift+- press dispatches a new walk or
// cancels the current one.
//
// "In flight" is set on a successful dispatch and cleared on:
//   - explicit `CancelMovement()`,
//   - the watchdog's per-tick distance check observing
//     `dist < 1.0m` from the destination (player arrived),
//   - or the player creature becoming unresolvable (un-loaded
//     mid-flight, area teardown).
//
// Idle cost: one bool check. Active cost (in flight): one position
// read + one horizontal-distance compare per OnUpdate tick.
bool IsAutowalkInFlight();

}  // namespace acc::guidance

// CSWSCreature::AddMoveToPointAction — __thiscall, 17 stack args.
// Signature decoded in investigation §Q3.
constexpr uintptr_t kAddrCSWSCreatureAddMoveToPointAction = 0x004F8B60;

// CSWSCreature::ForceMoveToPoint — __thiscall(CSWSForcedAction*),
// returns void. SARIF-confirmed signature. Bypasses the per-creature
// action queue (per investigation §Q3 row); still pathfinds.
constexpr uintptr_t kAddrCSWSCreatureForceMoveToPoint = 0x004EDBA0;

// CSWSObject::AddUseObjectAction — __thiscall(ulong target_id, ulong
// param2) -> int. SARIF CONFIRMED. Decompiled body just gates on
// `field48_0xe8 != 0` then forwards to AddAction(this, 0x28, 0xffff, 3,
// &target_id, 0, NULL...). 0x28 == ACTION_USEOBJECT.
constexpr uintptr_t kAddrCSWSObjectAddUseObjectAction = 0x0057C810;

// CSWSObject::ClearAllActions — __thiscall(int) -> void. SARIF CONFIRMED
// (named in Lane's DB; entry bytes captured 2026-05-04 via DumpBytes:
// `a1 1c 28 83 00 83 ec 10 85 c0 56 8b f1 74 22 ...`). The `int param_1`
// is currently passed as 0 — semantics not fully decoded; first-fire
// in-game test covers the case where 0 isn't sufficient (we'd see no
// movement-stop and escalate to 1).
constexpr uintptr_t kAddrCSWSObjectClearAllActions = 0x004CCD80;

// Engine sentinel for "no object" in AI-queue object-id slots
// (objectId1/objectId2 of AddMoveToPointAction, the
// SetLockOrientationToObject release-call, and several siblings in the
// AIAction* family). Distinct from CGameObjectArray's removed-handle
// sentinel (0xFFFFFFFF) — different layer.
constexpr unsigned long kInvalidObjectId = 0x7F000000;
