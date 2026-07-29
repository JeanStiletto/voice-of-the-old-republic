# Phase 3 — consolidated report and candidate list

Per-file low-level sweep, merged with the Phase-4 AI-pattern pass per the
2026-07-27 decision. Two separately approvable candidate sections, plus a
possible-bugs section the user decides on separately.

**Nothing in this report has been executed. No source file was modified.**

## Scope and method

24 Sonnet agents, batched by line count, read-only on the codebase, each
writing only its own report under `docs/refactoring/reports/`. Coverage:

- 287 patch files (147 .cpp, 140 .h), ~85,700 lines
- 30 kdev .cs files, ~8,600 lines
- 45 installer .cs files, ~8,900 lines
- Total 362 files, ~103,000 lines. Every file assigned to exactly one batch.

Per-batch reports: `phase-3-*.md` (24 files, ~9,000 lines). This document
consolidates them; the per-batch reports carry the full evidence.

Each agent worked to a common brief (evidence standard, do-not-touch list,
the file-name-vs-namespace trap that nearly deleted live code in Phase 1,
and the instruction to report findings rather than fixes). Cross-cutting
patterns discovered mid-sweep were fed forward into later agents' briefs,
so coverage of the guard gap and the localisation gap is deliberately
uneven — later batches checked for them explicitly, earlier ones did not.

### Verified in the main session, not taken on trust

These claims were re-checked by hand before landing here. Two agent claims
were corrected in the process:

- Mojibake in `menus_listbox.cpp`: agent said 48 instances; actual is 52
  lines, and 49 of them are comments. Reclassified from "text corruption"
  to cosmetic. Confirmed isolated to that one file across all 287.
- `strings.h`'s claim that `lang_fr` aliases `lang_en` is FALSE. The
  French table is 651 real entries and never references `lang_en`. This
  stale comment was in the brief handed to the agent as settled fact; the
  agent checked it anyway and was right to.
- `kLinkedListHeadOffset` walk in `combat_special_watch.cpp`: confirmed
  genuinely missing a hop (see F1).
- `strings::Get()` never returns nullptr: confirmed in the dispatcher and
  all six locale tables. ~20 dead null-check sites, not the ~30+ a naive
  extrapolation suggested.
- Phase-2 B7's `BwmFile.cs` constants: confirmed zero uses, 22 raw hex
  literals remaining in the three consumers.
- Installer `ValidatePath()` silent status update: confirmed, including
  the contrasting correct helper in the same file.
- Missing SEH guards: confirmed the flagged functions are unguarded and
  that guarding is the surrounding convention (9 and 23 `__try` blocks in
  the same/sibling file).
- `kHoverPauseMs` shadowing: both copies are 300. Drift risk, not a live
  divergence. Agent classified it correctly.

## Headline conclusions

**1. The Phase-1 splits were executed but never cleaned up after.**
This is the dominant finding, appearing in every batch that touched a
split file: ~120 dead `#include`s, ~26 dead using-declarations, stranded
forward declarations, helpers left with external linkage they no longer
need, and file-banner comments describing the pre-split world. All
compiler-verifiable, all mechanical.

**2. Phase 2 created shared helpers but did not migrate the callers.**
Three independent instances:
- `FindPanelByKind` exists; hand-rolled `panels[]` scans survive in
  `menus_monitors.cpp` (3x) and elsewhere.
- `Core/BwmFile.cs` exists and is imported by all three consumers via
  `using static`; they use zero of its constants and keep 22 raw hex
  literals.
- `view_mode.h` publishes `kHoverPauseMs`; `view_mode.cpp` keeps a local
  duplicate.
"Finish Phase 2" is a concrete, bounded piece of work, and this is it.

**3. Everyone hand-walks the engine's object graph.**
The `AppManager → CClientExoApp / CServerExoApp / GuiManager / Camera`
resolve chain is independently reimplemented at roughly 20 call sites
across at least 8 files. This is the clearest architectural finding in
the sweep, and the most K2-relevant: on a KOTOR 2 port every one of those
walks changes, and there is no single seam to change them at.

