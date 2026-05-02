# Tab Crash Investigation

Tracking the persistent crash that fires when keyboard-Tab cycles between tabs in the Options menu (and likely all Options-style panels with a button cluster + listbox layout).

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

## Current state

Latest commit: `229396a` (cursor warp + deferred setActive + PlayGuiSound diagnostic hook).

Working hypotheses still standing:
- The crash is in pure engine code, in a code path we have no hooks on (no log activity between listbox refresh and crash).
- The faulting EIP is exactly at the GuiManager struct address + 5 ⇒ some code is treating the manager pointer as a function pointer.
- The setActive call we make goes through a DIFFERENT path than the natural mouse-click activation; the engine ends up missing some pre/post-activation work, leaving a stale function pointer or vtable entry that gets called next frame.

What works:
- Tab DOWN press is captured cleanly. Our handler queues, returns consumed.
- OnUpdate fires next frame, runs cursor warp + deferred setActive cleanly.
- SetActiveControl fires for the new tab, listbox refreshes for the new tab's content.
- Then ~0.5s of silence (no log events), then crash.

What's still broken:
- Crash on the first Tab cycle. Same crash address pattern (`mgr+5`) every time.

Code state:
- `patches/Accessibility/Accessibility.cpp`: Tab handler defers via `g_pendingTabPanel` / `g_pendingTabTarget`, also queues cursor warp via `g_pendingCursorMove`/`g_pendingX`/`g_pendingY`. OnUpdate runs warp first, then deferred setActive. Manager-pointer guard kept.
- `patches/Accessibility/hooks.toml`: `OnPlayGuiSound` diagnostic hook installed.

## What's uncertain

- WHICH function in the engine is doing the indirect call/jump that lands on the manager pointer. We know WHERE it lands (manager + 5) but not WHO calls it.
- WHETHER our setActive call is missing some setup that the natural mouse-click path does. Plausible but not proven.
- WHETHER the corrupted function pointer is in a vtable, a callback registration, or transient register state. The crash being NOT in a loaded module argues against vtable corruption (vtables would normally point inside DLLs/EXE, and an overwrite would still typically point inside a heap-allocated structure that itself contains some valid-looking dispatch). Heap-pointing function pointer is more consistent with a fresh allocation that wasn't initialized properly.
- WHY the manager pointer specifically. Is the manager being mistaken for some other object type (e.g., a `CSWGuiPanel*` slot getting the manager value)? Or is some piece of state being read AS a pointer when it actually holds the manager value?
- WHY the 0.5s delay before crash. Indicates the engine does some non-trivial work in the post-activation path before hitting the bad call. Could be one frame of rendering + one frame of input poll + one frame of ... ?
- WHETHER the music stutter is the engine's main loop stalling, or audio thread issue, or rapid sub-tick activity. We've ruled out PlayGuiSound loops.

## Things we've ruled out (don't re-investigate)

- Win32 message `WM_LBUTTONUP` is not the source of `val=514` (verified via BufferEvent hook).
- Engine framework's selective-POPAD/ESP bug is fixed in our local fork — not the cause.
- Tight loop of `PlayGuiSound` calls — only 6 in the whole session, none near the crash.
- "App→gui_manager pointer is repointed across state transitions" theory — the `this=<different mgr>` events were caused by synchronous nesting from our handler, not by engine state-mgr swapping. With deferred setActive those events stop appearing.
- Stack corruption from synchronous setActive returning into a corrupted frame — ruled out by clean end-of-function log values after the defer refactor.
- "Big jump in coordinates causes crash" — every crash so far has been on the FIRST cycle (Gameplay → Feedback, 45-pixel jump). Bottom-tab (Sound) reachability is a separate historical issue.

## Next step

**Investigate the indirect-call lead in the SARIF / Ghidra DB.**

Goal: identify code paths in the engine that perform `CALL <register>` or `CALL [memory]` near the post-tab-activation code path, and could plausibly end up with the manager pointer in the call target.

Specific things to look for:
- Indirect calls inside `CSWGuiPanel::SetActiveControl` (0x40a630) and its callees. The function is small (77 bytes, only calls `PlayGuiSound`) but its early-return branches mean some post-activation work is skipped. What's in the fall-through path?
- Indirect calls in the engine's per-frame `Update` / `Draw` / mouseover-reconciliation paths that run AFTER our deferred setActive completes. The crash happens during these, not during setActive itself.
- Any vtable-style call (`CALL [reg+offset]`) where `reg` could plausibly hold the manager pointer at the call site. Particularly suspect: anywhere a `CSWGuiPanel*` is dereferenced for a vtable call but the slot might have been set to the manager pointer by mistake.
- Check `CSWGuiManager`'s vtable — is there a slot at offset +0 that some panel-like dispatch could call? The manager has fields, not a vtable, so calling `(*manager->vtable[N])` would read the first 4 bytes of `mouse_x` as a vtable pointer, then call into garbage. EIP at manager + 5 is consistent with the engine doing `CALL <mgr>` directly (treating the manager pointer as a function pointer).
- Search for any data store where a function pointer slot might be initialized FROM the manager pointer by mistake — e.g., wrongly-typed assignments in cascade hooks, or callback registration calls that take the manager pointer.

How to investigate (no code changes):
- Use `Decompile.java` on candidate functions (engine update/draw cycle, panel update, listbox update for the activated tab).
- Search SARIF for the pattern `CALL EAX` / `CALL ECX` / `JMP EAX` etc. inside the relevant address ranges.
- Cross-reference with the address database (`addresses.db`) for any global function-pointer slot that might be set during activation.

If the SARIF lead doesn't pan out within ~1 session of investigation, fall back to crash-dump analysis (next section).

## Future / fallback options (not pursuing now)

- **Crash dump analysis (option B from the discussion).** Add a `kdev analyze-dump` subcommand that reads the `.dmp` files Windows is already capturing in `%LOCALAPPDATA%\CrashDumps\`, extracts the call stack at the crash, resolves return addresses against Lane's Ghidra database. Would give us the WHO — which function did the bad call — definitively. Estimated ~2-3h of dotnet dev work using `Microsoft.Diagnostics.Runtime`.
- **Mouse-click simulation.** Instead of `SetActiveControl`, simulate a real mouse click by calling `CSWGuiButton::HandleLMouseDown` and `HandleLMouseUp` directly. More faithful to the engine's expected activation path; would do whatever pre/post-activation work the click flow does that our shortcut skips.
- **Ship without keyboard tab cycling.** Defeat-for-now option. Mouse-driven Options would still work; users would lose keyboard access to tabs but the rest of the patch is unaffected. Revisit later when we have better tools or insight.
