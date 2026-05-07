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

## Next-session plan

Order:

1. **Shift+- baseline test in this build, this save.** If Shift+- walks the character normally, the bug is genuinely view-mode-specific. If it doesn't, the bug is broader and view-mode's chain just exposes it.
2. **Hover-target Enter in view mode against a non-adjacent NPC.** Tests whether the engine click pipeline (`HandleMouseClickInWorld` via picker) walks the character better than raw `WalkTo` does. If yes, lay-off 5's hover-target branch is salvageable as-is and only the empty-cursor `WalkTo` branch is broken; we can route the empty-cursor case through the picker too somehow.
3. **Strip the listener-override hook temporarily** (gate `OnSetListenerPosition` to passthrough regardless of view-mode state) and re-test the WalkTo dispatch. If it walks now, the hook is the culprit and we need to investigate what the substitution is doing wrong. If it still doesn't walk, the hook is innocent and the bug is elsewhere.
4. **Strip the cursor-iteration `NarrateNearestObject` work** as a second isolation test if step 3 doesn't fix it. Reduces view_mode to the bare lifecycle (cursor position + collision + hover narration disabled).
5. **Investigate the audio listener silence as a separate concern.** Even if the autowalk fix lands, the audio inaudibility needs its own diagnosis — likely in `OnSetListenerPosition` or the inner `CExoSoundInternal::SetListenerPosition` call.

## Untested at this checkpoint

- Hover-target Enter via `DispatchInteract` against a target that requires an actual walk (not just adjacent dialog).
- Shift+- in this session (see H4).
- Whether the audio silence happens with the cursor position deliberately set far from walls / objects (i.e. is it a 3D-attenuation issue or something more structural).
- Whether re-loading the save before testing changes anything (engine state corruption from a prior run).
