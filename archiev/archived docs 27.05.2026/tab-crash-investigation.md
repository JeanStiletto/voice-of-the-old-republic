# Tab Crash Investigation

**Status: CLOSED (reframed) — 2026-05-02.** Investigation halted. The crash is real but we were attacking the wrong problem: `CSWGuiPanel::SetActiveControl` is structurally the wrong activation primitive for the unified-cursor model's end goal. See [Conclusion](#conclusion-setactivecontrol-is-the-wrong-primitive) and [Next session](#what-this-means-for-the-next-session) below. The `Things we've ruled out` section is preserved as engine knowledge for the click-simulation work that replaces this.

Original framing (kept for historical context):

> Tracking the persistent crash that fires when keyboard-Tab cycles between tabs in the Options menu (and likely all Options-style panels with a button cluster + listbox layout).

## What we're trying to do

Make Tab cycle the focused tab button forward (Shift+Tab backward) in the Options menu so screen-reader users can reach Gameplay / Feedback / Auto-Pause / Grafik / Sound. Mouse navigation works; keyboard tab cycling crashes the engine.

## Smoking-gun finding

The crash address from the Windows Application Event Log lines up exactly with the GuiManager pointer + 5, across multiple runs with different heap allocations.

- Run at 04:54: `mgr=06CFEF00` armed, crash at `0x06D2EF05`. **Wait — different value!** Re-checked: 04:54 run armed mgr=06D2EF00, crash at `0x06D2EF05` ⇒ +5 ✓
- Run at 06:54: `mgr=06D6EF00` armed, crash at `0x06D6EF05` ⇒ +5 ✓
- Run at 07:05: `mgr=06D2EF00` armed, crash at `0x06D2EF05` ⇒ +5 ✓

Exception code: `0xC0000005` (ACCESS_VIOLATION).
Faulting module: **unknown** (i.e., not inside any loaded DLL/EXE — the faulting EIP is in heap memory).

This means the engine's CPU is executing instructions at the GuiManager struct address — some code is doing the equivalent of `CALL <register>` / `JMP <register>` with the register holding the manager pointer (which is a data pointer, not a function pointer). Execution lands inside the manager struct, decodes the first few bytes as instructions, then faults at offset +5.

Root cause is therefore most likely:
- A vtable entry overwritten with the manager pointer value, OR
- A callback function pointer overwritten with the manager pointer value, OR
- An indirect call/jump via a register that picked up the manager pointer from somewhere stale.

## What we've tried

Each attempt is a commit on `main`; the working tree always reflects the latest.

### Attempt 1 — Cursor-warp + listbox-overlap detection
Commits: `30c63ff`, `b4a60db`.

Approach: warp the cursor to the next tab's center; engine's hit-test/hover pipeline activates it. Detected when the listbox overlaps the tabs (Options-style panels) and warped one position past the target so the engine's "tab visually above cursor" fallback would land on the intended tab.

Result: crashes after the first cycle. Sound (bottom tab) was unreachable because there's no "tab visually below it" to use the fallback against.

### Attempt 2 — Direct setActive (synchronous, in handler)
Commit: `3ba1a65`.

Approach: drop cursor warp; call `CSWGuiPanel::SetActiveControl(panel, target_tab)` directly from the input handler — same pattern Enter-commit uses. Sound becomes reachable because there's no hit-test to defeat.

Result: crashes after the first cycle. A "synthetic" event with `key=LOGICAL_TAB val=514 this=<different mgr>` appeared in the log right before the crash, which we initially attributed to an engine cascade.

### Attempt 3 — Diagnostic: BufferEvent hook (mouse-only, removed)
Brief diagnostic; not a fix.

Hooked `CExoInputInternal::BufferEvent (0x5e12a0)` to see if the val=514 was a real `WM_LBUTTONUP` from Windows. Found that no `WM_LBUTTONUP` was queued during the scenario, AND the function only handles mouse messages (switch on WinMessages 0x200..0x20A) — keyboard events bypass it entirely.

Conclusion: val=514 is NOT a Win32 message. The match with `WM_LBUTTONUP` is numerical coincidence. Removed the hook (would spam every mouse move during real gameplay, and is useless for keyboard diagnostics anyway).

### Attempt 4 — Manager-pointer guard
Commit: `ba0221a`.

Approach: capture `*0x7A39F4` (the GuiManager singleton pointer) into `g_armedManager` when Tab mode is armed; in the Tab branch, only act when `thisPtr == g_armedManager`. Idea: skip events that arrive on a different manager (the supposed cascade event).

Result: crash persisted. The "cascade event" with the different `this` and `val=514` still showed up in the log AND still tripped `consumed=true`, despite the values clearly differing from `g_armedManager`. We diagnosed this as a contradiction in our source vs. the log behavior, hypothesizing stack corruption. (Both halves of that hypothesis turned out wrong — see Attempt 5.)

### Attempt 5 — Defer setActive to OnUpdate
Commit: `9fb428c`.

Approach: stop calling `setActive` from inside the input handler; record the target in `g_pendingTabPanel`/`g_pendingTabTarget` and have OnUpdate call `setActive` on the next per-frame tick. Same defer pattern arrows use.

Result:
- The "synthetic val=514 event" with the different `this` STOPPED appearing in the log entirely. So it WAS caused by synchronous nesting from inside our handler — Theory B (separate cascade event) was correct, Theory A (stack corruption) was wrong.
- Press's end-of-function log line now shows correct values (`this=<armed mgr>`, `val=128`, `CONSUMED`).
- Crash still happens, at the same point: after the listbox refresh for the new tab completes.

### Attempt 6 — Cursor warp + deferred setActive
Commit: `229396a`.

Approach: also queue a cursor warp to the new tab's center alongside the deferred setActive. OnUpdate runs the warp first, then setActive overrides whatever the warp's hit-test resolved to. Hypothesis: the engine's per-frame mouseover reconciles activeControl against cursor position; without the warp it crashes from the mismatch.

Result: crash persists at the same point. User observation: the crash is now slightly delayed — music stutters for ~0.5s before the game dies. The cursor warp ran cleanly with no extra `SetActiveControl` events fired by its hit-test.

### Attempt 7 — Diagnostic: PlayGuiSound hook (current state)
Hook is currently in place.

Hooked `CSWGuiManager::PlayGuiSound (0x40a140)` at function entry to count calls. Hypothesis: per-frame oscillation through some path that doesn't reach our `OnSetActiveControl` hook, which would hammer `PlayGuiSound` (the only `CALL` inside `SetActiveControl`).

Result: only 6 PlayGuiSound calls in the entire session, NONE between the deferred setActive (line 82) and the crash. No oscillation, no audio call loop. The 0.5s music stutter is NOT from a PlayGuiSound loop — more likely the engine's main loop is stalling (audio thread starves).

## Conclusion: SetActiveControl is the wrong primitive

Stepping back from the immediate crash, the strategy itself doesn't fit the project goal. The end goal isn't "Tab cycles tabs"; it's making the Options panel **navigable AND usable** — including the toggles and sliders inside the listbox blob. `CSWGuiPanel::SetActiveControl` cannot reach those:

- The Options listbox has `controls.size == 1` (one multi-line label blob). Individual settings ("Difficulty: Normal", "Combat Movement: Default", ...) are not separate `CSWGuiControl*`s — they are rendered from internal listbox state.
- `SetActiveControl(CSWGuiControl*)` requires a control pointer to target. There is nothing to point at for an individual setting.
- The engine reaches per-setting interactions via mouse clicks dispatched into the listbox, which resolves the click by y-coordinate, not by control pointer.

So even if the Tab crash were fixed, `SetActiveControl` would unlock tab buttons but no listbox-internal targets. The same engine surface that activates a setting is the one that should activate a tab — for consistency and to satisfy whatever pre/post-click invariants the engine maintains (which is what we've been tripping for seven attempts).

**The right primitive is synthesized mouse click at coordinate** (`CSWGuiButton::HandleLMouseDown` + `HandleLMouseUp` for tab buttons; a coordinate-based click into the listbox for individual settings; the same primitive sequenced with a move for slider drag). This serves Tab cycling, per-setting navigation, per-setting activation (toggles), and slider manipulation — the entire Options-panel goal — with one well-defined engine call sequence.

Why every attempt crashed at `mgr + 5`: the engine has post-activation invariants set up by the natural click flow (`HandleLMouseDown` → focus → `HandleLMouseUp` → activate → cleanup). `SetActiveControl` skips all of that. Some pointer the click-flow normally writes (vtable slot, callback, transient register state) ends up pointing at the manager. Next frame, the engine does `CALL <that>` and lands inside the manager struct. Switching primitive to the click flow means we don't skip the invariant in the first place.

## Things we've ruled out (preserved — informs the click-sim design)

These remain useful: any future activation primitive must avoid re-introducing them.

- Win32 message `WM_LBUTTONUP` is not the source of `val=514` (verified via BufferEvent hook).
- Engine framework's selective-POPAD/ESP bug is fixed in our local fork — not the cause.
- Tight loop of `PlayGuiSound` calls — only 6 in the whole session, none near the crash.
- "App→gui_manager pointer is repointed across state transitions" theory — the `this=<different mgr>` events were caused by synchronous nesting from our handler, not by engine state-mgr swapping. With deferred setActive those events stop appearing.
- Stack corruption from synchronous setActive returning into a corrupted frame — ruled out by clean end-of-function log values after the defer refactor.
- "Big jump in coordinates causes crash" — every crash so far has been on the FIRST cycle (Gameplay → Feedback, 45-pixel jump). Bottom-tab (Sound) reachability is a separate historical issue.
- Per-frame oscillation through `CSWGuiPanel::SetActiveControl` — `PlayGuiSound` (the only `CALL` inside) fires only 6 times per session, not in a loop near the crash.
- Any flavor of deferred-vs-synchronous `SetActiveControl` (sync, deferred-via-OnUpdate, deferred + manager guard, deferred + cursor warp + manager guard) — all crash identically. The defer pattern is fine; the primitive is wrong.

## Findings from `kdev analyze-dump` (2026-05-02)

Built `kdev analyze-dump` against `Microsoft.Diagnostics.Runtime` and ran it
on the captured Tab-crash dumps in `%LOCALAPPDATA%\CrashDumps`. Two of the
ten dumps are clean Tab-path crashes (`24928.dmp`, `27200.dmp`); they show
identical signatures:

- **Exception:** `0xC0000005` ACCESS_VIOLATION write to `0x00000000`.
- **EIP at fault:** `mgr+5` (heap; manager pointer changes per-run, but the
  +5 offset is stable).
- **EBP at fault = manager pointer.** The bad indirect call corrupts EBP to
  the manager pointer alongside EIP. That's why the EBP chain dies one frame
  in. New constraint for the click-sim primitive.
- **ESP=0x001afd14** (consistent across runs).
- **ESP scan resolves identical return addresses across both dumps:**
  `MainLoop+0xdf0`, `FrameHandler_007273a8` (a VC++ SEH entry table), and
  CRT cleanup unwinders `inline_unlock_8+0x7` / `unlock_8+0x5`.
- **Top-of-stack hex starts `02 02 00 00`** — the engine-internal event
  code 514 sitting as a function arg at fault time. Same value the manager
  logs as `param_2` on press edges (per `project_engine_event_value_layers.md`,
  NOT `WM_LBUTTONUP` — engine-internal, not a Win32 message). Confirms the
  engine was mid-event-dispatch when the indirect call landed on mgr+0.

Implication for Phase 3: the engine maintains a state through the natural
`HandleLMouseDown → HandleLMouseUp → activate → cleanup` flow. `SetActive-
Control` skips the prelude that writes the eventually-called pointer; that
pointer ends up zero or stale, gets called, lands on the manager struct
(data, not code), simultaneously corrupting EBP. Click-sim must enter the
flow at `HandleLMouseDown` so the prelude runs.

## What this means for the next session

The Tab handler in `Accessibility.cpp` and the visual-tab-order / manager-guard / pending-tab-target machinery should come out. The `OnPlayGuiSound` diagnostic hook in `hooks.toml` should also come out. The replacement is the click-simulation primitive described in `docs/menu-nav-design.md` Phase 3.

Implementation order:

1. ✅ **Crash-dump analysis subcommand (`kdev analyze-dump`).** Built; findings above. Reusable for any future crash.

2. ✅ **Strip the SetActiveControl-based Tab path.** Done in `e638aab`. Tab branch + visual-tab state + manager-guard + pending-tab-target + `OnPlayGuiSound` diagnostic all gone. `DetectTabsCluster` + `g_tabbedPanel`/`Start`/`Count` retained — they arm the listbox virtual-line cursor today and will feed the click-sim primitive in Phase 3.

3. **Build the click-simulation primitive.** Mid-function hooks on `CSWGuiButton::HandleLMouseDown` / `HandleLMouseUp` to verify offsets and arg shapes, then call them directly via `__thiscall` function pointers (same pattern as `MoveMouseToPosition`). Deferred via `OnUpdate` — same reentrancy reasoning as cursor warps. See `docs/menu-nav-design.md` Phase 3 for the full plan.

4. **Validate on Tab cycling first.** Tab buttons are engine-known `CSWGuiControl*`s with well-defined extents — simplest case for the new primitive. If this works, it confirms the primitive is sound.

5. **Apply to listbox per-setting activation.** Compute line y-coordinate from listbox extent + line index, dispatch click at that coordinate. Per-setting *navigation* (read-only line walk) already works; this adds *activation*.

6. **Sliders** as a follow-up: same primitive sequenced with a cursor move (down at handle, move to target, up). Defer until tabs + toggles are stable.

The work after step 1 is no longer Tab-specific — it's the activation half of the unified-cursor model. The Tab crash falls out as a side effect once the engine sees a proper click sequence.