**4. A crash-risk class invisible to any single-file review.**
Engine-memory reads that skip the SEH guard their sibling functions use,
found independently in four batches. See F2.

**5. Six-language localisation has holes in exactly the paths nobody
tests** — fallbacks, startup, and probe output. See F8.

---

# SECTION A — general low-level cleanup

Mechanical unless noted. Each item is one commit.

### P3-A1 — Dead includes (~120 across ~30 files)
Largest mechanical item in the sweep. Every one verified by grepping the
qualified symbol the include provides, not the header name.
Distribution: menus.cpp 25, input/interact 19, menus_chain pair 15,
engine_player family 11, combat 7, guidance 6, engine_area family 6,
engine_radial/reads 5, map/discovery 4, plus singles elsewhere.
Also: unused `using` directives across 5 installer files, 1 in kdev.
Risk: mechanical. The compiler is the arbiter — a clean `kdev build`
plus a clean `dotnet build` fully verifies this item.
NOTE: `input_pipeline.cpp` (2 of these) is the sensitive engine-detour
file. Recommend splitting it into its own commit.

### P3-A2 — Dead using-declarations (~26)
`menus.cpp:92-138` (~25) and `menus_focus.cpp:52`, for functions that
moved away in the Phase-1 split. Two adjacent comments are now factually
wrong about what calls what. Risk: mechanical.

### P3-A3 — Dead code with zero callers
Compiler- and grep-verified, each against the namespace and bare name:
`WideToUtf8` (prism.cpp), `ExtractTagName` (update_checker_http.cpp),
`GetListBoxRowScreenCenter` (menus_internal.cpp), `GetMapPinFlags`
(engine_area), `InvalidatePartyCache` (party_cache.cpp),
`GameProcess.IsRunning()` (kdev), `kMaxEdges` (guidance_pathfind.cpp),
`kVK_F9` (probe_pathfind.cpp), `kWaypointMapNoteLocOff` (map_ui_cursor),
`kServerExoAppPartyTableOffset` (engine_player.h, legacy alias),
4 offset constants (probe_camera_state.cpp), 11 constants + 3 state
fields from two retired swoop steering features
(minigame_swoop_audio.cpp), 3 of `core_settings.h`'s 4 pillar structs.
Risk: mechanical. The swoop constants are the largest single block.

### P3-A4 — Dead `Id` enumerators in strings.h (37)
Compiler-verified zero references across all 287 files, including the
`S::` alias and `using namespace` forms. Grouped into 10 root-cause
clusters: 9 combat callouts superseded by `combat_strings.cpp`, 3
pre-unified-action-menu leftovers, 2 minigame cues, and others.
Removing an enumerator means removing its case from all six tables.
Risk: mechanical but touches seven files per cluster. Recommend one
commit per cluster, not one big commit.

### P3-A5 — Dead null-checks on `strings::Get()` (~20 sites)
`Get()` is documented and implemented as never returning nullptr
(verified: dispatcher falls through to `""`, no locale table contains a
single `return nullptr`). Sites: room_topology.cpp (the bulk),
map_ui_cursor.cpp (4), tutorial_hints.cpp (3), help.cpp (1),
same_name_suffix.cpp (1), trap_watch.cpp (2). One has an unreachable
hardcoded fallback behind it. Risk: mechanical.

### P3-A6 — Raw hex literals duplicating an in-scope named constant (~15)
Every case: the named constant already exists, is already reachable, and
in several cases is named in the comment directly above the literal.
Sites include engine_area.cpp:83 (`kInvalidObjectId`),
combat_queue_hooks.cpp:57, menus_pending.cpp (3), menus_journal.cpp:214,
peek_description.cpp:208, state_overrides.cpp:152,
narrated_target.cpp:19-20, spatial_change_detector.cpp (a repeated
radians-to-degrees literal at 3 sites).
Risk: mechanical, but VALUES MUST NOT CHANGE — this is a name
substitution only, and each one needs the value confirmed identical
before substituting.

