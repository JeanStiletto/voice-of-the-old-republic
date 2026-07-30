# Phase 3 scan — menus core (dispatch, focus, monitors, pending)

Scope: `menus.cpp` (959), `menus.h` (49), `menus_internal.cpp` (446),
`menus_internal.h` (210), `menus_dispatch.cpp` (618), `menus_focus.cpp` (442),
`menus_focus.h` (34), `menus_monitors.cpp` (950), `menus_monitors.h` (59),
`menus_pending.cpp` (801), `menus_pending.h` (135), `menus_speak.cpp` (26),
`menus_speak.h` (17). All in `C:\Users\fabia\Dev\kotor\patches\Accessibility\`.

Method: full read of every file in the batch (all 13). For every candidate
"is this dead?" question I grepped the actual namespace-qualified symbol
(e.g. `acc::menus::charsheet`, `acc::view_mode`, `acc::pazaak`) across the
owning file, not the header's filename, per the brief's trap #1 — file names
and namespaces differ throughout this codebase (`guidance_autowalk.h` →
`acc::guidance`, `diag_chargen_feats.h` → `acc::diag::chargen_feats`,
`interact_dispatch.h` → `acc::interact`, `peek_description.h` → `acc::peek`,
`spatial_change_detector.h` → `acc::spatial::change_detector`,
`audio_footstep_suppress.h` → `acc::audio::footstep_suppress`,
`minigame_pazaak.h` → `acc::pazaak`, `menus_charsheet.h` →
`acc::menus::charsheet`, etc.). Every "unused" claim below was verified by
grepping the resolved namespace, not the include filename, and by confirming
the only hit was the `#include` line itself (comments on the include line
don't count as usage). Cross-checked `FindPanelByKind`, `exports.def`, and
`hooks.toml` before calling anything dead, per traps #1/#3.

## Section A — general low-level cleanup

### A1 — 25 unused `#include`s in menus.cpp, left behind by the Phase-1 split (menus.cpp:29-76)

menus.cpp used to be the whole menu subsystem; the Phase-1 structure pass
carved `OnHandleInputEvent` out to `menus_dispatch.cpp`, first-sight/focus
logic to `menus_focus.cpp`, and the general monitors to `menus_monitors.cpp`.
The functions moved; the blanket include list at the top of menus.cpp did
not. Grepping each header's actual namespace across the current file body
(excluding the `#include` line) finds zero real usage for:

- `menus_charsheet.h` (line 29, `acc::menus::charsheet`)
- `menus_chargen_feats.h` (line 32, `acc::menus::chargen_feats`)
- `menus_powers_levelup.h` (line 33, `acc::menus::powers_levelup`)
- `menus_abilities.h` (line 38, `acc::menus::abilities`)
- `menus_modsettings.h` (line 42, `acc::menus::modsettings`)
- `menus_pazaakdeck.h` (line 47, `acc::menus::pazaakdeck`)
- `minigame_pazaak.h` (line 50, `acc::pazaak`)
- `menus_journal.h` (line 51, `acc::menus::journal`)
- `help.h` (line 52, `acc::help`)
- `engine_keymap.h` (line 55, `acc::engine_keymap` / `VksForCode`)
- `engine_player.h` (line 59, `acc::engine::GetPlayer*`) — the include's own
  comment says "(test fixture only)", which matches: zero use in the file.
- `audio_bus.h` (line 62, `acc::audio_bus::`) — same "(test fixture only)"
  self-label, zero use.
- `announce_degrees.h` (line 63, `acc::announce_degrees`)
- `probe_mouselook.h` (line 64, `acc::probe_mouselook`)
- `view_mode.h` (line 65, `acc::view_mode`)
- `cycle_input.h` (line 66, `acc::cycle_input`)
- `guidance_autowalk.h` (line 67, `acc::guidance`)
- `camera_announce.h` (line 68, `acc::camera_announce`)
- `input_pipeline.h` (line 69, `acc::input::`)
- `diag_chargen_feats.h` (line 70, `acc::diag::chargen_feats`) — this logic
  now lives in `menus_focus.cpp`, which has its own copy of this include.
- `interact_dispatch.h` (line 71, `acc::interact`)
- `passive_narrate.h` (line 72, `acc::passive_narrate`)
- `peek_description.h` (line 73, `acc::peek`) — now used from
  `menus_dispatch.cpp`, which has its own copy.
- `spatial_change_detector.h` (line 74, `acc::spatial::change_detector`)
- `audio_footstep_suppress.h` (line 75, `acc::audio::footstep_suppress`)
- `strings.h` (line 76, `acc::strings::`) — comment says "Container loot
  panel announces"; that logic is now in `menus_focus.cpp` /
  `menus_monitors.cpp`, both of which include `strings.h` themselves.

