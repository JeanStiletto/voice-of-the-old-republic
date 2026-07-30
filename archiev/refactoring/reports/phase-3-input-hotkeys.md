# Phase 3 scan — input routing, hotkeys, interaction, unified action menu

Scope (13 files, ~4789 lines):
- input_pipeline.cpp (594) / input_pipeline.h (69)
- input_poll_router.cpp (504) / input_poll_router.h (31)
- unified_action_menu.cpp (1102) / unified_action_menu.h (94)
- hotkeys.cpp (762) / hotkeys.h (224)
- interact_dispatch.cpp (661) / interact_dispatch.h (24)
- interact_internal.h (29)
- focus_guard.cpp (631) / focus_guard.h (64)

Method: full read of every file in the batch (no truncation). Every dead-code
and dead-include claim below is grep-verified against the actual codebase,
not inferred from a single read-through — the exact commands are quoted
under each finding so they can be re-run. Cross-batch claims (a symbol
"used elsewhere") were checked with a repo-wide grep, not assumed.

Specific checks run because the brief called them out:
- **Priority ladder** (`input_poll_router.cpp::PollHotkey`): read the header
  comment's stated order against the actual code order end to end. They
  match exactly (openers+level-up → bare-key announces → examine view →
  combat queue → unified action menu → bare-R narration → Enter dispatch).
  No drift found — nothing to report here.
- **HARD RULE** (unified action menu only populated from `input_pipeline`,
  never from `OnUpdate`/`PollHotkey`): grepped `RePopulate|PopulateMenus|
  PrepareBareDispatch` across every file in the batch except
  `input_pipeline.cpp` itself. Every hit is inside a comment explaining why
  NOT to repopulate from that context (the documented 2026-06-07
  phantom-confirm bug). No violation found.
- **hotkeys.cpp internal consistency**: scripted diff of the `Action` enum
  order (hotkeys.h) against the parallel `kActionNames[]` string table
  (hotkeys.cpp) — 75 entries, byte-identical order. Also verified every one
  of the 75 actions has exactly one `bind(Action::X, ...)` call in
  `InitDefaults` (no action silently left at the zero-initialized/unbound
  default). Both clean — no findings.

## Section A — general low-level cleanup

### A1 — 8 dead includes in input_poll_router.cpp

`input_poll_router.cpp:24-47` includes `engine_actionbar.h`, `engine_manager.h`,
`engine_radial.h`, `filter_objects.h`, `guidance_approach.h`,
`guidance_autowalk.h`, `narrated_target.h`, `engine_rebase.h`. None of the
eight is referenced anywhere else in the file — each string appears exactly
once (the `#include` line itself).

Verified with:
```
grep -c "engine_actionbar\|engine_manager\|engine_radial\|filter_objects\|guidance_approach\|guidance_autowalk\|narrated_target\|engine_rebase" input_poll_router.cpp
```
run once per pattern → each returns `1`. Cross-checked against a full dump
of every `acc::...` symbol actually used in the file (`grep -oE
"acc::[A-Za-z_:]+" input_poll_router.cpp | sort -u`) — none of the eight
subsystems' namespaces appear.

Why: this file is the poll-router half of the candidate-19 split
(`interact_hotkey.cpp` → `input_poll_router.cpp` + `interact_dispatch.cpp`).
The pre-split file needed all of these for the interact-dispatch half, which
now lives in `interact_dispatch.cpp` and has its own copy of the same
includes. The split left the union of both halves' includes in this file
instead of trimming to what `PollHotkey` alone needs.

Proposed change: remove the 8 `#include` lines.
Risk: mechanical (a stray reference would fail to compile). Estimated delta: -8 lines.

### A2 — 9 dead includes in interact_dispatch.cpp

`interact_dispatch.cpp:11-37` includes `combat_query.h`, `engine_input.h`,
`engine_levelup.h`, `engine_manager.h`, `examine_view.h`, `floor_puzzle.h`,
`hotkeys.h`, `input_pipeline.h`, `view_mode.h`. Same story as A1, mirrored:
these are the poll-router half's dependencies, left behind in the
interact-dispatch half after the split.

