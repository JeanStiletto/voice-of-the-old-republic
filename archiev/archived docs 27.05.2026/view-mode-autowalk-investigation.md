# View-mode autowalk — investigation snapshot (2026-05-07)

State of Phase 4 lay-off 5 ("Enter / Shift+Enter routing out of view mode"). Bundled checkpoint before PC restart — captures what landed, what's verified, and what's still failing so the next session can pick up cold.

## Goal recap

Lay-off 5 routes `Enter` and `Shift+Enter` while view mode is active so the user can act on the virtual cursor:

- **Enter on a hover target** → exit view mode, dispatch `acc::interact::DispatchInteract(hover_obj, hover_handle, false)`. Same engine-action-picker pipeline as outside-view-mode Enter (engine walks + invokes default action: open / talk / loot / pick up).
- **Shift+Enter on a hover target** → same call with `forceRadial=true`. Engine opens the radial action menu so the user picks the action; engine still walks once an action is chosen.
- **Enter or Shift+Enter on empty cursor** (no hover target inside the 1.0 m hover-pause radius) → exit view mode, speak "Walking to point" / "Gehe zum Punkt", dispatch `acc::guidance::WalkTo(cursor_pos)`. Raw walk to the cursor's world position, no action.
- View mode auto-exits before dispatch so the autowalk runs against an unfrozen character (decision (a) of the lay-off plan: clean lifecycle).

Outside view mode, `Enter` / `Shift+Enter` semantics unchanged (cycle/`LastTarget` resolution + force radial).

## What landed in the working tree

Modified files (uncommitted at the time of this snapshot):

- `patches/Accessibility/interact_hotkey.{h,cpp}` — `OnInteract` split into resolution shim + `DispatchInteractImpl`. New public `acc::interact::DispatchInteract(target, handle, forceRadial)` is a thin forwarder so view_mode can drive the same dispatch path. `PollHotkey` gates the rising-Enter branch on `acc::view_mode::IsActive() || ConsumedEnterThisTick()` to avoid double-dispatch when view_mode handed Enter off this tick.
- `patches/Accessibility/view_mode.{h,cpp}` — added `g_state.hover_pending_obj` (captured alongside the handle in `NarrateNearestObject`); new `g_enter_consumed_this_tick` flag with auto-clearing public getter `ConsumedEnterThisTick()`; new deferred-dispatch state `g_pending` and `ProcessPendingDispatch()` (runs at top of `Tick` before the active-gate); new `PollEnter()` invoked from `Tick`.
- `patches/Accessibility/strings.{h,_de.cpp,_en.cpp}` — added `Id::GuidingToPoint` ("Gehe zum Punkt" / "Walking to point") for the empty-cursor pre-roll.

## What works (verified in `patch-20260507-064544.log` and `patch-20260507-070644.log`)

- **Cursor movement and walkmesh collision** — `W/S` translate the cursor along camera yaw, walls clamp the cursor 5 cm short, collision logged at every wall hit (e.g. `ViewMode: collision at (16.78,30.06,-1.11) cursor clamped to (16.78,30.06,-1.11)`).
- **Hover-pause narration via Tolk speech** — user-confirmed: "speech narration was hearable is working." Log shows `ViewMode: hover narrate handle=0x0000012f cat=NPC name=[end_trask] dist=0.50` etc.
- **Double-dispatch fix** — view_mode owns `Enter` while active. Log shows the gate firing cleanly: `Interact: Enter rising — view mode owns this press, deferring to view_mode::Tick`. Prior bug (PollHotkey re-firing OnInteract right after PollEnter exited view mode) is gone.
- **Deferred dispatch lifecycle** — PollEnter exits view mode + re-enables player input synchronously, arms `g_pending`, and `ProcessPendingDispatch` fires next tick. Log: `dispatch armed for next tick (...)` followed ~47 ms later by `WalkTo cursor=(...) ok=1 elapsed=47ms (no hover target)`.
- **Cursor target ≠ player position** — confirmed via log: `dest=(18.82,30.06) from=(20.06,25.77) dist=4.47m`. The cursor is moving, the destination is a real different point on the walkmesh, the dispatch is asking for a real walk.