What's left after removing these is the set menus.cpp actually calls:
`menus_extract.h`, `menus_internal.h`, `menus_focus.h`, `menus_pending.h`,
`menus_listbox.h`, `menus_editbox.h`, `menus_chain.h`, `menus_monitors.h`,
`tutorial_hints.h`, `tutorial_popup.h`, `menus_store.h`, `menus_galaxymap.h`,
`menus_keymap.h`, `menus_chargen_attr.h`, `menus_chargen_skills.h`,
`engine_area.h`, `engine_input.h`, `engine_manager.h`, `engine_offsets.h`,
`engine_panels.h`, `engine_reads.h`, `hotkeys.h`, `update_checker.h`,
`focus_guard.h`, `bringup_announce.h`, `transitions.h`, `engine_rebase.h`
(needed directly — menus.cpp is the one file in the batch that still calls
`acc::addr::R()` itself, for `kAddrPanelSetActiveControl`).

- Why it's a problem: every one of these headers pulls in its own transitive
  include chain; a change to any of the 25 unused headers currently forces
  menus.cpp to recompile for no reason, and the list actively misleads a
  reader trying to learn what menus.cpp depends on.
- Proposed change: delete the 25 lines listed above.
- Risk: mechanical — compiler-checked; if anything actually needed one of
  these, the build fails immediately and obviously.
- Estimated line delta: -25.

### A2 — 10 unused `#include`s in menus_dispatch.cpp, same cause (menus_dispatch.cpp:28,30,31,33,35,41,43,50,59,60)

Same shape as A1, in the file that received `OnHandleInputEvent`. Confirmed
zero real usage of the resolved namespace outside the `#include` line for:

- `menus_chargen_attr.h` (line 28, `acc::menus::chargen_attr`)
- `menus_chargen_skills.h` (line 30, `acc::menus::chargen_skills`)
- `menus_charsheet.h` (line 31, `acc::menus::charsheet`)
- `menus_extract.h` (line 33, `acc::menus::extract`)
- `menus_journal.h` (line 35, `acc::menus::journal`)
- `menus_pending.h` (line 41, `acc::menus::pending`) — Enter/Esc/Left-Right
  dispatch to the queue now happens inside `menus_chain.cpp`'s
  `HandleEnterActivation`/`HandleEsc`/`HandleLeftRight`, not here.
- `menus_store.h` (line 43, `acc::menus::store`)
- `engine_rebase.h` (line 50, `acc::addr::` / `Ok(`) — this file has no
  direct `R()`-wrapped address of its own.
- `tutorial_hints.h` (line 59, `acc::tutorial_hints`)
- `tutorial_popup.h` (line 60, `acc::tutorial_popup`)

- Why it's a problem: same as A1 — unnecessary rebuild fan-out and a
  misleading dependency list on the single largest function in the mod.
- Proposed change: delete the 10 lines listed above.
- Risk: mechanical.
- Estimated line delta: -10.

### A3 — unused `#include "menus_charsheet.h"` in menus_monitors.cpp (menus_monitors.cpp:20)

Same cause, smaller instance. `acc::menus::charsheet` has zero real
references in this file.

- Risk: mechanical.
- Estimated line delta: -1.

### A4 — ~26 unused using-declarations left behind by the Phase-1 split (menus.cpp:92-138, menus_focus.cpp:52)

Distinct from A1/A2 (dead *includes*): these are dead *using-declarations* —
names imported into unqualified scope by the "Step 2B / Step 4 / Step 5 seam"
comment blocks at the top of menus.cpp, for functions/state that used to be
called directly from this file and have since moved (with the calling code)
to `menus_focus.cpp`, `menus_dispatch.cpp`, `menus_chain.cpp`, and
`menus_chain_input.cpp`. Verified by grepping each bare name across
menus.cpp's *body* (excluding its own using-declaration line and excluding
comment-only mentions):

Step 2B block, all five unused (menus.cpp:92-96):
`IsChainNavigable`, `IsClassSelectionIcon`, `ClassLabelCacheLookup`,
`ClassLabelCacheStore`, `GetControlCenter`.

Step 4 block, all seven unused (menus.cpp:103-109):
`FindControlById`, `FindListBoxChild`, `IsSaveLoadPanel`,
`ReadSaveLoadEntryString`, `DriveListBoxSelection`, `ListBoxNavResult`,
`QueueButtonByIdActivate`.

