# input-handling prompt — round 1 record

Run 2026-05-28 against `patches/Accessibility/`. User opted to diverge from the prompt
(which is structured as "do a survey, ask, plan, big refactor") into a 6-item enumerated
cleanup of F1–F6 surfaced by the architecture survey (see `input-handling-survey.md`),
and to do the **internal refactorings first, bug fixes last**. Net: 3 of 6 items were
genuine, 3 dissolved on contact with the code (false positives from the architecture
survey subagent). 7 commits, ~−250 lines in menus.cpp + new menus_submenu module.

## Done

### F1 — OnHandleInputEvent dispatcher decomposition (5 commits)
The largest payoff. `menus.cpp::OnHandleInputEvent` was 1037 lines, mostly inline
chain-driven dispatch. Pulled four stages into `menus_chain`:

1. **`388f580` engine_panels: promote IsModalPopupPanel to public** — prerequisite
   (HandleEsc needs the predicate; it was file-static in menus.cpp).
2. **`004cde5` extract HandleEsc** — store override, workbench-upgrade override,
   generic sub-dialog/modal-popup close.
3. **`3a9904c` extract HandleLeftRight** — slider in/decrement, cycle-arrow flanker,
   portrait-panel override.
4. **`5d2436f` extract HandleNavStep** — Up/Down/Home/End chain step + chargen sync +
   cursor warp + per-row suffixes. Co-promoted `WalkChildren` from menus.cpp file-static
   to a `menus_chain` public helper (empty-chain probe shares the primitive with the
   panel-walk / listbox-walk diagnostic sites).
5. **`45bb22e` extract HandleEnterActivation** — per-target classification ladder
   (virtual mod-settings root, tab button, equip slot, workbench-upgrade slot, store
   item row, journal row, text-only, generic FireActivate) + the lazy-rebind for
   engine-pushed modals.

Net: `OnHandleInputEvent` 1037 → 528 lines; menus.cpp 2565 → ~1900 lines; menus_chain.cpp
847 → ~1420 lines (gained the four dispatch stages + WalkChildren). Behaviour preserved
verbatim with one trivial divergence: the mod-settings virtual-entry path now flows
through the dispatcher's trailing `Menus.Input` log line instead of short-circuiting via
`trackPress(1)` — same final return value and tracker state, one extra log line that
brings it into line with every other consumed path.

### F2 — pending-queue UI/input split (1 commit, mostly a false positive)
**`792efd9` menus_pending: drop dead QueuePrevSWInGameGui** — the wrapper for
`CallPrevSWInGameGui` had zero callers (the synchronous call site lives in
`engine_subscreen.cpp` after the wrapper LEA-ESP fix made the engine's vanilla
Esc-closes-sub-screen path work correctly).

Survey claim: "Drain mixes UI ops (ClickAt, MoveCursor) alongside input ops; some
callers queue UI when they should queue input. Subsystem-specific queues would
clarify." Reality:
- `QueueMoveCursor` has exactly one caller (`menus_chain::HandleNavStep` cursor warp).
- `QueueClickAt` has exactly one caller (tab-button activation).
- The survey's example ("radial Up/Down queue MoveCursor") doesn't exist in the code.
- Every op in the queue is **deferred engine-reentrancy-avoidance for input-driven
  dispatch** — they share the same purpose. Splitting into per-subsystem queues would
  duplicate the deferral machinery without ownership benefit.

### F3 — radial / target_action / actionbar state split (1 commit, weaker than claimed)
**`fb0d106` menus_submenu: centralise actionbar↔target_action mutex contract** — pulled
the symmetric `IsActive()`/`ForceDisarm` mutex check out of both Open() functions into
a shared `acc::menus::submenu::EnforceCombatHotkeyMutex(opening)` helper.

Survey claim: "radial state lives in radial_menu.cpp, but row-specific state lives in
target_action_menu.cpp; SubmenuState enum would clean up." Reality: the 5 submenus
(actionbar, target_action, radial, examine, combat_queue) each own substantial private
state besides the active bit, and only actionbar↔target_action share a mutex (the
others can co-exist). A SubmenuState enum would force per-submenu state migration for
no gain. The dyadic mutex helper is the right-sized fix.

## Not done — false positives

### F4 — Shift+Enter (force radial) vs Enter (default) don't share a gate
Already shared. `acc::interact_hotkey::OnInteract(bool forceRadial)` is the single
entry point; both `Action::InteractTarget` and `Action::InteractForceRadial` route
through it. The survey misread.

### F5 — chain-rebuild index drift on Options-sub-close
Two separate functions conflated:
- `RebindChainPreserveIndex` preserves the INDEX VALUE — used by exactly one caller
  (Store sell-row removal), where index-value preservation is correct (next row moves
  up into the same slot).
- `RebindChain` anchors `g_chainIndex` on `panel.activeControl` — falls back to 0 when
  not found. The "Options sub-close drops to first tab" symptom isn't a bug: the engine
  resets the strip's activeControl to the first child after sub-dialog dismiss; we
  faithfully follow it.

A "remember last tab per panel" feature would address the UX gripe but it's a new
feature, not a refactor. Not in scope.

### F6 — Esc-close routing scattered
The unpause + audio-resync IS centralised:
`acc::engine_subscreen::DispatchUnpauseCleanup` runs on the modal-pop edge from
`TickInputClassReassert` per-tick, automatically. What's "scattered" is the close-
primitive selection per panel type (FireActivate on Schliess, vs `store::CloseFromEsc`,
vs workbench `FindControlById(BTN_BACK)`, etc.) — but those are different mechanics that
don't share code. A `DoClosePanel(panel, reason)` helper would just be a switch
statement over what's already there.

## Survey-quality lesson

The architecture-survey subagent wrote ~3500 words of confident-sounding diagnoses for
F1–F6, but only 3 of 6 held up against grep + actual code reads. Common failure modes:
- F2: the agent fabricated an example ("radial Up/Down queues MoveCursor") that the
  code doesn't contain.
- F4: the agent missed an existing `bool forceRadial` param it would have seen with
  one grep.
- F5: the agent conflated `RebindChain` and `RebindChainPreserveIndex` as if they were
  the same function.
- F6: the agent didn't follow `DispatchUnpauseCleanup`'s call graph.

For future rounds: surveys are useful for the topology map but need a pruning pass
that verifies each finding against the actual code before pitching to the user. The
F1 finding (which I did verify before pitching) held up cleanly and produced 5 real
commits; the unverified ones cost time to investigate and dismiss.

## Notes for future rounds
- `menus_chain.cpp` is now ~1420 lines. The 4 new dispatch stages share state and
  helpers cleanly; future menu-side refactoring lands naturally here rather than
  growing menus.cpp.
- `menus_submenu.{h,cpp}` is a new tiny shared TU (~30 lines total) — natural home if
  a third submenu joins the mutex group later.
- `OnHandleInputEvent` still has ~150 lines of cross-cutting infrastructure
  (press-release pair tracking, synthesis-Esc passthrough, foreground panel resolution,
  Enter ClaimRisingEdge for InteractTarget). That's load-bearing and can't be extracted
  without losing readability.