### P3-A7 — Locally redeclared constants that already have public names
Distinct from A6: these are full local declarations shadowing a public
one. `engine_subscreen.cpp` (4), `map_ui_cursor.cpp` (4),
`tutorial_popup.cpp` (2), `camera_spin_guard.cpp` (1),
`engine_radial.cpp` (`kResRefMaxLen` vs `kResRefSize`),
`audio_loop.cpp`/`audio_pitch.cpp` (same field, two names),
`view_mode.cpp` (`kHoverPauseMs`), plus the AppManager+0x8 offset which
exists under THREE names codebase-wide plus a fourth private copy in
`minigame_swoop_race.cpp`.
One is actively dangerous: `map_ui_cursor.cpp`'s local name is nearly
identical to a canonical constant holding a DIFFERENT value (0x228 vs
0x22c). Risk: mechanical, same value-confirmation requirement as A6.

### P3-A8 — Stale and wrong comments (~20)
Comments that would actively mislead a reader, several in load-bearing
places:
- `engine_area.h:7` documents `CSWSArea.rooms` as an inline array; every
  reader treats it as a pointer. The codebase already knows — a comment
  400 lines away calls this header line misleading.
- `strings.h` claims `lang_fr` aliases `lang_en`. False (verified).
- `camera_spin_guard.h` names `SetCursorPos` as the mechanism; the
  project's own docs record that it does not work.
- `input_pipeline.cpp` names a deleted file (`interact_hotkey`) and warns
  about a bug documented elsewhere as fixed.
- `guidance_pathfind.h:19` says "~200 nodes max"; the live cap is 512.
- `HoloPatcherRunner.cs:23-26` says a constant is "set just under"
  another; it is double.
- `combat_special_watch.h:9` names the wrong cue.
- Plus stale banners in the minigames (4), a stale includer count, a
  stale caller list, truncated header sentences, and a "kept local, hoist
  if a third consumer appears" comment in kdev that survived the exact
  hoist it warned about.
Risk: mechanical (comment-only). High value per byte — these are what a
K2 port will be read against.

### P3-A9 — Mojibake in menus_listbox.cpp (52 lines)
Double-encoded em-dashes alongside correct ones. 49 in comments, 3 in
code (2 are English-internal log strings). No user-facing speech
affected. Isolated to this one file across all 287 — it is the file
candidate 23 rewrote days ago, which points at an edit that round-tripped
the encoding wrong.
Risk: mechanical. **Process implication: add an encoding check to the
Phase-3 execution protocol, or this recurs across every file we touch.**

### P3-A10 — Leftover diagnostics from closed investigations
- `combat_special_watch.cpp:118-122,154-159` — per-item log line, its own
  comment says "remove once the dispatch is understood".
- `minigame_swoop_audio.cpp:606` — per-tick diagnostic left enabled.
- `menus_listbox_picker.cpp:249-283` — live temporary diagnostic block.
- `spatial_wall_surfaces.cpp:186-189` — "remove once understood" comment
  on a diagnostic that is now documented and permanent (comment is the
  wrong part here, not the code).
Risk: low. NOTE: this is NOT a verbosity cleanup. Project rule is that
logs are deliberately not rate-limited; each of these is proposed only
because its own comment marks it as temporary.

### P3-A11 — Unnecessary external linkage
File-local helpers with external linkage and no external caller:
`ScanRoomAllTriangleEdges` (engine_area_walls.cpp:214), two in
`engine_reads_items.cpp`, `PendingContainsHint` (tutorial_popup.cpp:157),
plus `TryResolveDisplayNameOnce` breaking its own file's
anonymous-namespace convention. Risk: mechanical.

### P3-A12 — Per-tick work that runs when it has nothing to do
- `combat_special_watch.cpp:277-284` — recomputes the full party-queue
  walk every frame for 6 seconds to keep a value fresh that is
  overwritten anyway. Compounded by F1, since the walk is also wrong.
