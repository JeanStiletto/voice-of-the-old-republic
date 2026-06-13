# Interaction dispatch model — how the engine walks-then-acts (decompile-verified 2026-06-13)

This is the ground-truth model of how KOTOR 1 turns "interact with object/NPC"
into "walk into range, then perform the action," recovered from Lane's gzf via
headless decompile. It **overturns** several older assumptions (see "Corrections"
at the bottom). Verified against `swkotor.exe` GoG bytes (== Steam at code addrs).

## The chain, end to end

A player interaction (talk / use / open / attack …) is **one verb against one
target object**. The engine never exposes "walk to an arbitrary point" as a
player verb — walking is always the *setup phase* of a verb. The flow:

1. **Client picks the verb.** `CClientExoAppInternal::GetDefaultActions @0x00620620`
   builds an action descriptor for the hovered/targeted object (talk on a
   creature → `action_id=0x3ea`, fn `CSWCCreature::ActionInitiateDialog`; open on
   a door → `0x3f2` / `ToggleDoorState`; etc.). Stored at `internal+0x4c8`.

2. **Client dispatch.** `CClientExoAppInternal::HandleMouseClickInWorld @0x00620350`
   has two branches: first click on a target → `PopulateMenus` (opens the radial);
   confirm (same target again) → call the descriptor's function pointer
   `(*(desc+0xc))(desc+8 /*action id*/, playerCharacter)`. Our picker forces the
   confirm branch by pre-setting `last_target == last_clicked_on_target == target`.

3. **The action function just sends a network message.** For talk,
   `CSWCCreature::ActionInitiateDialog @0x0060f620` does **no walking**: it
   `ClearAllActions` on the *target* NPC, orients the NPC toward the player,
   then `CSWCMessage::SendPlayerToServerInput_Dialog(npcId) @0x00677ea0` and
   `SetGlobalDialogState(1)`. It only succeeds standalone if already in range.

4. **Server input handler enqueues the walk-capable AI action.**
   `CSWSMessage::HandlePlayerToServerInputMessage @0x005254c0` switches on the
   input sub-code (the 3rd arg to `SendPlayerToServerMessage`, NOT the
   `CreateWriteMessage` size hint):
   - **case 8 = Dialog:** reads the NPC id, **`SetAILevel(player, 1)` if the
     player's `ai_level == 0`** (turns AI processing on for the player so the
     action runs!), then `AddAction(player, 0x18 /*AIActionDialogObject*/, npcId)`.
   - **case 0xb = UseObject:** reads the object id, `AddUseObjectAction(player, id)`.

5. **`CSWSObject::RunActions @0x0057f4a0`** (pumped by the message handler, not
   the NPC-only AI scheduler) is the action dispatcher. Action-code → handler map
   (partial): `0x1 MoveToPoint`, `0x2 CheckMoveToObject`, `0x6 PlayAnimation`,
   `0xa CheckMoveToPoint`, `0x11 CheckMoveToObjectRadius`, `0x13 ChangeFacingObject`,
   `0x14 OpenDoor`, `0x18 DialogObject`, `0x1e Wait`.

6. **The walk lives inside the verb's AI action.**
   - `CSWSObject::AIActionDialogObject @0x0057a470` (code 0x18): if the player is
     **> 10 m** from the NPC (`100.0 < distSq`), it `GetUseRange`s a destination
     near the NPC, `AddMoveToPointActionToFront(player, dest)`, and re-queues
     itself (0x18) to the front. Player walks in; on the next run it's in range →
     starts the conversation (party reposition, dialog camera, `ExoInput` active).
   - `CSWSObject::AIActionUseObject @0x0057e8c0` (use path): identical shape —
     `GetIsInUseRange`; if not, `AddMoveToPointActionToFront(player, useRangePt)`
     and re-queue, then perform the use on arrival.

**So the player IS walked by the engine's own AI action system** — the engine
even flips `SetAILevel(player,1)` to make it happen. Walk-then-talk and
walk-then-use are the same architecture; only the leaf verb differs.

## Why our distant-NPC talk freezes (the "Richter"/janicebug class)

Live repro: Manaan courtroom, judges 15–20 m away. Each Enter → ~4 s frozen
(`patch-20260612-194215.log`, five consecutive `TickPlayerInputRestore — stalled`
at 21:09:44–21:10:12). The player's server action queue reached depth 2 and
bounced 1↔2 without draining; the PC never moved; no dialog panel opened.

Our path *does* reach step 3 (it calls `ActionInitiateDialog`, which sends the
same case-8 message), so the engine *should* enqueue `AIActionDialogObject` and
walk us in. It doesn't. Open question (best settled empirically): which of our
own setup steps suppresses the native walk —
- `SetPlayerInputEnabled(false)` → `SwitchMode(player,0)` (sets client creature
  mode field `0x3a8`), applied *before* dispatch; and/or