Step 5 block, 13 of 24 unused (menus.cpp:116-139) — `g_chain`,
`g_chainPanel`, `g_chainIndex`, `g_chainCount`, `g_tabbedPanel`,
`g_tabsStart`, `g_tabsCount`, `ValidateTabbedPanel`, `ValidateChainPanel`,
`DetectTabsCluster`, and `WalkChildren` genuinely are used (verified real
call sites in `OnListBoxSetActiveControl`, `ValidatePanels`, and
`DrainPendingAnnounce`) and must stay. The following 13 are not called
anywhere in the file:
`ChainEntry`, `kMaxChainEntries`, `kVirtualMod_SettingsRoot`,
`g_equipSlotClickOffsetY`, `g_classIconClickOffsetX`, `RebindChain`,
`ResetTabbedState`, `IsTabButton`, `FindAdjacentArrow`, `FindCloseButton`,
`FindCancelButton`, `FindChainEntry`, `ReadPanelActiveControl`.

Plus one in menus_focus.cpp: `using acc::menus::monitors::AnnounceControl;`
(menus_focus.cpp:52) — `AnnounceControl`'s only callers are now in
`menus_chain_input.cpp`, fully qualified; this file's copy of `s_focusMonitorControl`
etc. never calls it.

The comment block at menus.cpp:141-144 ("chain handlers in OnHandleInputEvent
below call it through this using-decl") is itself stale for the same reason
— `OnHandleInputEvent` is no longer in this file, and its `AnnounceControl`
calls now go through `menus_chain_input.cpp` with full qualification.

- Why it's a problem: 24 of ~35 using-declarations in menus.cpp's three seam
  blocks import names nothing in the file calls — pure noise for a reader
  trying to work out what menus.cpp still does, and actively wrong when the
  adjacent comment (line 141-144) describes a call path that moved away.
- Proposed change: delete the 25 dead using-declaration lines (24 in
  menus.cpp + 1 in menus_focus.cpp) and correct/trim the two comment blocks
  that describe the now-false "chain handlers below call it" relationship.
- Risk: mechanical — using-declarations that import an unused name compile
  cleanly either way; removing them can only fail to compile if something
  actually needed the import, which the grep evidence rules out.
- Estimated line delta: -25, plus a few comment-line trims.

### A5 — three raw offsets duplicate existing named constants (menus_pending.cpp:579,581,604)

`Kind::WorkbenchSlotSelect`'s handler reads three `CSWGuiUpgrade` fields by
hand-typed hex offset even though `engine_offsets_fields.h` (reachable
through the `engine_offsets.h` aggregator this file already includes)
declares the same three offsets as named constants:

- `menus_pending.cpp:579` — `reinterpret_cast<unsigned char*>(slotBtn) + 0x58`
  duplicates `kUpgradeSlotCustomValueOff = 0x58` (`engine_offsets_fields.h:550`).
- `menus_pending.cpp:581` — `... + 0x2f74 + slotIdx * 4` duplicates
  `kUpgradeSlotInstalledItemsOff = 0x2f74` (`engine_offsets_fields.h:559`).
- `menus_pending.cpp:604` — `*(reinterpret_cast<unsigned char*>(panel) + 0x2f4c)`
  duplicates `kUpgradePanelCategoryOff = 0x2f4c` (`engine_offsets_fields.h:549`).

The surrounding comments already spell out the field names in prose
("`panel.field35_0x2f74`", "`panel.field25_0x2f4c`"), so the constants
clearly exist and are meant for exactly this; this one function just
wasn't updated when they were added.

- Why it's a problem: two representations of the same RE fact can drift —
  if the offset is ever corrected in `engine_offsets_fields.h` (e.g. a K2
  port), this function silently keeps the old value.
- Proposed change: replace the three literals with
  `kUpgradeSlotCustomValueOff`, `kUpgradeSlotInstalledItemsOff`,
  `kUpgradePanelCategoryOff`.
- Risk: mechanical (same numeric value, named constant already in scope).
- Estimated line delta: 0 (three token substitutions).

### A6 — dead static function GetListBoxRowScreenCenter (menus_internal.cpp:65-77)

```cpp
static bool GetListBoxRowScreenCenter(void* lb, void* row, int& outCx, int& outCy) {
```

Has internal (file-static) linkage and a substantial doc comment about
translating listbox-row-local extents to screen-absolute coordinates, but
it is never called — the only match for the name in the whole
`patches/Accessibility` tree is its own definition.

- Why it's a problem: dead code with no caller anywhere in the mod.
- Proposed change: delete the function (lines 65-77) and its doc comment
  (lines 57-64).
- Risk: mechanical (static, zero callers, compiler would flag on removal if
  wrong).
- Estimated line delta: -21.

### A7 — stale caller list in menus_speak.h's file comment (menus_speak.h:1-4)

```cpp
// Shared speech+log helper for the per-menu Speak* paths
// (unified_action_menu, examine_view, combat_queue — same shape: speak a
// label with interrupt, then log it under the menu's tag with a context
// printf string).
```

`SpeakChoice` (the only function this header declares) has exactly one
caller in the whole tree: `unified_action_menu.cpp`. Neither
`examine_view.cpp` nor `combat_queue.cpp` calls it — both still hand-roll
their own `prism::Speak(...)` + `acclog::Write(...)` pairs inline (e.g.
`examine_view.cpp:667-680`, `combat_queue.cpp:683-767`).

- Why it's a problem: the comment tells a reader this is a 3-file shared
  helper; it's a 1-file helper whose intended two other adopters never
  migrated. Someone reading this file to decide whether it's safe to change
  `SpeakChoice`'s signature would under-scope the blast radius (or, more
  likely here, over-scope it and go looking for call sites in files that
  don't have any).
- Proposed change: either update the comment to name only
  `unified_action_menu.cpp`, or — a live-code question for the user, not a
  mechanical cleanup — migrate `examine_view.cpp` and `combat_queue.cpp`'s
  matching speak+log pairs onto `SpeakChoice` so the comment becomes true.
  Flagging the comment fix as the safe half; the migration is a design call
  since it touches two files outside this batch.
- Risk: mechanical for the comment fix; the migration (if wanted) is
  low risk but out of this batch's scope.
- Estimated line delta: -1 word ("examine_view, combat_queue" removed) for
  the comment fix alone.

### A8 — the manager panels[] array is fetched by hand three times in one file (menus_monitors.cpp:613-621, 735-747, 929-936)

`FindPanelByKind` (Phase 2, `engine_panels.cpp:782`) exists precisely to
collapse the "resolve `kAddrGuiManagerPtr`, SEH-read `kMgrPanelsSizeOffset`/
`kMgrPanelsDataOffset`, cap at 16, walk, compare `IdentifyPanel`" loop that
used to be hand-copied at eight call sites — its own doc comment says so.
It only covers the single-kind case, though (`PanelKind kind` in, `void*`
out). This file has three more call sites that need the *array*, not a
single kind — `MonitorPanelContents` (line 613-620), `MonitorDialogReplies`
(line 735-747, look for the first panel matching a small kind predicate),
and `FindActiveSubScreenPanel` (line 929-936, look for the first panel
whose kind has a strip-spec entry) — and each hand-rolls the same five-line
"resolve mgr, read panelCount/panelData off `base + kMgrPanelsSizeOffset` /
`base + kMgrPanelsDataOffset`, null/count guard, cap at 16" fetch again,
verbatim except for variable names.

- Why it's a problem: three copies of the same non-trivial (SEH-adjacent,
  cap-at-16, offset-pair) fetch in one file is exactly the kind of small
  repetition that drifts — if the 16-entry cap or the manager-pointer
  resolution ever needs to change, three sites have to be found and edited
  identically.