- `engine_area.cpp:801-862` — recomputes rebased addresses every call.
Risk: low, but these are behaviour-adjacent — measure before and after.
Same shape as the already-fixed 360x/second combat-round clear.

---

# SECTION B — AI-pattern and structural findings

Judgment items. Several are large; none are mechanical.

### P3-B1 — The engine resolve-chain duplication (~20 sites, 8+ files)
The highest-value structural finding in the sweep and the most
K2-relevant.
- `AppManager → CClientExoApp` walk: ~11 sites across engine_player.cpp,
  engine_player_party.cpp, engine_player_inputlock.cpp, engine_panels.cpp,
  engine_panels_state.cpp (3 sites in one file), engine_picker.cpp.
- `AppManager → ServerExoApp` walk: 3 sites in engine_reads_items.cpp.
- `AppManager → ... → Camera` walk: 4 sites across camera_orient.cpp,
  probe_camera_distance.cpp, probe_camera_state.cpp, engine_player.cpp.
- A four-function client/GUI resolve chain duplicated verbatim across
  engine_radial.cpp, engine_actionbar.cpp and engine_picker.cpp.
Proposal: one small set of resolve primitives with the SEH guard applied
once, in one place. This would also close most of F2 by construction.
Risk: needs-in-game-test, touches many files. Recommend doing it in
per-chain slices, not one commit.

### P3-B2 — Oversized functions (16 candidates)
Phase 1 explicitly deferred function-level decomposition to Phase 3.
Ordered by size:
- `FromControl` (menus_extract.cpp) — ~1470 lines, ~20 already
  comment-delimited sections. Hazard: an early `return nullptr`
  mid-function needs explicit tri-state handling or control flow changes
  silently.
- `BuildForArea` (room_topology.cpp) — 804 lines. Its four merge passes
  were grep-proven to touch ZERO shared anonymous-namespace state.
- `RebindChain` (menus_chain.cpp) — ~680 lines, contains a self-contained
  157-line lambda and four structurally identical 25-line blocks.
- `DriveSelectedPeg` (minigame_turret.cpp) — ~650 lines, 8+ jobs.
- `spatial::change_detector::Tick()` — 588 lines, 4 jobs.
- `map_ui_cursor::Tick()` — 566 lines, 9 jobs.
- `ClassifyCluster` (room_topology.cpp) — 504 lines.
- `unified_action_menu::HandleInputEvent` — 406 lines, 6+ jobs.
- `interact_dispatch::DispatchInteractImpl` — 307 lines, 5 jobs.
- `transitions::Tick()` — 317 lines, 5 jobs.
- Plus `ComputePath`, `AnnounceCurrent`, `dialog_speech::Tick()`,
  `examine_view::BuildRows()`, `BuildAreaWallCache`,
  `floor_puzzle::Tick()`.
All proposed as same-file, same-anonymous-namespace static helpers —
nothing crosses a TU boundary, which is the specific thing that sank
Phase-1 candidates 13 and 24.
Risk: needs-in-game-test, every one. Several are code the player hears
constantly (room_topology, transitions, map cursor, cycle input).
Recommend: pick two or three, not sixteen.

### P3-B3 — Finish Phase 2's migrations
Three bounded items, described in headline conclusion 2:
`FindPanelByKind` adoption, `BwmFile.cs` constant adoption (22 raw hex
literals in 3 files), `kHoverPauseMs` local removal.
Risk: low to mechanical. Highest value-to-risk ratio in Section B.

### P3-B4 — The installer's UIA-notification helper (4 copies)
The ~15-20 line "update label and announce it" block is copy-pasted
across MainForm, Kotor2ModsInstallForm, TslrcmInstallForm and
WorkshopTlkHarvestForm. The missing fifth and sixth copies are exactly
what caused F3 and F5. Extracting it once would prevent that class of
bug by construction.
Risk: low. Recommend doing this WITH F3/F5 rather than separately.