Verified per-header, each returns exactly 1 occurrence (the include line) or,
for the three with extra matches, the extra matches are confirmed to be
prose comments, not calls:
```
grep -c "combat_query\|engine_input\|engine_levelup\|engine_manager\|examine_view\|floor_puzzle" interact_dispatch.cpp   # each = 1
grep -n "hotkeys" interact_dispatch.cpp        # include line + 2 comment mentions of the English word "hotkeys"
grep -n "input_pipeline" interact_dispatch.cpp # include line + 1 comment mention of the word
grep -n "view_mode" interact_dispatch.cpp      # include line + 3 comment mentions of the word "view_mode"
```
None of `acc::combat::query::`, `acc::engine_levelup::`, `acc::engine_manager::`,
`acc::examine_view::`, `acc::floor_puzzle::`, `acc::hotkeys::`, `acc::input::`
(from input_pipeline.h), `acc::view_mode::`, or any `kInput*` constant from
`engine_input.h` is used as code anywhere in the file — confirmed against
the full `acc::` symbol dump for this file.

Proposed change: remove the 9 `#include` lines.
Risk: mechanical. Estimated delta: -9 lines.

### A3 — 2 dead includes in input_pipeline.cpp (engine-detour file — high sensitivity)

`input_pipeline.cpp:35` includes `engine_offsets.h` with no usage comment at
all, and `input_pipeline.cpp:43` includes `engine_player.h` commented
`// GetPlayerServerCreature`.

- `engine_player.h`: `GetPlayerServerCreature` never appears as a call
  anywhere in the file (`grep -n "engine::GetPlayerServerCreature\|
  engine_player::" input_pipeline.cpp` → no matches; only the include-line
  comment names it). No other symbol from `engine_player.h` is used either.
- `engine_offsets.h`: the file uses no `Vector` (`grep -n "Vector\b"
  input_pipeline.cpp` → no matches) and the one place `kInvalidObjectId`
  appears (`input_pipeline.cpp:110,376`) is inside prose comments — the
  actual code at line 117 hardcodes the literal `0x7F000000u` rather than
  referencing the named constant. So no symbol from `engine_offsets.h` is
  consumed as code.

Per the brief: `input_pipeline.cpp` is the live engine-detour path Phase 1
deliberately left untouched, and this is flagged as high-risk to touch.
Removing two unused `#include` lines is not a structural change to the
detour — it changes nothing about the hooked functions, the SEH blocks, or
the dispatch logic — but given the file's sensitivity this is flagged
separately from A1/A2 rather than folded in, so it can be decided (and
verified with a full clean build) on its own.
Risk: mechanical, but flagged for extra caution given the file. Estimated delta: -2 lines.

### A4 — Misleading include comment in unified_action_menu.cpp (engine_area.h)

`unified_action_menu.cpp:10` reads:
```
#include "engine_area.h"        // ResolveServerObjectHandle, kInvalidObjectId
```
`ResolveServerObjectHandle` is correctly attributed (declared in
`engine_area.h`, used at `unified_action_menu.cpp:355`). `kInvalidObjectId`
is not declared in `engine_area.h` at all — it's defined in
`engine_offsets_values.h:49` and reaches this file via the separate
`#include "engine_offsets.h"` two lines below (`unified_action_menu.cpp:13`,
itself correctly commented `// kInvalidObjectId`). The comment on the
`engine_area.h` line just double-attributes a symbol that already has its
own correct comment on its own include line.

Proposed change: drop `, kInvalidObjectId` from the `engine_area.h` comment.
Risk: mechanical (comment-only). Estimated delta: 0 lines (one-line edit).

### A5 — Stale bug-tracking comment in input_pipeline.cpp names a file that no longer exists