- Proposed change: factor a small internal helper, e.g.
  `bool GetPanelArray(void*** outData, int* outCount)` (or a `struct
  PanelArray { void** data; int count; }` getter) in this file's anonymous
  namespace, and have all three call sites use it. `FindPanelByKind` itself
  is a fourth, cross-file instance of the identical fetch
  (`engine_panels.cpp:782-795`) — worth a look together, but that's a
  different TU and file, so left as a note rather than folded into this
  candidate.
- Risk: low — behaviour-preserving (same reads, same cap, same null
  handling), but touches three call sites in one edit; recommend an
  in-game pass over any screen that opens a sub-screen, a dialog, and a
  content-monitored modal (e.g. Journal, then a StatusSummary popup) after
  the change.
- Estimated line delta: roughly -20 (three 5-line fetches collapse to three
  1-line calls plus one ~8-line helper).

## Section B — AI-pattern findings

### B1 — the "conditional raise is_active" idiom is copy-pasted seven times across one switch (menus_pending.cpp:352-358, 481-495, 516-535, 556-569, 693-711)

`Drain`'s `case Kind::Activate` establishes a documented idiom: read a
control's `is_active` field, and only raise it 0→1 (never clobber a
non-zero engine-bookkeeping value — the corruption rationale is a full
paragraph at lines 266-292). Every later case that touches a control's
`is_active` repeats the identical two-line shape and cross-references the
same explanation by comment instead of by calling a shared function:

- `Kind::Activate` (352-358): `uint32_t prevIsActive = *isActive; if
  (prevIsActive == 0) *isActive = 1;` + a log line spelling out
  `"->1"`/`"(preserved)"`.
- `Kind::EquipSelect` (481-495): same shape, `slotBtn`, comment "Conditional
  raise — see Kind::Activate above for the 5→1 corruption rationale."
- `Kind::EquipCommit` (516-535): same shape applied twice (`row`, `btn`),
  same referring comment.
- `Kind::WorkbenchSlotSelect` (556-569): same shape, `slotBtn`, same
  referring comment.
- `Kind::WorkbenchUpgradeCommit` (693-711): same shape applied twice
  (`row`, `btn`), no referring comment this time (the only case that
  dropped it).

- Why it's a problem: seven near-identical copies of a subtle,
  safety-critical idiom (the whole point is "don't blindly write 1") is
  exactly the shape that regresses under a future edit — someone fixing a
  bug in one copy has to remember there are six siblings elsewhere in the
  same 800-line file, and `WorkbenchUpgradeCommit` already dropped the
  cross-reference comment, so the trail to the corruption rationale is one
  copy-paste away from being lost entirely.
- Proposed change: factor a helper in this file's anonymous namespace,
  e.g. `uint32_t RaiseIsActiveIfZero(void* control)` returning the previous
  value (for the log line's `"->1"`/`"(preserved)"` decision), and call it
  from all seven sites. The one-paragraph corruption rationale moves to the
  helper's doc comment, read once instead of cross-referenced seven times.
- Risk: low — behaviour-preserving (same conditional write, same log
  content), but touches five `case` blocks in the deferred-op drain, which
  is deliberately not unit-testable from outside the game. Needs-in-game-test:
  exercise a tab-button activate (Options), an equip-slot select + commit,
  and a workbench upgrade slot-select + commit, since those are the five
  sites.
- Estimated line delta: roughly -15 (five inlined pairs collapse to five
  one-line calls plus one ~10-line helper).

## Findings (possible bugs — user decides)

None. Everything read as behaviourally intentional and consistent with its
surrounding comments; no branch looked unreachable or contradicted its own
documentation.

## Candidate 28 — narrow-header include opportunities

`engine_offsets.h` is now a 28-line aggregator over `engine_offsets_types.h`
/ `_addresses.h` / `_fields.h` / `_values.h` (Phase 2, C8). Checked every
file in this batch against it:

- Every file in this batch that includes `engine_offsets.h` (all 7 `.cpp`
  files) uses a genuine mix of all four categories — struct/typedef types
  (`CExoArrayList`, `CExoString`), `.text` addresses (`kAddrGuiManagerPtr`,
  `kAddrMoveMouseToPosition`, …), field offsets (`kListBoxControlsOffset`,
  `kControlIdOffset`, …), and vtable/value constants
  (`kVtableCSWGuiButton`, `kVtableListBox`, …). None of the seven would
  meaningfully narrow to one sub-header — this batch is not a candidate-28
  win on its own.
- `engine_panels.h`, `engine_player.h`, `engine_area.h`, `engine_reads.h`
  (the other aggregators the brief names) are each still a single public
  header in this codebase today — only `engine_panels_internal.h` and
  `engine_player_internal.h` exist alongside them, and those are
  implementation-private, not a public narrow-header alternative. So
  `menus_monitors.h`'s `#include "engine_panels.h"` (for the `PanelKind`
  parameter type) is already the narrowest available include; nothing to
  migrate to.

## Files scanned with nothing to report

- `menus.h` — small, accurate public-surface header; every declared
  function has exactly one real implementation and the doc comments match
  current behaviour.
- `menus_focus.h` — five declarations, all with a single real caller
  (`menus.cpp`'s `OnSetActiveControl`), comments match.
- `menus_internal.h` — the cross-TU seam contract; every declared name has
  a live user on both sides of the seam (verified while checking A4).
- `menus_focus.cpp` — clean apart from the one dead using-declaration in A4;
  no dead code, no stale comments, first-sight speech is correctly
  title-only (`AnnouncePanelTitle` returns after the first speakable label,
  never walks further).
- `menus_dispatch.cpp` — clean apart from the dead includes in A2; the
  gate-order comment at the top matches the actual sequence of gates in
  `OnHandleInputEvent`, and every gate's rationale comment matches its code.
- `menus_pending.h` — every documented `Queue*` contract matches its
  `menus_pending.cpp` implementation.
- `menus_speak.cpp` — 16 lines, does exactly what its header says (apart
  from the header's stale caller list, A7).
- `menus_monitors.h` — accurate for what's left after A3.