### P3-B5 — Per-screen menu duplication
- `menus_chargen_attr.cpp` / `menus_chargen_skills.cpp` — ~300 lines of
  near-verbatim shell. Two sibling functions were explicitly checked and
  REJECTED as merge candidates (their domain logic genuinely differs).
- `menus_charsheet.cpp::MaybeAnnounce` hand-duplicates offset→format
  logic its own spec table already encodes — and this file has a
  documented HP/FP mixup bug in its history from exactly this drift.
- The "virtual chain-row anchor" trio hand-rolled in three files.
- The clamp-cursor Up/Down/Home/End block written out four times.
- Three byte-identical listbox announce callbacks; four near-identical
  dialog-listbox specs.
Risk: low to medium. The charsheet one has a track record and is the
strongest of these.

### P3-B6 — Smaller duplications
`CResRef`/`FillResRef` across two audio files (the second's own comment
calls itself a "local mirror"); `CategoryNameId()` byte-identical in
view_mode.cpp and passive_narrate.cpp with no shared home despite
`filter_objects.h` already hosting its sibling; a 4x-duplicated cue-play
block in cycle_input.cpp; the "conditional raise is_active" idiom
copy-pasted 7 times across one switch in menus_pending.cpp (the
explanatory comment already dropped from the last copy); eight
panel-vtable detectors reimplementing a helper their own file defines;
a hand-rolled ~65-line JSON parser in the installer where
`System.Text.Json` is already a dependency; K1cp/K2cp installer
skeletons; `IntroMovieDisabler`'s hand-mirrored 85-line pair;
`SpatialAudioManager` reimplementing `SwkotorIniTweaker`'s INI algorithm.
Risk: low, mostly independent, individually small.

### P3-B7 — Belt-and-braces re-validation
Guards that duplicate a check one frame up the call stack, several
self-admitted in their own comments: `prism.cpp` `g_sapiReady` recheck,
`cycle_input.cpp` (2), `map_user_markers.cpp` ("belt-and-braces" in its
own comment), `WalkmeshGeometryAnalysis.cs`, `LaunchCommand.cs` OS guard,
`camera_orient.cpp`'s self-flagged dead parameter,
`spatial_change_detector.cpp`'s `GetCachedWalls`.
Risk: low. **Caution: on engine-derived pointers a "redundant" null check
is usually load-bearing.** Every agent was told this; these are the ones
that survived that filter, but each still needs a second look.

---

# POSSIBLE BUGS — user decides, not proposed as cleanup

### F1 — combat_special_watch.cpp walks the action list one hop short
**Verified in the main session.** `combat_queue.cpp` walks three derefs
(list → internal → head) with a comment spelling it out.
`combat_special_watch.cpp:136` walks two, then treats the list's
INTERNAL pointer as the first node — reading node-data and node-next
offsets off it. It is the sole surviving user of the legacy
`kLinkedListHeadOffset` alias, which is defined equal to
`kListInternalOffset` — which is precisely what makes it wrong.
Impact: backs the shipped "you can act now" heartbeat cue. Whether it
faults or returns garbage cannot be settled by reading.
Compounded by P3-A12 (the same wrong walk runs every frame for 6s).

### F2 — Engine reads missing the SEH guard their siblings use
Four independent batches. **Verified**: the flagged functions are
unguarded and guarding is unambiguously the local convention (9 `__try`
blocks in the same file, 23 in its closest sibling).
- `engine_panels_state.cpp` — `HasActiveDialogPanel`, `HasActiveSubScreen`,
  and `GetForegroundPanel`.
- `dialog_speech.cpp:470-496` — `FindActiveDialogPanel`. Best-evidenced:
  that file's OWN `Tick()` comment documents the panels array being torn
  down mid-cutscene-handoff, and the guarded `FindPanelByKind` is called
  moments later in the same file.
- `engine_radial.cpp` — `ReadControlNameFields`, called unguarded from
  `OnHandleFocusChange`, a hook that fires during engine teardown. The
  project already hit and fixed this exact failure shape once (the
  IsSlider crash dump).