## What does NOT work

### 1. Empty-cursor `WalkTo` from view-mode-exit silently no-ops

`AddMoveToPointAction` returns `ret=0x00000001` (engine accepted) but the player never moves. Watchdog reports `moved=0.00m dist=4.47m (stuck)` at t+1s and `(still stuck)` at t+3s for every dispatch. FootstepSup's per-tick speed sampling shows no movement after view-mode entry — last `speed=[1-9]` line is *before* view mode entered, and `(20.003, 25.750)` stays pinned for the entire post-dispatch window.

This is true even after the deferred-dispatch fix that gives the engine ~47 ms of `enabled=1` settling time before the dispatch path's `enabled=0` transition.

### 2. Collision audio cues and Pillar 1 Trigger 1 cues inaudible in view mode

User report: "i hear a lot of objects, chests npcs but nothing while in umsehen mode. i didn't heared collision sounds as well." The log shows `ViewMode: collision` lines at every wall-cursor contact (so the cue *is* being requested via `acc::audio::PlayCue3D`), but the user never hears them. Same for Pillar 1 change-driven cues that should fire as the cursor's "soundscape" walks past objects.

Tolk speech (used by the hover narrate path) IS audible — confirms the `Tolk.Speak` channel is fine. Only 3D-spatialized engine audio is silent.

This points strongly at the listener-override hook (`OnSetListenerPosition` at engine address `0x005D5DF0`) misbehaving in some way — either the substituted listener position is invalid for the audio engine, the inner `CExoSoundInternal::SetListenerPosition` call is failing, or the override is putting the listener somewhere far enough from cue origin that 3D attenuation drops everything to zero (5 cm gap between cursor-clamp and wall should be inaudible only if the listener is somewhere else entirely).

### 3. "Trask interaction" in the prior session was *not* an autowalk

User clarified after the fact: "trask interaction was just dialog opening i think." Trask is adjacent at the Endar Spire spawn, so the picker dispatch (`action_id=0x3ea Dialoge`) opened dialog without actually walking. We do not have evidence that the picker → `HandleMouseClickInWorld` path *walks* the character from view-mode-exit any better than the raw `WalkTo` path does.

## Hypotheses tried and ruled out

### H1 — Double-dispatch (rejected, but fix kept)

Initial theory: `view_mode::PollEnter` exits view mode, then later in the same tick `interact_hotkey::PollHotkey` sees `IsActive()==false` and re-fires Enter via `OnInteract`, which dispatches a stale-`LastTarget` Dialog action that preempts our queued `WalkTo`. Verified in `patch-20260506-142103.log`: `ViewMode: Enter -> WalkTo` immediately followed by `Interact: Enter -> [Dialoge end_trask]`. Fix landed (`ConsumedEnterThisTick` flag + PollHotkey gate). Verified clean in subsequent sessions.

But removing the double-dispatch did NOT fix the empty-cursor WalkTo failure — the dispatch is the only one running now and the player still doesn't walk. So H1 was real, but it wasn't the root cause of the no-movement symptom.

### H2 — Same-tick `false → true → false` input-toggle round-trip clears the action queue (rejected)

Theory: `ExitViewModeQuiet` calling `SetPlayerInputEnabled(true)` while view-mode-state was `enabled=false` re-engages the per-tick movement clobber for an instant before the subsequent `SetPlayerInputEnabled(false)` from `WalkTo`, and the clobber stomps the queued AI action. Removed the round-trip — `ExitViewModeQuiet` no longer toggled input. Player still didn't move (`patch-20260507-064544.log`).

So either the round-trip wasn't the culprit, or there's a second mechanism we haven't identified.

### H3 — Engine needs a real frame between `enabled=1` settling and the `1→0` transition before AI dispatch (rejected)

