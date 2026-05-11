# Hook vs. Poll Audit

Half-day classification pass over every active hook in
`patches/Accessibility/hooks.toml` and every `Tick*` / `Poll*` call in
`patches/Accessibility/core_tick.cpp`. Applies the principle from
`feedback_hook_vs_poll_principle.md`:

- **Mutation** — the hook/poll *decides* or *alters* engine outcome
  (consume input, suppress audio, override value, dispatch action,
  modify pause/sound state). Hooks are the right tool when there is a
  clean event we can intercept.
- **Observation** — the hook/poll *surfaces* engine state (announce
  changes, narrate state, edge-detect transitions). Polling is the
  default; hooks earn their seat only when there is a unique event
  with no alternate code paths.
- **Diagnostic** — the hook/poll writes log lines only, no speech, no
  engine mutation. Diagnostic shapes should die once their
  investigation is closed.

Read-only audit. No source changes this session.

## 1. Inventory

Each entry shows: layer, role, one-line "what it does", and verdict
(keep / re-evaluate / propose change).

### Hooks (active in hooks.toml)

- **OnRulesInit** @ `0x00552c9a`
  - Layer: engine bootstrap
  - Role: diagnostic (one-shot)
  - What: first-fire log + `EnsureTolkInitialized()` + dump-bytes
    probes during early RE.
  - Verdict: keep — trivial cost, gated by `static bool fired`.
    Doubles as TLS-load anchor.

- **OnHandleFocusChange** (`CSWGuiControl::HandleFocusChange`) @ `0x0041896b`
  - Layer: menus
  - Role: diagnostic (log-only)
  - What: writes `Menus.FocusChange #N this=… p1=… tip=…` per
    focus-change event. Source comment: "Demoted to log-only … the
    panel-level SetActiveControl hook above is the real announcement
    signal".
  - Verdict: **re-evaluate** (proposed: remove).

- **OnSetActiveControl** (`CSWGuiPanel::SetActiveControl`) @ `0x0040a638`
  - Layer: menus
  - Role: observation (event-driven)
  - What: speak the focused control's text on every panel-level
    focus change. Earns its seat because the engine fires this
    exactly once per actual focus change (no alternate path).
  - Verdict: keep.

- **OnHandleInputEvent** (`CSWGuiManager::HandleInputEvent`) @ `0x0040c907`
  - Layer: menus / engine input
  - Role: mutation (selective consume, `consumed_exit_address=0x0040cbcb`)
  - What: drives chain navigation, consume arrows so the engine
    never sees them, run handler chains for Enter / Esc / Tab.
  - Verdict: keep — the single most load-bearing hook in the mod.

- **OnUpdate** (`CSWGuiManager::Update`) @ `0x0040ce76`
  - Layer: engine
  - Role: infrastructure (per-frame entry)
  - What: one call to `acc::tick::Dispatch()`. Hosts every poll.
  - Verdict: keep.

- **OnSetMoveToModuleString** (`CServerExoApp::SetMoveToModuleString`) @ `0x004aecd0`
  - Layer: engine
  - Role: observation (event-driven)
  - What: capture destination module resref at start of area-
    transition pipeline so we can speak "Lade: …" before the load
    screen renders. No equivalent state to poll — the resref is
    consumed by `operator=` immediately after.
  - Verdict: keep.

- **OnListBoxSetActiveControl** (`CSWGuiListBox::SetActiveControl`) @ `0x0041c16b`
  - Layer: menus
  - Role: observation (event-driven)
  - What: announce listbox row focus. Listbox row navigation does
    not bubble to `CSWGuiPanel::SetActiveControl`, so there is no
    equivalent state to surface via polling.
  - Verdict: keep.