- `menus_editbox.cpp` — `FindMatchingPanel` plus its vtable predicates;
  this file has zero `__try` blocks at all.
- `menus_journal.cpp:230-242` — `ForceRepopulate` skips the `Ok()` guard
  its siblings use.
Precision on severity: these DO null-check and clamp. What is missing is
protection against a stale or freed pointer, which null checks cannot
catch. Real but conditional — needs the teardown window.
Checked and CLEAN: probe/examine/camera batch, core/infra batch (39
files), engine radial/reads' other files. The problem is bounded to
panel- and control-reading paths.

### F3 — Installer: status update after Browse is silent to screen readers
**Verified.** `MainForm.ValidatePath()` writes `_statusLabel.Text` and
sets it red with no `RaiseAutomationNotification` — while `UpdateStatus()`
in the same file does it correctly and carries a comment explaining
exactly why it is required.
Failure: a blind user browses to a wrong folder, **Install silently
becomes disabled**, the reason appears in red text, nothing is spoken.
This is the installer — it happens before any accessibility feature
exists to help them.
Colour is also the only state signal here (red vs default), with no text
equivalent.

### F4 — Installer: Install button's AccessibleDescription goes stale
Captured once at form-init and never refreshed, so it is wrong exactly
when status has changed.

### F5 — Installer: UninstallForm progress is silent
`UninstallForm.UpdateStatus` (UninstallForm.cs:144) updates the label
with no notification, unlike every sibling form. Same class as F3.

### F6 — Installer: four dialogs have no AcceptButton/CancelButton
WelcomeForm, UninstallForm, InstalledOptionsForm, UpdateAvailableForm.
Escape does nothing in them, unlike every other dialog in the batch.
Keyboard-only users have no cancel path.
Lower priority but same family: four ProgressBars have no
AccessibleName.

### F7 — kdev: WalkmeshFaceTypesCommand single-file mode has no length guard
Its own directory-mode sibling has one. A short or garbage file crashes
with a raw stack trace instead of the tool's clean error. Root cause is
F-adjacent to P3-B3: `BwmFile.HasValidHeader` exists and is unused.

### F8 — Hardcoded user-facing strings bypassing Get(Id)
Six shipping languages; each of these ignores the configured one.
- **`core_dllmain.cpp:161-164`** — "Voice of the Old Republic loaded,
  version X", spoken with `interrupt=true` at every launch. **Verified.**
  The first thing every player hears, always in English.
- `menus_chargen_feats.cpp:307-309` — on a failed button-text read, speaks
  the developer log tag: the user hears "BTN_BACK". **Verified.**
- `menus_chargen_feats.cpp:326` — hardcoded "Talent %u" fallback.
- `probe_audio_frame.cpp:71`, `probe_camera_distance.cpp:235`.
Fix is to LOCALISE, not remove — the project rule is that fallback
announcements must never be silenced.

### F9 — Gated dialog replies are never announced as unavailable
Found while tracing dead string ids. The planned enriched cue was never
wired; the live path uses a generic row format with no gating word, so a
player cannot tell an unavailable reply from an available one.
This is a **missing feature, not a regression** — flagged because it was
discovered here, and it is an accessibility gap rather than a code smell.

---

# C4 — landmark doorMatched ordering contract (carried from Phase 2)

Assigned to Phase 3 by STATE.md; now has a concrete proposal.

`Landmark::doorMatched` is written only by `room_topology.cpp` reaching
into `transitions.cpp` through `IterateLandmarks` and
`MarkLandmarkClaimedByDoor`, and read back in
`transitions.cpp::TickProximityLandmarks`. `AttachLandmarksToDoors` is
only correct if `RebuildLandmarkCache` already ran for the current area.
Both call sites get this right by construction, not by enforcement.

Proposal: `IterateLandmarks` already RECEIVES the `area` and discards it.
Thread it through and gate the cache read on it, turning a silent
"0 landmarks matched" into a loud logged mismatch. Exactly one caller to
update. This is better than the reset-API shape Phase 2 anticipated,
because it makes the contract checkable rather than merely documented.
Risk: needs-in-game-test (landmark and door narration).