Theory: AI walk dispatch needs a recent `1→0` transition to fire, and a synchronous `false → true → false` collapses too fast for the engine to ever see `enabled=1`. Implemented deferred dispatch: PollEnter exits view mode + re-enables input synchronously, arms `g_pending`, and `ProcessPendingDispatch` fires on the next OnUpdate tick after `kPendingDispatchMinElapsedMs` (16 ms minimum) elapses. Result: `WalkTo` now runs ~47 ms after the `SetEnabled(true)`, with a clean `enabled=1 → 0` transition immediately before the dispatch.

`patch-20260507-070644.log` shows the lifecycle firing exactly as designed. Player still doesn't move.

So H3 is also wrong — the issue isn't input-toggle timing. There's something else view_mode is doing to engine state.

### H4 — `cycle_input::OnPathfindFocus` Shift+- baseline behaviour (untested in current sessions)

The memory says cycle Shift+- works after the SetEnabled(false) toggle was added (`project_player_creature_ignores_ai_moves` resolved). But the user has not exercised Shift+- in this lay-off-5 testing run. We don't have direct evidence that raw `WalkTo` works AT ALL in this build, in this game session, on this save. The next session needs to start with a Shift+- baseline to disentangle "view mode breaks AI dispatch" from "AI dispatch is broken in this build".

## Other view-mode side effects to suspect

Things view_mode does between entry and the dispatch that are NOT present in cycle_input's working flow:

- **`OnSetListenerPosition` hook active** — substitutes `g_state.cursor_pos` for the engine's camera-derived listener vector on every per-frame `CExoSound::SetListenerPosition` write. Suspect because the audio is silent. Could it also affect AI dispatch indirectly? Long shot, but the listener vector is shared engine state and we don't fully understand what reads it.
- **`AreaObjectIterator` walk in `NarrateNearestObject`** — iterates the area's `game_objects` list every tick to find the nearest in-radius object to the cursor. Should be read-only but our handle resolution path has burned us before (cf. `project_player_in_area_object_list`).
- **`SegmentCrossesWalkmesh` 2D edge intersection** — pure math, reads the cached `WallEdge` array. Should not touch live engine state.
- **`SetPlayerInputEnabled(false, armAutoRestore=false)` held continuously across many ticks** — the sustained-disable variant. The engine field `CSWPlayerControl.enabled` stays at 0 from view-mode entry until exit. This *should* be the intended state for AI dispatch (per the player-control toggle memory), but it's also the longest period of `enabled=0` we ever produce — cycle_input only flips `1→0` for a single dispatch and the auto-restore timer flips back after 3 s.

## 2026-05-07 session findings — diagnosis is wrong, "view-mode" was a red herring

Three back-to-back experiments completed in this session, all logged. The conclusion overturns the framing of this whole investigation.

### Experiment A — listener override gated off (`patch-20260507-082230.log`)

`audio_bus.cpp` `kSubstituteCursorForListener=false` forces the `OnSetListenerPosition` hook to passthrough engine values regardless of view-mode state. Log lines confirm `ListenerHook: override=0` throughout. Re-tested empty-cursor `WalkTo`:

- 10.31 m → moved=0.00 m, stuck
- 3.25 m → moved=0.00 m, stuck
- 0.38 m → moved=0.00 m, "(reached)" (false-positive — within 1 m tolerance, but no actual displacement)
- 0.38 m → moved=0.00 m, stuck

**Listener override is innocent.** Trigger-1 audio cues also still inaudible — same gate has no effect on the audio silence either.

### Experiment B — `CancelMovement` (queue clear) before `WalkTo` (`patch-20260507-083116.log`)

Tested whether view-mode's sustained `SetEnabled(false)` left a stale entry at the queue head that the engine pathfinder won't consume past. Added `acc::guidance::CancelMovement()` call (wraps `CSWSObject::ClearAllActions @0x004CCD80`) immediately before `WalkTo` in `ProcessPendingDispatch`. Log shows `Autowalk: CancelMovement dispatched (ClearAllActions(0))` cleanly preceding each `WalkTo dispatch`. Re-tested:

- 0.55 m → moved=0.00 m, "(reached)"
- 4.07 m → moved=0.00 m, still stuck
- 5.99 m → moved=0.00 m, still stuck