`input_pipeline.cpp:462-464`:
```cpp
// NOTE: the announce path in interact_hotkey is currently crossed
// the other way (it labels key 7 as Misc); that mismatch is the
// live bug being chased — do not "align" this restamp to it.
```
`interact_hotkey.cpp` was split by candidate 19 into `interact_dispatch.cpp`
and `input_poll_router.cpp` — the file this comment names does not exist
in the current tree. Beyond the stale filename, the described bug appears
to already be fixed: the actual announce path today
(`interact_dispatch.cpp::AnnounceBarePersonalKey`, called from
`input_poll_router.cpp:287-290` as `AnnounceBarePersonalKey(0..3)` for keys
4..7) uses the same linear slot mapping this restamp code uses (key 6 →
slot 2 Misc, key 7 → slot 3 Explosives) — there is no crossing left to find.
`input_poll_router.cpp:176-182` documents the same correction in the past
tense ("the earlier 'engine swaps 6↔7' belief was wrong ... it left the
announce/menu pointing at the opposite column").

This is not a proposed logic change (the surrounding restamp code is
correct and matches the rest of the codebase) — just a comment that
references a deleted file and warns about a bug the rest of the codebase's
own comments say was already fixed. Recommend updating/removing the note;
if the user wants certainty before touching engine-detour comments, the
one in-game check that would fully confirm it is closed: press bare 6 and
bare 7 in a session with grenades/mines available in Explosives and a
Sonstiges item present, and confirm the "X, Platz N" queue announce names
match what's actually queued (visible in the action queue / combat log).
Risk: comment-only, mechanical. Estimated delta: 0 lines (edit/remove 3
comment lines).

### A6 — Same stale framing on the diag_label switch, one case (input_pipeline.cpp:415)

```cpp
case 0xec: diag_label = "bare-7"; break;  // engine slot swap (6↔7)
```
The trailing comment uses the same "slot swap" language A5 documents as a
disproven belief. The `diag_label` assignment itself is correct (0xec is
the logical code physical key 7 emits — that part of the file's opening
comment block, lines 348-354, is accurate and distinct from the slot-swap
claim), but the inline "(6↔7)" annotation reads as reasserting the swap
belief right next to the code that proves it wrong 40 lines later. Suggest
folding this into whatever edit resolves A5, e.g. dropping the trailing
comment or rewording to "physical key 7's logical code, not sequential."
Risk: comment-only. Estimated delta: 0 lines.

## Section B — AI-pattern findings

### B1 — unified_action_menu.cpp::HandleInputEvent is a 406-line function doing 6+ separable jobs

`unified_action_menu.cpp:695-1100`. In one function: Esc/close handling
(with in-combat vs. out-of-combat branching), the "follow-cycling"
re-anchor when the narrated target changed mid-session, the "unfold"
path (fold target rows into a personal-only menu), the "lazy re-anchor"
drained-rows recovery, category-list rebuild + cursor relocation,
Shift+arrow description mode, and the Left/Right/Home/End/Up/Down/Enter
dispatch switch (Enter itself branches again on combat state, world-pause
state, and follow-cycle carry status).

This matches the brief's explicit invitation to decompose oversized
functions in Phase 3 (the `ClassifyCluster`/`BuildForArea` precedent). A
plausible split: `HandleEscClose()`, `ReanchorForFollowCycle()` (covers both
the re-anchor and unfold branches, which already share the
`acc::picker::ReanchorRadial` call and the `rowCarried[]` bookkeeping),
`RecoverDrainedTargetRows()` (the lazy re-anchor block), and leave the
final nav switch in `HandleInputEvent` itself calling into the above plus a
new `FireSelectedEntry()` for the Enter case.

This is genuinely delicate code — the comments document several
hard-won, log-verified bug fixes (the phantom-confirm repopulate trap, the
2026-06-08 queue-overwrite fix via the append-mode bit, the
2026-07-17 stale-target restamp fix) that a mechanical extraction could
silently undo if state ordering shifts even slightly (e.g. `targetChanged`
and `selectionCarried` must still be computed before the `BuildCategoryList`
call that can reassign `g.curCat`). Recommend doing this split only with an
in-game pass covering: Shift+Enter/1-3/4-7 open, follow-cycling via `,`/`.`
while the menu is open, an Enter that lands on a carried vs. not-carried
selection after a cycle, and the in-combat stack-mode vs. out-of-combat
fire-and-close branches.
Risk: needs-in-game-test. Estimated delta: neutral (reorganizes existing
lines into named functions; no line-count reduction expected).

### B2 — interact_dispatch.cpp::DispatchInteractImpl is a 307-line function doing 5 separable jobs

`interact_dispatch.cpp:197-503`. Name resolution + fallback, the
transition-trigger walk-to-coordinate special case, the engine-picker
populate-only drive + radial-arm-and-speak path, the dispatch-by-action-id
branch (talk vs. use vs. generic engine click pipeline), and the final
UseObject fallback all live in one function.

Same caution as B1: this function's comments document a **compiler-observed
bug** (the phrase-local `Get(phrase)` producing session-persistent garbage
under `/O2`, `interact_dispatch.cpp:338-350`) whose fix is "resolve the
format string via `Get(literal enum constant)` on both arms of the ternary
— don't fold this back into a single phrase variable without re-verifying
across cold sessions." Any extraction that reintroduces a phrase-local
variable inside that branch would silently reopen a bug that took a
dedicated investigation to close. If this is split, that block should be
copied verbatim into whatever function inherits it, with the warning
comment intact.
Risk: needs-in-game-test (door/placeable interact with the no-target-rows
radial case specifically, to re-exercise the ternary). Estimated delta:
neutral.

### B3 — AnnounceBarePersonalKey / AnnounceBareTargetKey share a near-identical shape (minor, optional)

`interact_dispatch.cpp:519-590` and `:606-649`. Both: read `preDepth` and
bail on `-1` (phantom-press gate), resolve an engine surface pointer and
bail if null, check a count and speak the same `FmtActionBarColumnEmpty`
phrase if empty, otherwise log a "queued; announce via AddAction hook"
line. The underlying reads differ (`engine_actionbar::VariantCount` +
`ReadVariantLabel` vs. `engine_radial::RowActionCount` +
`ReadRowActionLabel`, and the personal path also resolves the shadow index
via `unified_menu::PersonalSelection`), so a shared helper would need a
small function-pointer/lambda seam to stay meaningful. With only two call
sites and the two are already extensively cross-commented ("see
AnnounceBarePersonalKey"), this is a low-priority nice-to-have, not a
must-fix. Noting it because the brief asks for copy-paste blocks
specifically, but not proposing execution.
Risk: low. Estimated delta: roughly neutral to slightly negative.

## Findings (possible bugs — user decides)

None beyond what's already covered by A5/A6 above (which read as stale
documentation of an already-fixed bug, not a live one — the current code
in `input_pipeline.cpp`, `input_poll_router.cpp`, and
`interact_dispatch.cpp` is internally consistent on the key-6/key-7 →
Misc/Explosives mapping). No other place in this batch showed logic that
looked like an actual behavioral bug.

## Candidate 28 — narrow-header include opportunities

- `input_poll_router.cpp:30` — `engine_offsets.h` included for the `Vector`
  type only (`Vector unused;` at line 116). Could narrow to
  `engine_offsets_types.h`.
- `unified_action_menu.cpp:13` — `engine_offsets.h` included for
  `kInvalidObjectId` only (see A4). Could narrow to
  `engine_offsets_values.h`.
- `interact_dispatch.cpp:19` — `engine_offsets.h` included for the `Vector`
  type only (three local `Vector` variables). Could narrow to
  `engine_offsets_types.h`.
- `input_pipeline.cpp:35` — `engine_offsets.h` is dead entirely; see A3
  (removing it makes this a non-issue rather than a narrowing candidate).
- The other four aggregator headers the brief names — `engine_player.h`,
  `engine_area.h`, `engine_panels.h`, `engine_reads.h` — are all included
  somewhere in this batch (`engine_area.h` in 4 files, `engine_player.h`
  in 2, `engine_panels.h` in 2, `engine_reads.h` in 1), but none of them
  currently has a narrower sibling header to migrate to — Phase 1/2 only
  split their `.cpp` implementations (into `engine_area_map.cpp` /
  `engine_area_walls.cpp`, `engine_player_party.cpp` /
  `engine_player_inputlock.cpp`, `engine_panels_state.cpp`,
  `engine_reads_items.cpp`); the `.h` declarations were deliberately left
  in the original single header each time (per the candidate 3/4/5/6
  entries in STATE.md). So there is nothing to migrate to for those four
  in this batch — flagging this so it isn't mistaken for an oversight.

## Files scanned with nothing to report

- hotkeys.h / hotkeys.cpp — clean. Enum/string-table/bind-table parallel
  arrays verified in sync (75/75/75, scripted diff). No dead includes, no
  hardcoded user-facing strings, no dead null-checks.
- focus_guard.h / focus_guard.cpp — clean. All 4 includes used, all
  user-facing text goes through `strings::Get` (`SpeakUrgent` call at
  focus_guard.cpp:618), no dead code found. (Minor observation, not a
  finding: the file-header comment frames the whole file as
  "Diagnostics for..." even though `ArmStartupForegroundGuard` /
  `MaybeReclaimForeground` / `DrainInputBlockedWarning` are production
  fixes, not diagnostics-only — consistent with why candidate 21 renamed
  it out of `diag_*` in the first place. Not raising as a finding since
  the body of the file already explains this; the top-of-file summary
  just undersells it slightly.)
- input_pipeline.h — clean, small, and its documentation of the two
  consume-latches matches the implementation exactly.
- input_poll_router.h — clean; its "routing ORDER is behaviour" framing
  matches `PollHotkey`'s actual order (verified above).
- unified_action_menu.h — clean; every declared function has a live
  caller (confirmed `ReannounceCurrent` from `combat_queue.cpp` outside
  this batch, everything else from within it).
- interact_dispatch.h / interact_internal.h — clean; the internal seam
  (`ShouldSwitchFromInGameMenu`, `OnInteract`, `AnnounceBarePersonalKey`,
  `AnnounceBareTargetKey`) is used exactly as the header describes, only
  from `input_poll_router.cpp`.