# Candidate 28 — narrow-header includes (SCOPE CORRECTED)

**The premise in STATE.md is partly wrong, and an agent caught it.**
Only `engine_offsets.h` was actually split into narrow siblings
(`_types` / `_addresses` / `_fields` / `_values`) in Phase 2. For
`engine_player.h`, `engine_area.h`, `engine_panels.h` and
`engine_reads.h`, Phase 1 split the .cpp files but deliberately left ONE
header each. There is nothing to migrate to.

So candidate 28 today means only: files that include `engine_offsets.h`
but need just one of the four. Concrete opportunities found:
`engine_actionbar.cpp` and `engine_levelup.cpp` (one narrow need each),
six headers needing only `Vector`, four audio files likewise, four in
core/infra. Caveat recorded: `guidance_autowalk.h` has a hidden
`kInvalidObjectId` dependency.
Benefit is capped until the other four headers are split, which is NEW
work, not a migration. Recommend: do the `engine_offsets.h` narrowing
opportunistically as other candidates touch each file, and treat
splitting the other four headers as a separate decision.

# Checked and clean (negative results worth having)

- The unified action menu's HARD RULE (never repopulate from OnUpdate or
  PollHotkey) — verified intact, no violations anywhere.
- The `PollHotkey` priority ladder — verified intact, no drift.
- Clamp-vs-wrap submenu convention — correct everywhere checked.
- Phase-2 B4 minigame consolidation — held; no surviving duplicates
  (one unused import remains).
- Phase-2 B5 AppManager constant fold — no duplicate survived.
- Phase-1 combat_diag / combat_queue_hooks separation — clean.
- `R()`/`Ok()` guards in engine_panels_state.cpp — read sensibly.
- Format specifiers across all 194 `Fmt*` ids in six locale tables — 0
  mismatches. Whitespace anomalies — 0. Duplicates — none actionable.
- SEH guards in the probe/camera batch and all 39 core/infra files —
  clean.
- No single-hop linked-list walk outside F1 (the engine_radial batch
  correctly noted its lists are `CExoArrayList`, a different struct).
- Several proposals were explicitly REJECTED by agents on evidence:
  two chargen functions whose domain logic genuinely differs; four
  `audio_bus.h` constants the file documents as deliberately kept;
  `menus_editbox`'s panel scan which genuinely cannot use
  `FindPanelByKind`; two `HandleInput` parameters that are a deliberate
  cross-file dispatch contract.

# Probe-retirement questions (not deletions)

`probe_mouselook`, `probe_pathfind` and `probe_priority_groups` each have
documented evidence their investigation is closed and their findings
consumed by shipped code (view_mode.cpp, guidance_pathfind.cpp,
audio_bus.cpp respectively). Per the probe convention this is a
live-code removal decision for the user, never a mechanical cleanup.
`probe_priority_groups` was re-confirmed LIVE via namespace grep — not
repeating Phase 1's filename-grep mistake.

# Execution notes

- **Phase 2 is still not verified in-game.** Executing Phase 3 on top
  would make a later in-game failure much harder to bisect. Strong
  recommendation: the Phase-2 smoke test (equipment picker → workbench
  arm/close/reopen → loot container / bark / galaxy map) gates Phase-3
  execution.
- **Add an encoding check** to the execution protocol before touching
  files, per P3-A9 — otherwise mojibake scatters across everything we
  edit.
- Sections A and B are separately approvable, per the phase plan.
- Section A items A1-A3, A5, A11 are compiler-verified: a clean
  `kdev build` and `dotnet build` fully validates them.
- Section A items A6 and A7 require per-site confirmation that the value
  is identical before substituting a name. Offset VALUES are on the
  do-not-touch list.
- Every Section B item except B3 needs an in-game test.
- Recommended order: A-block mechanical items first (they shrink the
  diff everything else lands on), then B3, then the F-block accessibility
  bugs with B4, then a chosen subset of B2.