**Queue contention is innocent.** Clearing the action queue immediately before dispatch makes no difference.

### Experiment C — `ForceWalkTo` (queue-bypass) instead of `WalkTo` (`patch-20260507-083538.log`)

`acc::guidance::ForceWalkTo` calls `CSWSCreature::ForceMoveToPoint @0x004EDBA0` — a different engine entry point that *bypasses* the per-creature action queue entirely. If queue *processing* (not contention) was the issue, force-dispatch would walk. Re-tested:

- 0.56 m → moved=0.00 m, "(reached)"
- 4.94 m → moved=0.00 m, still stuck

**Queue-bypass also fails.** Both `AddMoveToPointAction` and `ForceMoveToPoint` silently no-op.

### Experiment D — Shift+- baseline test (`patch-20260507-084333.log`)

The killer test. The investigation framed everything as "view-mode breaks WalkTo," assuming Shift+- (cycle pathfind, raw `WalkTo` to a cycle target's position) still worked. H4 flagged that we hadn't actually exercised Shift+- in current testing. Ran the test cold, no view-mode involved, with player walking around manually first to set up live engine state:

- 0.72 m → moved=0.00 m, stuck
- 0.72 m → moved=0.00 m, stuck
- 40.06 m (Tür, verriegelt) → moved=0.00 m, still stuck
- 52.07 m → moved=0.00 m, still stuck
- 16.98 m → moved=0.00 m, still stuck
- 6.62 m → moved=0.00 m, stuck (twice)

**7 attempts, every single one stuck, none view-mode-related.** Then on the SAME save, SAME build, SAME player position, the user pressed Enter on the same locked door (`obj=0FCF28B0 handle=0x00000046`):

```
Picker: descriptor populated target=0x80000046 action_id=0x3f2 label=[Öffnen] icon=[i_opendoor] count=1
Picker: HandleMouseClickInWorld dispatched target=0x80000046 action_id=0x3f2 label=[Öffnen]
```

Player position changed from `(23.93, 18.77)` to `(34.58, 18.44)` over the next ~10 seconds — engine walked the player ~10 m to the door via the picker pipeline. Same target, same player creature, same engine state.

### What this means

The bug is **not view-mode**. The bug is that `AddMoveToPointAction` (queue path) and `ForceMoveToPoint` (queue-bypass) — both raw-coordinate engine surfaces operating directly on the player creature — silently no-op. View-mode just happens to be the only place we use `AddMoveToPointAction` for a feature, so it surfaced first; cycle Shift+- has the same break but had been assumed working from older sessions and not re-verified.

The picker pipeline (`CClientExoAppInternal::HandleMouseClickInWorld` after `GetDefaultActions` populates `+0x4c8`) **does** walk the player. That's a higher-level engine API that takes a target *handle*, resolves the destination from the object's position, and dispatches against the player creature through a different code path.

Memory `project_player_creature_ignores_ai_moves` was marked RESOLVED with the SetPlayerInputEnabled toggle fix. That conclusion has regressed — the toggle is still happening per the log (`SetEnabled(false, armAutoRestore=1)` immediately before each `AddMoveToPointAction`) but the action no longer takes effect.

### Experiments E–H — Ghidra-driven RE pass (2026-05-07 session continued)

After Experiment D proved the bug isn't view-mode-specific, switched from blind probes to direct decompilation of the engine functions. Each step ruled out an arg-level / state-level fix and narrowed to the architectural cause.

**Experiment E — `runFlag=0` → `runFlag=1`** (`patch-20260507-090932.log`).
Hypothesis: the engine started rejecting walk-mode (`runFlag=0`) actions specifically. 6 Shift+- attempts (0.38 m to 44.32 m), all `moved=0.00m stuck`. Innocent — arg value isn't the issue.

**Experiment F — set `field390_0x9f0 |= 2` on the player creature** (`patch-20260507-093617.log`).
Decompile of `CSWSCreature::AIActionMoveToPoint @0x0051f4f0` showed the function early-returns 3 (abort) at line 515 when:
1. The action's target object resolves to INVALID via `GetGameObject` (which our raw `WalkTo` always passes — `kInvalidObjectId` for both object refs), AND
2. `creature->field390_0x9f0 & 2 == 0`.

Set bit 1 of `field390_0x9f0` before dispatch. Log shows `prev_low=0xff` — bit was *already* set. Patch was a no-op. So the field386 gate isn't the block. 5 attempts, all stuck.

**Experiment G — diagnostic probe of `field427_0xa8c` and `field101_0x1f8`** (`patch-20260507-094234.log`).
The killer diagnostic. `field427_0xa8c` is `AddMoveToPointAction`'s "I just queued a move" marker (set to 2). `AIActionMoveToPoint`'s various exits leave it at distinct values: 1 (switch case), 0 (short-tail/long-branch end), -1 (long-branch reset). At t+1s after dispatch, log shows `field427=2` — **`AIActionMoveToPoint` literally never ran**. The action enters the queue, but the per-tick scheduler never picks it up.

**Experiment H — RE of `RunActions` callers + `SendPlayerToServerInput_*` enumeration**.
Decompile of `CServerAIMaster::UpdateState @0x004b0b70` (the per-tick AI scheduler) shows it iterates a list at `this->field1_0x4[3]` of object IDs. For each NPC creature with a queue head action_id == 1 OR 0x3d, it calls `WalkUpdateLocation_QuickWalk` directly (not even going through `AIActionMoveToPoint`). **The list is the AI master's NPC list — does not include the player creature.** So no scheduler iterates the player's queue.

Decompile of `CClientExoAppInternal::HandleMouseClickInWorld @0x00620350` shows the picker calls `descriptor.action_function(action_id, playerCharId)` — and `GetDefaultActions @0x00620620` populates `action_function` with **CLIENT-SIDE methods**:
- Dialog → `CSWCCreature::ActionInitiateDialog @0x0060f620`
- Door open/close → `CSWCDoor::ToggleDoorState @0x00683d90`
- Use placeable → `CSWCPlaceable::UsePlaceable @0x00682660`
- Disable mine → `CSWCTrigger::ActionMenuDisableMine`
- Attack → `CSWCCreature::ActionMenuAttack`

These all funnel into `CSWCMessage::SendPlayerToServerInput_*` — the **client-to-server message bus**. The server receives the message, enqueues server-side action AND triggers `RunActions` as part of message processing. Our raw `AddMoveToPointAction` writes directly to the server creature's queue, skipping the message bus entirely; the server's per-tick scheduler doesn't know to wake.

The complete `CSWCMessage::SendPlayerToServerInput_*` symbol set:
`CastSpell` `TogglePauseRequest` `GiveItem` `Attack` `UseSkill` `UseItem` `UseObject` `ChangeDoorState` `UnlockObject` `AbortDriveContro` `Dialog`.

**There is no `SendPlayerToServerInput_MoveToPoint`-equivalent.** The vanilla engine never exposed "walk to arbitrary point" as a player-input message — by design. The player only ever asks "do action Y on target Z," and the engine walks-to-target as a setup phase of executing the action. Vanilla left-click on empty walkmesh does nothing. Vanilla movement is W/A/S/D through the input loop, dispatched by `CSWPlayerControlCamRelative::Control @0x00679940`.

### Real diagnosis

`acc::guidance::WalkTo` is on a fundamentally wrong path for player creatures. `AddMoveToPointAction` and `ForceMoveToPoint` are **NPC-only primitives**:
- They're invoked from the AI scheduler's per-tick loop (which iterates only NPC IDs).
- They write to the per-creature action queue.
- The player creature isn't in that loop, so the queue head never gets popped.

Memory `project_player_creature_ignores_ai_moves` was a misdiagnosis. The original "RESOLVED via SetEnabled toggle" symptom was probably observation of the picker working (USE_OBJECT chain) and conflating it with raw `WalkTo` working. The toggle does what the memory says (gates per-tick movement clobber so AI moves *survive* once dispatched), but it doesn't get the action *dispatched* in the first place for the player. NPCs don't need the toggle because they have no input clobber.

The picker's `USE_OBJECT` chain works because it goes:
client `CSWCCreature::ActionInitiateDialog` (or sibling) → `CSWCMessage::SendPlayerToServerInput_*` → server message handler → server-side enqueue + immediate `RunActions` pump.

**The fix path is option 1 from the discussion: anchor walking on a synthetic engine object at the destination, drive it through the existing player-action message bus.** Spawning a runtime invisible object at the cursor position, then dispatching `SendPlayerToServerInput_UseObject` (or whichever message routes to `AIActionUseObject @0x0057e8c0`) gets the engine to walk the player to that object's position. Despawn after arrival.

This is invasive, but it's the only path that works *inside the engine's intended player-action model*. Synthesizing W/A/S/D keypresses (option 2) bypasses pathfinding and breaks at any obstacle. Anchoring on existing nearby objects (option 3) doesn't satisfy "click empty space → walk there."

## Implementation plan — synthetic-target walk (next session)

Goal: route view-mode-Enter on empty cursor (and Shift+-) through the engine's player-action message bus by spawning a temporary anchor object at the destination.

The plan is broken into independent lay-offs that are individually testable. Each lay-off is a single, narrow change with a clear in-game success criterion. Scratch any lay-off that proves the next one unnecessary; combine them only if testing requires it.

### Lay-off A — RE the runtime object-creation API

**Goal:** identify the engine call that script `CreateObject` ultimately invokes. We need the C++ entry point, signature, owning area, and lifecycle expectations (does the engine garbage-collect orphan objects, or do we need to despawn manually?).

**Approach:**
1. Decompile `ExecuteCommandCreateObject` in the NWScript VM bridge — should be at one of the `0x53*` addresses near other `ExecuteCommandActionMoveToPoint`-family functions.
2. Decompile its callee chain into the server-side spawner. Likely target: `CSWSArea::AddObject` / `CSWSModule::CreateWaypoint` / `CSWSPlaceable::Create` / similar.
3. Identify which engine class is cheapest to spawn. `CSWSWaypoint` (server waypoint) is the leading candidate — vanilla scripts spawn waypoints all the time, they have minimal side effects (no rendering, no inventory, no AI).
4. Document the call signature, required CSWSArea*, position Vector, and any required tag / template fields.

**Output:** a `docs/runtime-object-spawn-investigation.md` (new) with the API surface, the call sequence, and SEH notes.

**Success criterion:** can call the spawner from a hook handler, observe the new object via `AreaObjectIterator` on the next tick, log its handle.

### Lay-off B — `acc::guidance::SpawnWalkAnchor(area, pos)` + `DespawnWalkAnchor(handle)`

**Goal:** wrap lay-off A's findings in a clean `acc::guidance` API.

**Approach:**
- New file `patches/Accessibility/guidance_anchor.{h,cpp}`. Two functions: `SpawnWalkAnchor(area, position) -> handle` and `DespawnWalkAnchor(handle)`.
- SEH-wrap the engine calls following the established `acc::engine` convention (false return on chain-unresolved or fault).
- Tag-mark the created object so we can find leaked anchors after a crash for cleanup. Suggested tag: `acc_anchor_<actionId>`.
- The anchor has no visual / collision / interaction — it's just a coordinate the engine can resolve to a position.

**Success criterion:** `kdev` can drive a spawn → log `spawned anchor handle=0xXXXX at (x,y,z)` → drive a despawn → confirm next `AreaObjectIterator` doesn't see the handle.

### Lay-off C — `acc::guidance::WalkToPoint(destination)` rewrite

**Goal:** replace the broken `WalkTo` body with the message-bus path.

**Approach:**
- Rename the existing `acc::guidance::WalkTo` to `acc::guidance::WalkToObject(handle)` (it was never a "walk to coordinates" primitive in practice; it just called the broken `AddMoveToPointAction`). This becomes a thin wrapper around the existing `acc::picker::Drive` / `SendPlayerToServerInput_UseObject`.
- New `acc::guidance::WalkToPoint(destination)`: spawn anchor at destination → grab handle → `WalkToObject(handle)` → arm a watchdog that despawns the anchor on arrival (within 1 m) or after a timeout (e.g. 30 s, matches engine's own pathfinding deadline).
- Anchor lifecycle is owned by `WalkToPoint`'s watchdog. View-mode and cycle-input callers are identical to today (`acc::guidance::WalkToPoint(cursor_pos)` / `WalkToObject(cycle_target_handle)`).

**Decisions to make in this lay-off:**
- What action_id to dispatch on the anchor. `0x28 USE_OBJECT` is the obvious candidate but might fire a "use" event the anchor doesn't support; safer alternatives are `MoveToObject` action (if exposed) or `ActionInteractObject` whose engine handler walks-to-target without doing anything on arrival for objects that don't implement use callbacks. RE call needed.
- Despawn timing: cancel the anchor on `CancelMovement` too (Shift+- second press). Otherwise we orphan an object every time the user changes their mind.

**Success criterion:** Shift+- on a real cycle target walks the player (regression of the original feature). Then view-mode-Enter on empty walkmesh walks the player to that point.

### Lay-off D — Migrate `view_mode::ProcessPendingDispatch` to `WalkToPoint`

**Goal:** delete the empty-cursor `WalkTo` branch in view_mode and replace with `WalkToPoint(cursor_pos)`.

**Approach:**
- Replace the call in `view_mode.cpp::ProcessPendingDispatch` (currently `acc::guidance::ForceWalkTo(cursor_pos)` per the active diagnostic build).
- Drop the listener-override gate, the field390 patch, and all the diagnostic probes accumulated this session. They're no longer load-bearing.
- Keep the `g_pending` deferred-dispatch lifecycle — there's no harm in waiting one tick before spawning the anchor, and it preserves the established "exit view mode → re-enable input → dispatch" sequence.

**Success criterion:** in-game test of the four view-mode cases the long-term plan documents — Enter on hover NPC (Dialog), Enter on hover door (Open), Enter on empty walkmesh (Walk to point), Shift+Enter on hover NPC (radial action menu).

### Lay-off E — Migrate `cycle_input::OnPathfindFocus` to `WalkToObject`

**Goal:** make Shift+- re-use the proven path.

**Approach:**
- Replace the `acc::guidance::WalkTo(focusedObj position)` call in `cycle_input.cpp::OnPathfindFocus` with `acc::guidance::WalkToObject(focusedObj_handle)`.
- The cycle target *is* a real engine object — no anchor needed for this path. Direct `WalkToObject` is the engine's own click-to-walk-to-target pipeline.
- Update `acc::guidance::IsAutowalkInFlight` / `CancelMovement` to track the new in-flight state across both `WalkToObject` and `WalkToPoint` callers.

**Success criterion:** Shift+- walks the player to the cycle target — the original Phase 2 feature working again.

### Lay-off F — Update memory

After lay-offs A–E land and test green:
- `project_player_creature_ignores_ai_moves` → mark RESOLVED again, with an updated body that captures the actual root cause (NPC vs player primitive split + message bus).
- `project_player_control_toggle` → narrow the "How to apply" to its actual scope (NPC-action survival across input clobber, not for getting actions dispatched).
- New memory: a "Player movement path" entry with the hierarchy `WalkToPoint → spawn anchor → WalkToObject → SendPlayerToServerInput_UseObject` + the pointer to the implementation.
- Remove `feedback_no_untested_commits` no longer being violated by speculative resolution claims (keep the feedback memory itself).

### Lay-off G — Diagnostic cleanup

After lay-offs A–E ship and the message-bus walk path is proven working in-game, remove the experiment-time scaffolding accumulated during the 2026-05-07 RE pass. Each item below was added as a single-shot diagnostic probe; none is load-bearing once the rewrite lands. Lay-offs C/D/E will naturally delete most of it as part of the rewrite — this lay-off is the explicit "verify nothing diagnostic survived" pass.

**`patches/Accessibility/audio_bus.cpp`:**
- The `kSubstituteCursorForListener` constexpr (currently `true`, restored to engine-original behavior). Innocent per Experiment A; the toggle no longer informs anything. Delete the constexpr and the gating clause in `OnSetListenerPosition`. Restore the unconditional `acc::view_mode::IsActive() && acc::view_mode::TryGetCursorPosition(chosen)` branch.
- The `// Diagnostic toggle introduced 2026-05-07. Tested innocent ...` comment block — delete with the constexpr.

**`patches/Accessibility/guidance_autowalk.cpp` (mostly handled by the lay-off C rewrite, but verify):**
- `runFlag=1` (changed from 0). The current value is harmless — engine's flag bit 0 — but the comment misleadingly suggests this matters. The lay-off C rewrite replaces `acc::guidance::WalkTo`'s body wholesale; verify the resulting `WalkToObject` / `WalkToPoint` doesn't carry the runFlag comment forward.
- The `kCSWSCreatureField427Offset = 0xa8c` and `kCSWSObjectField101Offset = 0x1f8` constants. Diagnostic probes — delete.
- The `preField427` / `preField101` reads at dispatch and the `Autowalk: pre-dispatch field427_0xa8c=N field101_0x1f8=0xXXXXXXXX; post-dispatch field427=N` log line. Delete.
- The `field427` / `field101` probe in `TickProgressWatchdog` at the t+1s checkpoint and the appended `field427=N field101=0xXX` in the moved/dist log line. Delete.
- The `field390_0x9f0 |= 2` patch (the prev_low / field390Patched local). Delete with the rest of the dispatch site.
- All long-form diagnostic comment blocks tagged "Diagnostic 2026-05-07:" or "2026-05-07 diagnostic:" — delete. The investigation doc carries the why.

**`patches/Accessibility/view_mode.cpp`:**
- Lay-off D rewrites `ProcessPendingDispatch`'s empty-cursor branch (`acc::guidance::ForceWalkTo(cursor_pos)` → `acc::guidance::WalkToPoint(cursor_pos)`). Verify the diagnostic-comment block referencing the queue-clear and ForceWalkTo experiments is gone.
- The `acc::guidance::CancelMovement()` call before WalkTo (was added for Experiment B). Delete — the new `WalkToPoint` lifecycle handles cancellation via its own anchor-tracking state.

**Verification step:** `git diff main -- patches/Accessibility/audio_bus.cpp patches/Accessibility/guidance_autowalk.cpp patches/Accessibility/view_mode.cpp` should show only the substantive lay-off A–E rewrite — no `field427`, `field101`, `field390`, `kSubstituteCursorForListener`, `runFlag=1`, `pre-dispatch ... post-dispatch`, or `CancelMovement before WalkTo` strings remaining.

**Success criterion:** clean grep — `grep -rn "field427\|field101\|field390\|prev_low\|2026-05-07 diagnostic" patches/Accessibility/` returns zero hits.

### Out of scope for this implementation

- **Audio silence (Trigger-1 cues inaudible in view mode).** Independent symptom, separate diagnostic track. Now that listener-override is proven innocent, the next angle is the engine's per-frame `UpdateSoundEngine` call site at `0x005f5626` and how it handles cursor-position-as-listener-vector vs camera position.
- **Path obstruction handling.** If the user clicks on the far side of a wall or into a different area, `WalkToObject` will dispatch and the engine's pathfinder will figure out the route. If the route fails (no walkmesh path), the picker's existing failure announce already covers it. We don't need new code for this.
- **Multi-area walking.** The anchor is in the current area only. If the cursor is across an area transition, the user clicks the transition trigger anyway (cycleable target), so the anchor case won't fire there.

## Untested at this checkpoint

- **Audio silence (Trigger-1)** — separate track, parked.
- Whether re-loading the save before testing changes anything (engine state corruption from a prior run is now a *less* plausible cause given the RE pass).
- Whether the message-bus path itself has regressed (i.e. would `SendPlayerToServerInput_UseObject` walk an NPC the user has cycled to). The picker tests this implicitly when descriptor_count > 0; we have evidence from `patch-20260507-084333.log` line 2914 that this currently works.