- **OnPlayFootstep** (`CSWCCreature::PlayFootstep`'s engine JZ) @ `0x0061a31a`
  - Layer: pillar 1 (audio cues)
  - Role: mutation (suppress player footstep audio when stuck)
  - What: replace engine's natural early-out JZ; combine engine's
    own `field6_0x20==0` check with our stuck-detection verdict.
    Returns 1 to consume → 0x0061a632 cascade.
  - Verdict: keep.

- **OnSetListenerPosition** (`CExoSound::SetListenerPosition`) @ `0x005d5df0`
  - Layer: pillar 4 (view mode)
  - Role: mutation (override listener position to cursor in view mode)
  - What: detour the camera-driven listener write; substitute view-
    mode cursor position when active. Only way to win the race
    against the engine's render-phase update.
  - Verdict: keep.

- **OnCalculatePitchVarianceFrequency** (`CExoSoundSourceInternal::CalculatePitchVarianceFrequency`) @ `0x005db3d0`
  - Layer: audio cue bus
  - Role: mutation (neutralise per-fire pitch jitter for our cues)
  - What: return `base_frequency` unchanged when calling thread is
    inside our `BeginScopedZero` window; let everything else jitter.
  - Verdict: keep.

- **OnSwitchToSWInGameGui** (`CGuiInGame::SwitchToSWInGameGui`) @ `0x0062cf2d`
  - Layer: in-game menu shell
  - Role: mutation (redrill cleanup — pop prior sub-screen before push)
  - What: on warm path (sub-screen already alive in `panels[]`),
    synchronously call `PrevSWInGameGui` so the new sub-screen
    lands on a clean stack.
  - Verdict: keep — the mutation is load-bearing. (Drill-arm was
    correctly moved out of this hook; see
    `feedback_hook_vs_poll_principle.md` failure-mode example.)

- **OnProcessInput** (`CClientExoAppInternal::ProcessInput`) @ `0x006227fb`
  - Layer: engine input
  - Role: diagnostic (frame marker)
  - What: writes `Diag.ProcInput frame=… seq=…` per frame. Used
    during the input-routing investigation
    (`docs/in-game-menu-input-investigation.md`) to distinguish
    "two events in one frame" from "two events across two frames".
  - Verdict: **re-evaluate** (proposed: remove).

- **OnClientHandleInputEvent** (`CClientExoAppInternal::HandleInputEvent`) @ `0x00621210`
  - Layer: engine input
  - Role: **hybrid** — diagnostic logging + arrow-key forwarding
    mutation for modal popups.
  - What: logs every client-route input event with seq + caller_eip.
    AND: when `modal_stack > 0` and key is `0xb6..0xb9` (direction),
    forwards the event to `CSWGuiManager::HandleInputEvent` directly
    so popup buttons can be keyboard-navigated. The fix is in the
    only DIAG handler, but it's not diagnostic — popups break
    without it.
  - Verdict: **re-evaluate** (proposed: split — keep the mutation,
    delete the diagnostic logging block).

- **OnSetSWGuiStatus** (`CGuiInGame::SetSWGuiStatus`) @ `0x0062aa00`
  - Layer: in-game menu shell
  - Role: diagnostic (caller-EIP capture)
  - What: writes `SubScreen.Status this=… new_status=… caller=…`
    per call. Source comment: "finds the caller that flips
    sw_gui_status from 3 → 1 and auto-closes pause within ~1s".
    That investigation is closed (`TickInputClassReassert` resolves
    the unpause issue directly).
  - Verdict: **re-evaluate** (proposed: remove).

- **OnHideSWInGameGui** (`CGuiInGame::HideSWInGameGui`) @ `0x0062cba0`
  - Layer: in-game menu shell
  - Role: diagnostic (caller-EIP capture)
  - What: same diagnostic shape as `OnSetSWGuiStatus`; identifies
    "WHICH engine path invokes HideSWInGameGui". Same investigation,
    same resolution.
  - Verdict: **re-evaluate** (proposed: remove).

### Hooks (disabled in hooks.toml — commented out)

- `OnPlay3DOneShotSound`, `OnListBoxLMouseDown`, `OnListBoxLMouseUp`,
  `OnListBoxHandleInput`, `OnListBoxSetSelectedControl` — all commented
  with rationale in `hooks.toml`. No action needed; they document
  closed RE branches. Leave as-is.

### Per-tick polls (called from `core_tick::Dispatch`)

In `core_tick.cpp` source order:

- **menus::ValidatePanels**
  - Layer: menus
  - Role: housekeeping (mutation of *our* state, not engine state)
  - What: drop stale `g_tabbedPanel` if the engine freed it.
  - Verdict: keep.

- **menus::TickMonitors**
  - Layer: menus
  - Role: observation (multi-monitor fan-out — focused control
    text-change, panel contents fingerprint, dialog/container/equip
    listbox selection, editbox, give-mode `G` poll).
  - What: announce diffs.
  - Verdict: keep.

- **cycle_input::PollWin32**
  - Layer: pillar 4
  - Role: mutation (dispatches cycle commands to engine)
  - What: `,` / `.` / `-` cycle keys via `GetAsyncKeyState` — engine
    keymap drops the unbound scancodes before our manager hook sees
    them. Win32-poll is the only path.
  - Verdict: keep. (Hybrid w/ `OnHandleInputEvent` justified — they
    cover disjoint key sets.)

- **announce_degrees::PollWin32**
  - Layer: pillar 2
  - Role: observation + speak (AltGr → compass heading)
  - What: same Win32-poll rationale; AltGr unbound in stock
    `kotor.ini`.
  - Verdict: keep.

- **probe_mouselook::PollWin32 + TickSweep**
  - Layer: pillar 4 (probe)
  - Role: diagnostic (mouse-look behaviour probe — Shift+AltGr)
  - What: toggle engine `mouse_look` bit + synthetic mouse-sweep
    state machine. Source comment: "Diagnostic-only; rebinds or
    goes away when view-mode design is locked."
  - Verdict: **re-evaluate** (view-mode skeleton has landed; check
    whether the design is locked enough to drop the probe).

- **view_mode::PollWin32**
  - Layer: pillar 4
  - Role: mutation (toggle view mode on B; probe on Shift+B)
  - What: keyboard entry point for the view-mode subsystem.
  - Verdict: keep.

- **guidance::TickProgressWatchdog**
  - Layer: pillar 2 / pillar 4
  - Role: observation (autowalk in-flight arrival check + watchdog
    log)
  - What: no-op when no recent `WalkTo` dispatch; t+1s + t+3s log
    lines per dispatch.
  - Verdict: keep.

- **engine::TickPlayerInputRestore**
  - Layer: engine
  - Role: mutation (timed re-enable of player input flag)
  - What: ~3s after a guidance/interact dispatch disabled it, restore.
    Idle when no disable session active.
  - Verdict: keep.

- **passive_narrate::Tick**
  - Layer: pillar 2
  - Role: observation (`LastTarget` change → speak + 3D cue)
  - What: per-tick read of engine `LastTarget`; announce on change.
  - Verdict: keep.

- **party_leader_announce::Tick**
  - Layer: pillar 2
  - Role: observation (Tab rising edge → speak controlled creature)
  - What: Win32-polled Tab edge; speaks leader name.
  - Verdict: keep.

- **turn_announce::Tick**
  - Layer: pillar 2
  - Role: observation (compass sector change + 5° hysteresis)
  - Verdict: keep.

- **transitions::Tick**
  - Layer: pillar 2
  - Role: observation (area + room change announcement)
  - Verdict: keep.

- **camera_announce::Tick**
  - Layer: pillar 2 / pillar 4 infrastructure
  - Role: observation + state (dead-reckon camera yaw, anchor on
    character-yaw change). Order-load-bearing — feeds
    `spatial::change_detector` and `view_mode`.
  - Verdict: keep.

- **spatial::change_detector::Tick**
  - Layer: pillar 1 / pillar 4 infrastructure
  - Role: observation (per-area wall cache + change-detect)
  - Verdict: keep.

- **view_mode::Tick**
  - Layer: pillar 4
  - Role: mutation (when active — virtual-cursor stepping, Enter
    dispatch). Idle otherwise.
  - Verdict: keep.

- **audio::footstep_suppress::Tick**
  - Layer: pillar 1
  - Role: observation (seeds `g_was_stuck` for the `OnPlayFootstep`
    hook). State surface, not a mutation — the consuming side is
    the hook.
  - Verdict: keep.

- **diag::engine_select::Tick**
  - Layer: pillar 2 (diagnostic)
  - Role: diagnostic (Q/E/Tab key + LastTarget log)
  - What: speech was moved to `party_leader_announce` (2026-05-11);
    file remains "as pure diagnostic logging for the Q/E/Tab
    investigation context." Source comment: "Removable in one
    commit once decided."
  - Verdict: **re-evaluate** (proposed: remove).

- **radial_menu::Tick**
  - Layer: pillar 4
  - Role: observation (auto-disarm probe — confirm radial still has
    populated rows)
  - Verdict: keep.

- **combat::TickCombatMode**
  - Layer: combat
  - Role: observation (combat-mode entry/exit announce)
  - Verdict: keep.

- **combat::TickCombatLog**
  - Layer: combat
  - Role: observation (`InGameMessages` listbox diff → speak)
  - Verdict: keep.

- **combat::TickAttackResolutions**
  - Layer: combat
  - Role: observation (`attacks_list[7]` transition → speak)
  - Verdict: keep.

- **combat::TickSavingThrows**
  - Layer: combat
  - Role: skeleton no-op (waiting for `SavingThrowRoll` hook)
  - What: literally `return` from the body — never speaks.
  - Verdict: **re-evaluate** (skeleton no-op call has no value
    without the dependency hook; either land the hook or remove
    the call site).

- **combat::query::TickLeaderChangeAutoAnnounce**
  - Layer: combat
  - Role: observation (party leader change → speak)
  - Verdict: keep.

- **combat::query::TickExaminePanel**
  - Layer: combat
  - Role: observation (Examine panel monitor for Shift+H)
  - Verdict: keep.

- **combat::queue::Tick**
  - Layer: combat
  - Role: observation (action-queue submenu auto-disarm probe)
  - Verdict: keep.

- **dialog_speech::Tick**
  - Layer: pillar 2 / dialog
  - Role: observation (NPC line + replies count + BarkBubble diff)
  - Verdict: keep.

- **interact::PollHotkey**
  - Layer: pillar 2
  - Role: mutation (Enter / Shift+Enter — dispatches engine click
    pipeline)
  - Verdict: keep.

- **probe::world_hover::TickMonitor**
  - Layer: pillar 2 (probe)
  - Role: diagnostic (`LastTarget changed: …` log per tick)
  - What: source comment: "Probe stays in tree until 9a's narration
    loop is verified working … deletable in a single commit
    thereafter." `passive_narrate::Tick` reads the same
    `LastTarget` and is live in production.
  - Verdict: **re-evaluate** (proposed: remove).

- **probe::world_hover::PollHotkey** (Alt+P)
  - Layer: pillar 2 (probe)
  - Role: diagnostic (one-shot cursor-warp probe)
  - What: source comment: "Probe RESOLVED 2026-05-04 … Layer C
    dropped."
  - Verdict: **re-evaluate** (proposed: remove).

- **engine::TickInputClassReassert** (named for legacy reasons; now
  drives the unpause cleanup)
  - Layer: engine
  - Role: mutation (edge-triggered `SetPauseState(2,0)` +
    `SetSoundMode(0)` on modal-pop and on last-sub-screen-leave)
  - Verdict: keep.

- **menus::TickPendingOps**
  - Layer: menus
  - Role: mutation (drains deferred cursor-warp / click / activate /
    equip-select / equip-commit / slider / sub-screen-switch ops)
  - Verdict: keep.

## 2. Findings

Order: smallest cleanup first, biggest behavioural shift last.

### F1. `diag::engine_select::Tick` — remove

**Why misplaced.** Pure diagnostic poll. The speech functionality was
explicitly moved out to `party_leader_announce::Tick` on 2026-05-11
(per the file's own comment). The remaining body just logs Q/E/Tab
presses + `LastTarget` for a now-closed investigation context. The
hook-vs-poll principle says diagnostic-only shapes should die once
their investigation is closed — this one is.

**Proposed change.** Delete the TU (`diag_engine_select.cpp`,
`diag_engine_select.h`), the `#include`, and the `Tick()` call site
in `core_tick.cpp`.

**Risk.** Near-zero. No production code reads its log lines. The
party leader announce path is the actual user-facing feature.

**Reference.** `patches/Accessibility/diag_engine_select.cpp:138-143`
("Speech moved out to party_leader_announce (2026-05-11) — that TU
owns the user-facing leader announce…").

### F2. `probe::world_hover::TickMonitor` + `PollHotkey` — remove

**Why misplaced.** Both are diagnostic. `TickMonitor` logs
`LastTarget` changes; `PollHotkey` is the Alt+P cursor-warp probe.
The probe verdict was reached on 2026-05-04 ("Layer A viable; Layer C
dropped"), and `passive_narrate::Tick` now reads the same
`LastTarget` for the production narration loop. The source comment
already declares them "deletable in a single commit thereafter".

**Proposed change.** Delete `probe_world_hover.cpp/.h`, the
`#include`, and the two call sites in `core_tick.cpp`.

**Risk.** Near-zero. The Alt+P keybinding is freed (no current
non-probe consumer of `Alt+P`).

**Reference.** `patches/Accessibility/probe_world_hover.cpp:1-95`
plus header. `core_tick.cpp:202-206`.

### F3. `combat::TickSavingThrows` — remove (or land its hook)

**Why misplaced.** The body of `TickSavingThrows` is documented as a
skeleton no-op pending a `SavingThrowRoll` hook that does not yet
exist. Per-tick polling for save changes was deemed "too noisy to be
useful (every effect cleanup / equip / level-up touches the totals)."
Calling an empty body each frame is just dispatcher noise.

**Proposed change.** Either:
- Remove the call site (and the function) until the hook lands;
- Or land the hook first and turn the call into a real consumer.

The principle from the memory entry says: don't burn polls or hooks
on no-ops. The "skeleton call" pattern doesn't earn its seat.

**Risk.** Near-zero (it's a no-op).

**Reference.** `patches/Accessibility/combat.cpp:460-…` and
`combat.h:71-78`.

### F4. `OnHandleFocusChange` — remove

**Why misplaced.** Demoted to log-only by an earlier commit; the
panel-level `OnSetActiveControl` already covers the user-facing
announce path. `HandleFocusChange` fires twice per nav (loses + gains
focus). That's noise in the log, plus a mid-function detour cost on
every focus event for no behavioural benefit.

**Proposed change.** Remove the hook from `hooks.toml`. Remove
`OnHandleFocusChange` (and `ReadControlNameFields` if it has no other
caller — check) from `menus.cpp`.

**Risk.** Low. The hook is documented as superseded; removing it
matches the existing intent. Verify no other code reads the
`Menus.FocusChange` lines.

**Reference.** `patches/Accessibility/menus.cpp:1186-1199`
("Demoted to log-only. The panel-level SetActiveControl hook above
is the real announcement signal; HandleFocusChange fires twice per
navigation so speaking from here would echo.").

### F5. `OnSetSWGuiStatus` + `OnHideSWInGameGui` — remove

**Why misplaced.** Both hooks are explicitly diagnostic in
`hooks.toml`:

- `OnSetSWGuiStatus`: "diagnostic only, finds the caller that flips
  sw_gui_status from 3 → 1 and auto-closes pause within ~1s of
  opening."
- `OnHideSWInGameGui`: "diagnostic. SetSWGuiStatus hook revealed
  pause auto-close goes through HideSWInGameGui (caller EIP
  0x0062cbe2 = HideSWInGameGui+0x42)."

The pause-close investigation is closed: `TickInputClassReassert`
solves the user-facing problem (idempotent `SetPauseState(2,0)` +
`SetSoundMode(0)` on modal-pop / last-sub-screen-leave edges). The
hooks have served their investigative purpose and now just emit
high-frequency log spam every time the user opens / closes an in-game
sub-screen or popup.

**Proposed change.** Delete both `[[hooks]]` blocks from
`hooks.toml`. Delete `OnSetSWGuiStatus` and `OnHideSWInGameGui`
handlers from `engine_subscreen.cpp` plus their declarations in
`engine_subscreen.h`. Keep `g_switchHookEverFired` /
`OnSwitchToSWInGameGui` (those are real-deal mutation).

**Risk.** Low. The diagnostic findings are already baked into
existing code. If a future regression re-implicates the pause path,
re-enable the hooks then — pattern is documented for re-use.

**Reference.** `patches/Accessibility/hooks.toml:725-806`,
`engine_subscreen.cpp:257-308`.

### F6. `OnProcessInput` — remove

**Why misplaced.** Frame-marker diagnostic. Source comment: "Used as
a frame delimiter that lets the input log distinguish 'two events in
one frame' from 'two events across two frames' — the open question
for the held-Esc repeat-press hypothesis". That investigation is
in `docs/in-game-menu-input-investigation.md` and was the predecessor
to the now-shipped routing fixes in `OnHandleInputEvent` and
`OnClientHandleInputEvent`. The frame-marker writes a line every
frame (~30/sec when in-game) for zero current behavioural benefit.

**Proposed change.** Delete the `[[hooks]]` block from `hooks.toml`.
Delete `OnProcessInput` and `s_frame` from `diag_input_pipeline.cpp`.
Leave `NextSeq()` — `OnClientHandleInputEvent` + `OnHandleInputEvent`
still call it for cross-route correlation.

**Risk.** Low. Frame counters are easy to re-add if a future input-
routing question requires them.

**Reference.** `patches/Accessibility/hooks.toml:636-674`,
`diag_input_pipeline.cpp:30-45`.

### F7. `OnClientHandleInputEvent` — split mutation from diagnostic

**Why misplaced.** Hybrid that grew organically: started as a pure
diagnostic ("captures every event ProcessInput dispatches via the
upstream branch") then absorbed the load-bearing arrow-key-forward
mutation for modal popups (Bug-2a fix). The hook is essential, but
the diagnostic logging that wraps it is no longer needed once the
routing investigation closed.

**Proposed change.** Keep the hook + the `param_2 != 0 && param_1
in 0xb6..0xb9` modal-stack arrow-forward block. Delete the per-call
`Diag.ClientHIE seq=… caller=… key=…` log line. Reduce the handler
to: read args → check modal-stack arrow gate → forward or no-op.

**Risk.** Medium. The arrow-forward block reads `param_1` /
`param_2` via the LEA-vs-MOV dereferencing pattern, which has fault
paths the diagnostic log lines currently expose. Split carefully:
keep the SEH-guarded read; just stop logging the success case.

Alternative shape (worth considering): extract a tiny
`acc::engine::ForwardModalArrowKey(param_1, param_2)` helper into
`engine_input.cpp` so the diagnostic-input TU doesn't need to host
mutation at all. Either is acceptable; the helper is cleaner.

**Reference.** `patches/Accessibility/diag_input_pipeline.cpp:64-156`.

### F8. `probe_mouselook::PollWin32 + TickSweep` — re-evaluate

**Why on the list.** Source comment: "Diagnostic-only; rebinds or
goes away when view-mode design is locked." View mode shipped a
skeleton + a working camera/listener override path (`view_mode::Tick`,
`OnSetListenerPosition`), but the probe is still wired up consuming
Shift+AltGr.

**Proposed change.** Not a clear remove — the user has not yet
declared the view-mode design "locked", and a synthetic-sweep probe
remains useful for verifying audio-engine response at any future
regression point. Default recommendation: **keep for now, revisit
once view-mode lay-off 4 ships its production cue set.** Bookmark
for a future cleanup pass; do not remove this session.

**Risk.** N/A (no change proposed here).

**Reference.** `patches/Accessibility/core_tick.cpp:62-70` + the TU.

### F9. Cross-cutting — none of the *current* polls should be hooks

I checked every Tick/Poll against the principle's "hook for control
flow, poll for state observation" rule. None of them currently look
like misplaced polls — i.e. none would clearly benefit from being
converted to an engine hook. Specifically:

- `passive_narrate::Tick`, `transitions::Tick`,
  `turn_announce::Tick`, `camera_announce::Tick`,
  `radial_menu::Tick`, `combat::*Tick*`, `dialog_speech::Tick`,
  `audio::footstep_suppress::Tick` — all polling state fields the
  engine writes without broadcasting a single-event signal. Hooks
  would require finding multiple engine paths or risk missing
  them, which is exactly the "drill-arm bug" failure mode the
  principle warns against.

- `cycle_input::PollWin32`, `announce_degrees::PollWin32`,
  `party_leader_announce::Tick` (Win32) — bypass the engine's
  keymap which drops unbound scancodes. There is no hook-able
  event for these.

- `interact::PollHotkey`, `view_mode::PollWin32` — same Win32
  rationale; the engine keymap drops the unbound binding before
  any manager-level hook sees it.

- `engine::TickInputClassReassert` — edge-triggered on a state
  diff (`modal_stack` count + `HasActiveSubScreen`). Polling here
  is the *right* shape because the engine has *no* unique
  close-event we could hook (the bug it works around is exactly
  that the engine forgets to clear pause on certain close paths).

This aligns with the principle's expectation that "almost every
per-tick poll does observation" — the live shape isn't accidental.

## 3. Recommended order of execution

Small first, risky last. Each step is independently shippable.

1. **F1.** Delete `diag::engine_select` TU + call site.
2. **F2.** Delete `probe::world_hover` TU + two call sites.
3. **F3.** Delete `combat::TickSavingThrows` call site + (optionally)
   the function body. Smallest behaviour change.
4. **F4.** Delete `OnHandleFocusChange` hook + handler.
5. **F5.** Delete `OnSetSWGuiStatus` + `OnHideSWInGameGui` hooks +
   handlers. Two `[[hooks]]` blocks, two functions.
6. **F6.** Delete `OnProcessInput` hook + handler + `s_frame`
   counter (leave `NextSeq()`).
7. **F7.** Refactor `OnClientHandleInputEvent`: extract the modal-
   arrow-forward mutation into `engine_input` (or inline it),
   delete the diagnostic logging block. **Test:** modal popups
   (Alt+F4 quit-confirm, save-overwrite) — arrows still move focus
   between buttons.

After (1)–(7) ship, revisit F8 (`probe_mouselook`) when view-mode
lay-off 4 has its production cue set locked.

## 4. Estimated impact

Cleanup pass — no new features, no expected behaviour change beyond
log-noise reduction. Approximate per-step diff size:

- F1, F2: whole-TU deletes, ~150-200 lines each.
- F3: ~30 lines.
- F4: ~15 lines (hook block + handler body).
- F5: ~50 lines (two blocks + two handlers).
- F6: ~20 lines.
- F7: ~30 lines refactor.

Expected log volume reduction: very large. Frame-marker
(`OnProcessInput`), per-call `Diag.ClientHIE`, focus-change spam,
SetSWGuiStatus/Hide spam, and Q/E/Tab Diag spam together dominate
recent `patch-*.log` files in `logs/`. Removing them sharpens
signal-to-noise for future bug hunts without losing any current
production functionality.