- `ActionInitiateDialog`'s client preamble (`ClearAllActions` on NPC +
  `SetGlobalDialogState(1)`) racing the server-side AI enqueue; and/or
- the picker forcing the confirm branch from out of range vs. a real click that
  has already walked most of the way before the verb fires.

**Decisive test:** dispatch a distant talk via the native dialog input WITHOUT
`SetPlayerInputEnabled(false)` and observe whether the PC walks in and the
conversation opens (footsteps / position narration / dialog panel). If yes, the
input-disable was the saboteur and the fix is to stop disabling input for talk
(let the engine walk-then-talk natively). If no, drive the approach ourselves via
the proven `AddMoveToPointActionToFront`/`UseObject` path before the talk verb.

## Corrections to prior assumptions

- **"The player can't be AI-controlled / its action queue is never processed"** —
  FALSE as stated. The NPC AI *scheduler* (`CServerAIMaster::UpdateState`) skips
  the player, but the **message-bus pump (`RunActions`)** drains the player's
  queue, and case 8 explicitly `SetAILevel(player,1)`s to enable it. The player
  walks for both use and dialog verbs (container-open in the same session proves
  the walk live). See `project_player_creature_ignores_ai_moves` (its
  "walks-to-target as a setup phase" line was right).
- **"`AddMoveToPointAction` is permanently NPC-only / leader → FollowLeader
  no-op"** — too strong. `AIActionUseObject` and `AIActionDialogObject` both call
  `AddMoveToPointActionToFront` *on the player* and it walks. The leader no-op
  observed earlier was for a *standalone* `WalkTo` queue-write that nothing pumps,
  not for a move enqueued inside a message-bus-dispatched verb. See
  `project_addmovetopoint_leader_broken`.
- **`SetPlayerInputEnabled(false)` is not a "movement clobber gate" at the engine
  level** — it just calls `CSWCCreature::SwitchMode(player, 0/1)`, which writes
  one client-side mode flag (`field162_0x3a8`). Whether that flag interferes with
  the native dialog walk is the open question above.

## Walk the player to a bare COORDINATE (no object) — verified 2026-06-13

The player CAN be pathfound to an arbitrary walkmesh coordinate (around corners,
full nav-graph A*), with no target object and no recreated pathfinding. The
recipe is the engine's own click-to-move, recovered from
`CSWSMessage::HandlePlayerToServerInputWalkToWaypoint @0x005235b0` (the handler a
ground-click sends to). The two steps that matter:

1. **Prime the action subsystem first:** `CSWSCreature::ActionManager(creature,
   8) @0x004f8770` (mode 8 = move/walk). EVERY engine handler that queues a
   player action primes ActionManager first (dialog/use handlers call it too,
   mode 8 / 2). Without it the leader's queued move bails — `field427_0xa8c`
   stays 2 ("never ran"), no path.
2. **`AddMoveToPointAction` with `secondaryPoint = (0,0,0)`**, `actionId =
   0xffff`, `objectId2 = INVALID`. The secondary point's X is a line-of-sight
   shortcut distance in `AIActionMoveToPoint`: non-zero (e.g. passing the
   destination again) makes the engine take a straight LOS move that stops at
   the first wall; zero skips the shortcut so the nav-graph A* runs.

Both are required together. `secondary=0` WITHOUT `ActionManager(8)` priming
gives `field427=2` / no movement (this is the trap an earlier attempt fell into).
The NPC script command `ExecuteCommandMoveToPoint @0x0053fe00`
(`ActionMoveToLocation`) uses `secondary=(0,0,0)` too, but it pathfinds for NPCs
because their action manager is already in the right state; the player needs the
explicit `ActionManager(8)`.

Implemented in `acc::guidance::WalkTo` (guidance_autowalk.cpp): prime → dispatch.
Player input is left ENABLED (disabling it flips the client SwitchMode and
suppresses the walk). In-flight tracking clears on queue-drain OR player-stillness
(UseObject's composite queue never drains to 0, so distance/stillness is the
reliable "walk ended" signal). Shift+- (cycle_input `OnPathfindFocus`) routes
real objects through `UseObject` (engine walks to use-range + triggers) and bare
points / map pins through this `WalkTo` coordinate path.

**Net correction:** the leader is fully walkable to both objects AND arbitrary
coordinates using the engine's pathfinder. "Recreate the nav graph" was never
necessary. The only genuinely unreachable case is a destination with no walkable
route at all (e.g. an NPC sealed behind geometry).
