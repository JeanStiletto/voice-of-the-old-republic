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

}  // namespace acc::guidance

// CSWSCreature::AddMoveToPointAction — __thiscall, 17 stack args.
// Signature decoded in investigation §Q3.
constexpr uintptr_t kAddrCSWSCreatureAddMoveToPointAction = 0x004F8B60;

// CSWSCreature::ForceMoveToPoint — __thiscall(CSWSForcedAction*),
// returns void. SARIF-confirmed signature. Bypasses the per-creature
// action queue (per investigation §Q3 row); still pathfinds.
constexpr uintptr_t kAddrCSWSCreatureForceMoveToPoint = 0x004EDBA0;

// Engine sentinel for "no object" in AI-queue object-id slots
// (objectId1/objectId2 of AddMoveToPointAction, the
// SetLockOrientationToObject release-call, and several siblings in the
// AIAction* family). Distinct from CGameObjectArray's removed-handle
// sentinel (0xFFFFFFFF) — different layer.
constexpr unsigned long kInvalidObjectId = 0x7F000000;
