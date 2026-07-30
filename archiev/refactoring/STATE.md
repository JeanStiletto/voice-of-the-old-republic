# Pre-KOTOR-2 Refactoring — State File

This is the persistent scratchpad for the multi-phase refactoring of
`patches/Accessibility/`. **Any Claude session working on the refactoring must
read this file first and update it before the session ends.** It is the
single source of truth for where we are; the chat transcript is not.

Goal: make the codebase the best it can be — fewer bugs, better
maintainability, less accidental complexity — and prepare it for a port to
KOTOR 2 (TSL), which runs a very similar engine but a different executable
(different addresses, some different structures/panels).

## How to resume a session

1. Read this file top to bottom.
2. Check "Current status" below for the active phase and next action.
3. Phase reports live in `docs/refactoring/reports/phase-N-<name>.md`.
4. Full file inventory (line counts, sorted): `docs/refactoring/file-inventory.txt`.
5. Rules of engagement are binding — read them every session.

## Current status  (updated 2026-07-30 — read this block, not the older
## per-phase sections further down, which are history)

- **Active phase:** Phase 3. **Sections A, B and F are ALL DONE and
  play-tested** — B5 and B7 closed 2026-07-30 (afternoon). What remains
  before Phase 3 can be declared finished is the loose-ends list in
  "WHAT PHASE 3 STILL OWES" below; it is four items, none of them large.
- **THERE IS NO PHASE 4.** The 2026-07-27 decisions log merged phases 3
  and 4 into one sweep with two separately-approvable sections: Section A
  (general low-level cleanup) and **Section B (the AI-pattern pass)**.
  Section B is that pass, and it is done. After the loose ends, the next
  phase is **Phase 5 — wrap-up** (end report, merge to main, move
  docs/refactoring/ to archiev/, delete the branch).
- **Phase 2:** done and verified in game (the gate was lifted 2026-07-29).
- **Branch:** `refactor/phase2-coupling`, cut from main @ 4c4e216. Not yet
  merged to main. Phase 1 is already on main.
- **Build state:** green everywhere. `kdev build --clean` = 196 TUs, 0
  warnings (195 + engine_app.cpp, added by B1). kdev and installer
  `dotnet build` = 0 errors; warning baseline is 1 pre-existing installer
  warning (PriorityGroup2da) + 3 in third_party KPatchCore.

### WHAT PHASE 3 STILL OWES (start here next session)

**Exactly one item: A12.** Everything else is closed — see the
2026-07-30 (evening) block at the end of this file for what happened to
each.

1. **A12 — per-tick work that runs when it has nothing to do.** The only
   open Phase-3 item. Two sites:
   - `combat_special_watch.cpp` recomputes the full party-queue walk every
     frame for 6 seconds to keep a value that is overwritten anyway. (F1
     fixed whether that walk is CORRECT; A12 is about how often it runs.)
   - `engine_area.cpp` recomputes rebased addresses on every call.
   Behaviour-adjacent, not cleanup: changing when work happens can change
   what a later read sees, so measure before and after. Same shape as the
   already-fixed 360x/second combat-round clear.

Closed since the list above was written:
- **A10 — DECIDED, STAYS IN.** User: "we have extensive diagnostics
  anyways." Not a deferral; do not re-raise it.
- **C4 — DONE** (ac8ad98), play-tested.
- **The three probes — RETIRED** (1ea67ba), play-tested.
- **Log spam — LARGELY DONE** (85db604). See the evening block for why
  Combat.Diag's residue is genuine signal rather than spam.

**Candidate 28 is NOT owed.** Scope-corrected in the sweep report: only
`engine_offsets.h` has narrow siblings, so 28 today means opportunistic
narrowing as other work touches a file. Splitting the other four headers
is NEW work and a separate decision.

Smaller leftovers, all deliberate and recorded: `probe_camera_state`'s 4
unused offset constants (offset constants are do-not-touch), the
`core_settings.h` pillar structs (their fields are unread, which is a
different and non-mechanical change), and `minigame_swoop_audio`'s
settle-gate paragraph that now describes a gate with no implementation.

### THE METHOD THAT WORKED, AND SHOULD BE REUSED

Verify every report claim against the code before proposing anything. In
six of the seven Section B items the scan's framing or counts were wrong,
sometimes in ways that changed what the right fix was. B2 was the only
fully accurate entry. Twice this session the *compiler or the logs* — not
reading — settled a question the report had rated "mechanical".

### THE VERIFICATION LEDGER (what is and is not actually tested)

**Play-tested and confirmed by the user, 2026-07-30.** Each was applied with
a freshness check on `<install>/patches/accessibility.dll` BEFORE testing, so
the stale-DLL trap does not apply to any of these results:
- B1 (all four slices) — DLL 11:18:48, HEAD f259ea5
- B3 (accessor + cap) — DLL 11:47:31, HEAD 9bfa806
- B6 patch side — DLL 12:31:57; session log `patch-20260730-103915.log`
  confirms a game run at 12:39:15–12:40:55 local, i.e. after that apply.
  (Log FILENAMES are UTC, file mtimes are local — the two are two hours
  apart and that has already caused one false "did they test the right
  build?" scare. The `session start utc=... local=...` header line settles
  it.)

- B2 (all five functions) — DLL 14:24:36, plus the two finding-fixes at
  14:39:15 and the class-icon fix at 14:44:51. Confirmed working by the
  user after the last of those.

**THE INSTALLER PASS IS DEFERRED ON THE USER'S EXPLICIT DECISION
(2026-07-30). Do not re-raise it as an outstanding gap.** Their reasoning:
they will trust the code review until there is a real KOTOR 2 port to test
against, and at that point the installer has to be reworked anyway (it
grows K2 detection, a second game path, K2CP wiring), so a manual pass now
would be spent twice. This is a decision with a trigger, not an oversight —
**the trigger is the K2 port starting.** When it does, run the pass below
BEFORE shipping anything K2-facing, because it is the accumulated debt of
F3-F6, B4 and both B6 hold-backs, and it is all end-user-facing:
- browse to a WRONG folder — the reason must be spoken, and the
  Install-button enable/disable transition announced (F3, F4, F6, B4)
- run an uninstall — progress must be spoken (F5, B4)
- confirm intro movies are disabled on install and restored on uninstall
  (B6 hold-back 2 — nine states are unit-verified, but never on a real
  install)
- if you use the spatial-audio toggle, check EAX lands in swkotor.ini
  (B6 hold-back 1 — verified against a copy of the real ini, not live)

### C8 is DONE (2026-07-29), and the Allard address bug it surfaced is FIXED.

**NEXT ACTION (2026-07-29, now DONE — kept for history).** Phase 2 was
unplayed at the time of writing; it has since been tested and passed,
including on the Allard Russian build.

**C8 executed and verified.** `engine_offsets.h` is now a 28-line
aggregator over `engine_offsets_types.h` / `_addresses.h` / `_fields.h` /
`_values.h`. All 367 declarations identical before and after on type, name
AND value; typedefs and structs byte-identical; every source line
accounted for once; `kdev build --clean` 194 TUs, 0 warnings. Full detail
and the four spec deviations (four files not three, `_fields` not
`_structs`, narrative-follows-meaning rather than a pure type cut, and the
spec's 358 being an undercount of 367) are in
`reports/phase-2-cleanup.md`, section "C8 — EXECUTED 2026-07-29".
Code-index refreshed: `engine_offsets.h.md` rewritten plus four new
entries, `_files.txt` updated.

**The "19 unwrapped addresses" note was wrong, and the truth is worse.**
Inside `engine_offsets.h` there was no gap at all — all 103 .text
constants were wrapped and the 2 unwrapped ones are .data globals,
correctly raw (verified against the exe's real PE section table). But a
codebase-wide scan found **12 genuinely unwrapped .text addresses** in
`engine_panels_state.cpp`, `peek_description.cpp`, `menus_journal.cpp` and
`menus_galaxymap.cpp`, every one `reinterpret_cast` and called.

They ARE wrong on the Russian build: zero of the 214 addresses sigscan
resolved against Allard kept their reference value, two of the twelve are
already in the rebase table displaced by +400 and +432 bytes, and the
other nine are bracketed by resolved neighbours with large non-zero deltas
on both sides. Root cause: kdev's address harvester regex cannot see
`static constexpr`, `constexpr std::uintptr_t`, or inline
`reinterpret_cast` — so they were never even attempted by sigscan. Same
blind spot means the harvester also cannot see the `R(...)` form, so
regenerating the table today would silently shrink it.

**FIXED 2026-07-29** (user asked for the full fix, all three steps):
1. kdev's harvester now sweeps every in-range hex literal instead of
   matching declaration shapes, with explicit `kdev-sigscan: ignore`
   opt-outs; an unresolved `.text` address now exits 7 instead of 0.
2. The Allard exe was extracted from the repo-root archive with Windows'
   own bsdtar (System32\tar.exe reads RAR5 — no third-party tool needed;
   it is a WinRAR SFX, PE timestamp matches kTimestampAllard172) and
   `engine_rebase_table.inc` regenerated: 214 → 223 entries, nothing lost
   or changed, the 9 additions exactly the missing addresses. Final
   resolve: 221 unique + 2 ordinal + 2 hand-resolved = all 225, zero
   unresolved.
3. All twelve wrapped in R() and guarded with Ok(), guards placed per site
   (is_active must not be left forced; CloseInGameMenuToWorld needs both
   its addresses or neither).

Zero behaviour change on the reference build — R() is identity there.
A repo-wide scan now finds zero unwrapped `.text` addresses outside
engine_rebase.cpp's own mapping table. Full detail in the report.

Phase-2 work is NOT yet in-game tested. Smoke-test list:

*On the standard (Steam/GoG) build — covers all of Phase 2:*
- A short swoop race and a turret sequence. B4 rewired the SEH read
  primitives all three minigames share.
- Room + landmark narration in a nav-heavy area. A1/A2 touch it.
- C8 is a pure constant move with a proved-identical constant set, and the
  address fix is a no-op here (R() is identity), so neither needs its own
  test beyond the above.

*On the Russian build (Allard 1.72) — the paths that were broken:*
- Item and store descriptions (inventory, a merchant), and a quest
  description in the journal.
- Galaxy-map planet navigation.
- Opening an in-game menu and closing it back to the world with Esc —
  movement must still work afterwards.
- A conversation that gets interrupted or walked away from, to exercise
  the dialog-state unstick.

**Also still open:** C4 (doorMatched split → Phase 3), candidate 28
(includer migration → falls out of Phase 3). Candidate 23 is now DONE —
see its entry further down.

## SESSION END 2026-07-29 (afternoon) — HISTORY, superseded by the
## "Current status" block at the top of this file.

Phase 2 is code-complete and committed. What happened this session, and
what is genuinely still owed:

**Done and committed:** candidate 23 in a narrowed form (state + monitors
moved to `menus_listbox_picker.cpp`, all thirteen specs stayed put — the
reasoning is in this file under the candidate-23 entry), a `FindPanelByKind`
helper replacing eight hand-copied panels[] scans, plus two performance
bugs found while investigating a user-reported stall (below). `tools/` is
now tracked in git.

**THE VERIFICATION GAP — read this before trusting the smoke-test list
above.** Two of the three in-game sessions this day ran a STALE DLL
(2026-07-27), because `kdev apply` silently skips the copy while the game
holds the file open. The user reported "all tested and working" against
that stale binary, so that report does not cover any of this session's
work. Only the 13:43 session ran the new code, and it was a pure turret
run: it never opened an equipment picker, a workbench picker, a loot
container, a bark bubble, a tutorial popup or the galaxy map.

So **the candidate-23 picker split has still never actually executed.**
It builds clean and the state analysis is sound, but no picker code path
has run. What to exercise, in order of risk:

1. Equipment picker — open a slot, arrow through items, Enter to equip.
2. Equip an already-equipped item (the unequip route).
3. Workbench upgrade — arm a crystal slot, arrow, commit; then close the
   panel WHILE ARMED and reopen it. That last step is the one that
   exercises the disarm-on-panel-gone path and the park-latch change.
4. Loot container, a bark bubble, the galaxy map, a tutorial popup — one
   each, they moved onto `FindPanelByKind`.

**Lesson worth keeping:** after `kdev apply`, check the mtime on
`<install>/patches/accessibility.dll` before believing any test result.
This cost a full round trip today.

**Performance thread — closed deliberately, not exhausted.** Two real
bugs found and fixed (both committed): an unbounded per-frame nav-graph
rebuild retry in `room_topology`, and a no-op combat-round clear logging
360x/second. A third avenue was measured and dropped: the per-line
`fflush` in `log.cpp` is only microseconds per call, so even the worst
burst in the log (1203 lines in one second) costs ~6ms — not worth
changing. Residual "a bit laggy" during the turret cutscene sequence is
NOT explained. The engine's own module loads there are 5-11s each and
dominate; our Dispatch never tripped the 200ms SLOW TICK threshold, but
that threshold is blind to the 5-15ms band where the symptom would live.
If it is picked up again, the cheap next step is lowering
`kSlowDispatchMs` in `core_tick.cpp` to ~10 for one diagnostic run — it
prints only when a tick is actually slow and names the worst of the 39
phases. The user declined a per-second budget reporter as too noisy.

**Next phase:** Phase 3 (per-file sweep). Not started. It absorbs C4 and
candidate 28.

## Rules of engagement (binding for every phase)

- **Behavior-preserving only.** No functional changes, no feature work, no
  drive-by fixes of gameplay behavior. If a phase finds an actual bug, it goes
  on the report as a *finding* for the user to decide, not silently fixed.
- **Approval gate per phase.** Each phase produces a numbered candidate list in
  its report file. Nothing is executed until the user approves the list (or a
  subset) in chat and gives the explicit execute command. Discussion happens
  item by item when an item is non-obvious.
- **Do-not-touch list** (only with an explicit, per-item user approval):
  - `hooks.toml` / `allard.hooks.toml` addresses and hook byte patterns
  - Numeric values in `engine_offsets.h` (renaming/regrouping is fine; values are RE facts)
  - Calling-convention typedefs (`__thiscall` etc.) and hook function signatures
  - `exports.def` export names
  - `manifest.toml` semantics
- **Build after every executed batch:** `kdev build` must succeed warning-clean
  relative to baseline. In-game smoke test by the user before a batch is
  considered done (per project rule: don't commit untested code — for pure
  mechanical renames/moves a clean build + user spot-check of the affected
  subsystem is the agreed bar; the report marks which items need in-game
  verification).
- **Small logical commits** on the branch, one theme per commit, so we can
  bisect if something breaks in-game later.
- **Subagent policy:** cheap models (Haiku/Sonnet) for mechanical per-file
  scans, batched ~10 files per agent, read-only on the codebase + write access
  only to their own report file under `docs/refactoring/reports/`. Fable/Opus
  only for architecture-level judgment. Any fan-out over 3 agents: state the
  agent count and rough cost in chat and get a typed go-ahead first.
- **No markdown tables** anywhere in reports (screen reader).

## Phase plan

- **Phase 0 — Infrastructure & baseline** (this phase)
  - [x] Branch `refactor/pre-k2-cleanup` created
  - [x] STATE.md + reports/ + file-inventory.txt created
  - [ ] Plan improvements discussed with user, decisions recorded here
  - [x] Code index fully refreshed 2026-07-27 (user-approved fan-out, 15 Sonnet
        agents): all 262 patch files re-indexed, 6 orphan entries for deleted
        sources removed (actionbar_menu, radial_menu, target_action_menu),
        _files.txt regenerated; NEW code-index/kdev/ (25 files) and
        code-index/installer/ (42 files incl. ModInstallers/). Completeness
        verified by scripted diff (sources ↔ .md, zero missing). Agents reported
        heavy staleness in the old index — trust the new entries only.
  - [x] Build baseline captured 2026-07-27: `kdev build --clean` OK — 178 TUs
        in 121.3s, 0 compiler warnings, Accessibility.kpatch + loader
        dinput8.dll produced. (178 TUs > 130 patch .cpp because the KPatchManager
        framework wrapper TUs compile into the patch too.)
- **Phase 1 — Structure audit.** Mod-wide structure: gigantic files
  (wall_topology 3404, menus 2269, turret_game 2072, strings.h 2003...),
  over-split files, misplaced responsibilities, naming/module-prefix
  consistency, header hygiene. Output: report + candidate list → approval →
  execute.
- **Phase 2 — High-level cleanup.** Coupled responsibilities, cross-file
  duplication, missing shared helpers/classes, bigger simplifications and
  restructurings. Includes the **K2-portability lens**: identify every
  K1-specific dependency and propose the engine-abstraction seam for the port.
  Output: report + candidate list → approval → execute.
- **Phase 3 — Low-level per-file cleanup.** Every file swept for dead code,
  small repetitions, stale comments, inconsistent patterns, leftover debug
  paths — the residue of a long-lived worked-on codebase.
  Output: report + candidate list → approval → execute.
- **Phase 4 — AI-pattern pass.** Explicit sweep for LLM-generated smells:
  needless verbosity, over-defensive code, redundant comments, copy-paste
  blocks an abstraction should own, unreadable/spaghetti spots.
  Output: report + candidate list → approval → execute.
- **Phase 5 — Wrap-up.** End report (what changed, what was rejected, K2-port
  readiness notes), merge to main, move docs/refactoring/ to archiev/,
  delete the refactoring branch.

## Phase-1 candidate approvals (running record)

- Candidate 1 (menus.cpp three-way split → menus_internal.cpp /
  menus_focus.cpp / menus_dispatch.cpp): **APPROVED** 2026-07-27.
  Execution pending explicit execute command after the walk.
- Candidate 2 (menus_chain.cpp → menus_chain_input.cpp, five input
  handlers move verbatim): **APPROVED** 2026-07-27.
- Candidate 3 (engine_area.cpp → + engine_area_map.cpp +
  engine_area_walls.cpp; move AreaObjectIterator::Next() back with core):
  **APPROVED** 2026-07-27.
- Candidate 4 (engine_reads.cpp → + engine_reads_items.cpp, contiguous
  cut at the domain boundary): **APPROVED** 2026-07-27.
- Candidate 5 (engine_player.cpp → + engine_player_party.cpp +
  engine_player_inputlock.cpp; non-contiguous cuts, extra care +
  in-game leader-announce/input-restore check): **APPROVED** 2026-07-27.
- Candidate 6 (engine_panels.cpp → + engine_panels_state.cpp, cpp-only
  move, declarations stay in engine_panels.h): **APPROVED** 2026-07-27.
  (User also raised the many-small-files question; answered in chat —
  splits stay seam-driven, burden of proof on the split.)
- Candidate 7 (combat.cpp → + combat_log.cpp, msg-router parser rule set
  moves; small internal header; in-game combat-announce check):
  **APPROVED** 2026-07-27.
- Candidate 8 (examine_view.cpp → + examine_view_effect_names.cpp, pure
  locale-data move): **APPROVED** 2026-07-27.
- Candidate 9 (update_checker.cpp → + update_checker_http.cpp, generic
  WinHTTP/JSON/version-compare primitives move): **APPROVED** 2026-07-27.
- Candidate 10 (engine_offsets.h → ~5 subsystem headers behind thin
  aggregator; values byte-for-byte; verify via scripted name/value diff
  of the preprocessed constant set): **APPROVED** 2026-07-27.
- Candidate 11 (hooks.toml subsystem banners, BANNERS-ONLY variant — no
  entry reordering): **APPROVED** 2026-07-27. Batch 1 fully approved.
- Candidate 12 (wall_topology.cpp 5-way split) **APPROVED EXTENDED**
  2026-07-27: new files named by SYSTEM, not wall_topology_* — the file
  mixes two systems the user wants name-split: wall probing ("is there a
  wall": wall_probe.cpp) vs room-shape/perceptual-region labeling
  (Korridor/Kreuzung/Bereich: room_topology*.cpp). Ambiguous functions
  sorted by which system consumes them; unclear ones reported, not
  silently filed. wall_topology_internal.h extern-audit + in-game
  nav-area narration check required.
- Candidate 12b (NEW, user-requested): symbol/namespace/header rename to
  complete the wall-vs-roomshape name split across the codebase.
  **APPROVED** 2026-07-27. Own commit right after the 12 split builds
  clean (bisectable); compiler-checked rename; same in-game check covers
  both.

- Candidate 13 (transitions.cpp → + transitions_landmarks.cpp; small
  header for the 2-3 cross-boundary cache reads; runs after 12/12b per
  execution order; in-game room/landmark narration check):
  **APPROVED** 2026-07-27.
- Candidate 14 (combat_diag.cpp → production hook OnCombatRoundAddAction
  + shared queue-size readers move to NEW combat_queue_hooks.cpp; export
  names unchanged; in-game queue-announce check): **APPROVED**
  2026-07-27. Batch 2 fully approved.
- Candidate 15 (kdev SoundScoreCommand.cs → extract WavAnalysis.cs,
  ~835-line self-contained engine; no finer engine split): **APPROVED**
  2026-07-27.
- Candidate 16 (kdev WalkmeshGeometryAuditCommand.cs → extract
  WalkmeshGeometryAnalysis.cs, ~590-line engine; BWM-reader dedup with
  WalkmeshStatsCommand stays a Phase-2 item): **APPROVED** 2026-07-27.
- Candidate 17 (installer Program.cs → GamePathDetector.cs +
  InstallFlow.cs + UninstallFlow.cs; version-compare STAYS in
  Program.cs; manual install/uninstall pass required before next
  release): **APPROVED** 2026-07-27. Batch 3 fully approved.
- Candidate 18 (menu_speak.* → menus_speak.* rename, one includer;
  final grep before executing): **APPROVED** 2026-07-27.
- Candidate 19 (interact_hotkey.cpp charter): **APPROVED, OPTION 2**
  2026-07-27 — PollHotkey (~450 lines) moves to NEW input_poll_router.cpp
  (poll-side router), interact dispatch stays/renames to
  interact_dispatch.cpp; input_pipeline.cpp UNTOUCHED (sensitive
  engine-detour path). Routing priority ladder preserved verbatim.
  In-game input smoke test required.
- Candidate 20 (minigame prefix): **APPROVED, OPTION 1** 2026-07-27 —
  rename drivers into the family: turret_game.* → minigame_turret.*,
  swoop_race.* → minigame_swoop_race.*, swoop_spatial_audio.* →
  minigame_swoop_audio.*, pazaak.* → minigame_pazaak.*; minigame_aim.*
  keeps its name (shared primitives). ~10-15 include sites updated;
  exported function names and hooks.toml untouched (they reference
  functions, not files). User chose rename over document-only.
- Candidate 21 (probe_/diag_ convention): **APPROVED, OPTION 2**
  2026-07-27 — document the convention (probe_* = throwaway RE tooling;
  diag_* = diagnostic-only) AND rename the two production-critical
  files out of the diag namespace: diag_focus.* → focus_guard.*,
  camera_spin_diag.* → camera_spin_guard.*. One includer each.
  Background per user: diag names are leftovers from
  "something-is-broken" investigations that later became shipped fixes.
- Candidate 22 (probe_priority_groups.cpp/.h DELETE + remove stale
  include from core_tick.cpp; confirmed dead — Tick/DumpOnce called
  nowhere; git history preserves it): **APPROVED (delete)** 2026-07-27.
  Batch 4 fully approved.
- Candidate 23 (menus_listbox.cpp → + menus_listbox_picker.cpp, the two
  externally-observed armed picker specs + monitors, ~575 lines):
  **APPROVED** 2026-07-27.
- Candidate 24 (engine_radial.cpp → + engine_radial_diag.cpp, ~370
  lines debug-only logging; consistent with the candidate-21 diag
  convention): **APPROVED** 2026-07-27.
- Candidate 25 (small kdev extractions): **25a APPROVED** (TlkFile →
  top-level TlkFile.cs, matches PeInfo/Signatures convention);
  **25b/25c REJECTED** (MsvcToolchain, minidump reader — marginal).
  2026-07-27.
- Candidate 26 (kdev Core/ folder + Kdev.Core namespace for Config,
  EngineAddresses, GameProcess, PeInfo, Signatures + the new
  WavAnalysis/WalkmeshGeometryAnalysis/TlkFile engines; real namespace
  change, full dotnet build to verify): **APPROVED** 2026-07-27.
  (25a's TlkFile lands directly in Core/ since 26 is approved.)
- Candidate 27 (27a swoop_spatial_audio split, 27b turret_game reads
  extraction): **DEFERRED (both)** 2026-07-27 — coherence outweighs
  size; swoop file still under active tuning (rock-avoidance next).
- Candidate 28 (migrate includers of engine_player/area/panels/reads.h
  to narrower headers): **DEFERRED** 2026-07-27 — the cpp splits use
  aggregator-style old headers (zero includer churn); migrate
  opportunistically as Phase 3's per-file sweep touches each file.

WALK COMPLETE 2026-07-27. Approved: 1-18, 19(opt 2), 20(opt 1),
21(opt 2), 22(delete), 23, 24, 25a, 26, plus user additions 12-extended
and 12b. Rejected: 25b, 25c. Deferred: 27a, 27b, 28.
Next: execution, batch by batch, in candidate order.

## Execution protocol (agreed 2026-07-27, binding for the executor)

- User decision: ALL approved candidates execute in ONE long run
  (user tests everything once at the end, in one in-game session).
  Session runs on Opus 5 by user's choice (mechanical work; scan
  reports + this file carry the judgment already made).
- Order: candidate number order (1..26 as approved). One commit per
  candidate; tightly-coupled pairs (12+internal header) may share.
- After EVERY patch-side candidate: `kdev build` must pass with 0
  warnings (baseline: Phase 0, 178 TUs). After every C# candidate:
  `dotnet build` clean. A failure stops the line — fix before moving on.
- Candidate 10 extra check: scripted name/value diff of the
  preprocessed engine_offsets constant set, must be empty.
- Candidates 12/12b/13 extra check: BEFORE touching wall_topology,
  capture a DumpGraphToLog output for a fixed area from the current
  build (needs the game once — if not feasible pre-execution, capture
  the dump instructions for the user's end test instead and rely on the
  extern audit). After 12, 12b, and 13: same dump must be byte-identical.
  Plus: grep-audit that every former file-static appears exactly once
  as a definition (silent-state-duplication guard).
- Seam boundaries: follow the scan reports (phase-1-scan-a/b/c.md)
  — line numbers were grep-verified there. If reality deviates from a
  report (seam messier than documented), STOP that candidate, note it
  here, report to user; do not improvise a different split.
- Behavior-preserving only; do-not-touch list in Rules of engagement
  applies unchanged (hook addresses/bytes, offset VALUES, calling
  conventions, exports.def names).
- End deliverable: summary + the user's single smoke-test checklist
  (menus nav, leader announce + input restore, combat announces +
  queue, region/landmark narration, hotkey routing, installer/kdev
  build note).

## Execution findings (deviations from the scan reports)

- **Candidate 10 (engine_offsets.h regroup) — STOPPED, not executed.**
  Two reasons, both discovered at execution time:
  1. The scan claimed "the file's own section index already documents ~13
     natural per-subsystem blocks". There is no section index. The file is
     a continuous stream of per-topic comments with structs
     (CExoArrayList, Vector, CExoString), typedefs and constants
     interleaved — several typedefs depend on structs declared earlier, so
     a 5-way cut needs a hand-verified dependency order, not a line-range
     split.
  2. The approved technique (keep engine_offsets.h as a thin aggregator so
     all 77 includers keep compiling) does NOT deliver the benefit the
     report claimed for it. If the aggregator still includes every new
     header, every includer still pulls in every constant and still
     rebuilds when any block changes. The rebuild-fan-out win only lands
     once includers migrate to narrower headers — which is candidate 28,
     explicitly DEFERRED. So as approved, the candidate delivers header
     navigability only, at the cost of the largest and most
     dependency-sensitive diff in the batch.
  Per the execution protocol ("if reality deviates from a report, STOP
  that candidate, note it here, report to user; do not improvise a
  different split") this was left unexecuted for the user to re-decide.
  Options to put to the user: (a) drop it; (b) do it together with
  candidate 28 so the benefit is real; (c) do a smaller conservative
  version moving only the clearly-contiguous topical runs.

- **Candidate 12 (wall_topology 5-way split) — PARTIALLY executed.**
  Done: the wall-vs-roomshape system split the user asked for.
  `wall_probe.{h,cpp}` now holds the probe primitives (ProbeWall stays
  private; ProbeDistance / IsAlcoveAlongAxis / ProbeClearance8 published),
  and the rest became `room_topology.{h,cpp}` with namespace
  `acc::room_topology` (candidate 12b). The probe block was verified
  self-contained: it reads the perimeter-wall cache directly and holds no
  room-topology state, so the extraction needed no shared externs at all —
  the silent-state-duplication risk never materialised.
  NOT done: the further doors / classify / build / diag 4-way split of
  what is now room_topology.cpp (3289 lines). Measured the cross-block
  call matrix before cutting; the "documented phases" are not separable.
  Nearly every anon-namespace helper is called from 2-5 of the proposed
  files — e.g. UFFind is used 18x from the build block, 4x from diag, 3x
  from the union-find block; ClassifyEdge from 4 blocks; OctantFromVector
  from 4; RenderDoorDirection from 4; SnapshotDoors from 3. Executing the
  split would mean publishing ~25 internal functions plus 6 pieces of
  mutable state (g_graph, g_doors_stability, s_uf_parent, the three
  s_class_* counters) through an internal header — turning a cohesive TU
  into a wide-interface mini-library, which is the opposite of the phase's
  goal and is exactly where the silent-duplication risk lives.
  Per the execution protocol this stops for the user to re-decide.
  Options: (a) accept room_topology.cpp at 3289 lines as cohesive;
  (b) extract only the diag block (~248 lines, needs ~5 declarations +
  s_uf_parent/s_class_* published) as a smaller, bounded step;
  (c) treat the real problem as function-level (ClassifyCluster 530 lines,
  BuildForArea 780 lines) and hand it to the Phase-3 decomposition sweep,
  which is where the report already put the comparable menus_extract case.

- **Candidate 13 (transitions.cpp landmark cache) — ATTEMPTED, REVERTED.**
  The scan called this a "looser seam" and it is looser than even that
  suggested. A function-level scan looks encouraging: the speech side calls
  only four landmark lifecycle functions, and the landmark side calls only
  IsWorldSpeechGatedImpl. But the anon-namespace *state* is interleaved at
  variable granularity, which a function-name scan does not see:
    * The block that reads as "landmark proximity state" also holds
      g_last_spoken_room_text / g_last_spoken_pos / g_last_spoken_pos_valid,
      which are room-speech state.
    * The speech side resets the landmark proximity trio
      (g_lm_prox_pending_idx / _pending_count / _last_spoken_idx) directly
      on area change and after speaking a room label.
  A correct split therefore needs per-variable sorting plus a new
  reset API for the cross-side pokes — i.e. more than the approved verbatim
  move, and with more of the same likely still hidden. Attempt was reverted
  (tree clean, build green) rather than improvised past.
  Options for the user: (a) drop it — transitions.cpp at 1412 lines is
  cohesive enough; (b) approve a version that may add small reset/accessor
  functions instead of moving raw state, and budget an in-game
  room+landmark narration pass for it; (c) fold it into the Phase-2
  coupling work, where "who owns this state" is the actual question.

- **tools/ is gitignored — kdev refactoring cannot be committed.**
  Discovered while executing candidate 26. `.gitignore:49` excludes
  `/tools/` ("Internal dev tooling ... Excluded from the public repo for
  now; may be released separately later") and `git ls-files tools/`
  returns zero files. Consequences the Phase-1 plan did not account for:
  the kdev candidates (15, 16, 25a, 26) produce no commits, are not
  bisectable, and have no rollback path — the passing build is the only
  safety net. They were executed anyway (all verified: `dotnet build`
  clean at 0 errors / 0 warnings, `kdev --help` runs, `kdev build`
  produces the kpatch), and the resulting source was copied to the
  session scratchpad as a manual backup.
  Decision needed from the user: leave kdev unversioned, un-ignore
  `/tools/` so dev tooling gets history too, or give it its own repo.
  Until that is settled, treat further kdev refactoring as higher-risk
  than the patch-side work, not lower.

- **Candidate 22 (delete probe_priority_groups) — CANCELLED, premise false.**
  The scan reported it "confirmed dead in practice — included by
  core_tick.cpp but its Tick()/DumpOnce() is called nowhere". It is live:
  `core_tick.cpp:405` calls
  `PHASE("probe.priority_groups", acc::probe::priority_groups::Tick())`.
  The scan's grep looked for `probe_priority_groups::`, but the namespace
  is `acc::probe::priority_groups` — file name and namespace differ, so
  the search missed every call site. The deletion was attempted, the
  compiler caught it immediately, and the files plus the core_tick include
  were restored (tree back to baseline, build green).
  Two follow-ups worth noting for later phases: (1) the same file-name vs
  namespace mismatch means any other "is this dead?" judgement in the scan
  reports that relied on a file-name grep should be re-checked before
  acting; (2) if the probe really has served its purpose, retiring it is
  still a reasonable question — but it is a live-code removal decision,
  not the dead-code cleanup it was approved as.

- **Candidate 24 (engine_radial diagnostics) — ATTEMPTED, REVERTED.**
  Same shape of miss as candidate 13: a function-name scan looked clean
  (the operational half never calls the four dump functions), but the
  dumps also need ~10 anonymous-namespace *constants* (kResRefMaxLen,
  kTamTargetActionsOffset, kTargetActionStride, the kRow*Offset family),
  and `CallVtableAsClass` — which the scan called "used only by
  LogTargetDiag" — is also called by the operational
  `IsCreatureClientTarget`. Making it work means publishing a batch of
  offset constants through an internal header for what the report itself
  rated the smallest structural win in the list. Cost/benefit inverted,
  so it was reverted rather than pushed through. Tree clean, build green.
- **Candidate 23 (menus_listbox picker split) — EXECUTED 2026-07-29,
  NARROWED.** The state measured clean: every reference to the six picker
  statics fell inside four picker-owned clusters (accessors, the two spec
  callback blocks, the two monitors). Nothing outside touched them — so
  this was *not* the category that broke candidates 13 and 24.

  The cut was narrowed anyway, on a different cost the original approval
  had not priced in. Moving the two SPECS out (the approved ~575-line
  version) would have forced `ListBoxPanelSpec` — deliberately private,
  16 fields, mostly documentation — into a shared header, and left the
  dispatcher's 13-entry probe table pointing at two entries in another
  file. So what moved is the state, `ParkPickerCursorOffList`, and the
  two picker monitors; all thirteen specs stayed together.

  `menus_listbox.cpp` 1982 → 1691, new `menus_listbox_picker.cpp` 305.
  Zero new headers: the spec callbacks reach the state through the
  accessors `menus_listbox.h` already published for menus.cpp, plus one
  addition (`WorkbenchUpgradePickerPanel()`) and a `TickPickerMonitors()`
  fan-out. Two knock-ons: routing the stale-reset paths through
  `Disarm*` now also clears the park-pending latch (the old inline
  version left it set — the panel is gone, so clearing is correct), and
  `menus_listbox.cpp`'s local `kWorkbenchUpgradeLbId` was dropped in
  favour of the identical `kWorkbenchUpgradeLbItemsId` that already
  existed in `menus_internal.h`. Build green, zero warnings.
  NOT yet verified in-game — see the caveat at the top of this file.

- **Candidates 10 + 28 are one job, and it belongs in Phase 2, not here.**
  They are coupled: the engine_offsets.h split (10) only pays for itself
  once includers move to the narrow headers (28). Doing 10 alone buys
  header navigability at the cost of the batch's largest diff.
  The stronger reason to wait: `engine_offsets.h` is the single most
  K1-specific artifact in the codebase — on a KOTOR 2 port every value in
  it changes. Phase 2 carries the K2-portability lens, so that is where
  the right cut lines get decided (which offsets are engine-version
  facts, which are structural, where the abstraction seam goes). Cutting
  it into arbitrary subsystem headers now would mean re-cutting it in
  Phase 2 along different lines. 28's includer migration then falls out
  of Phase 3's per-file sweep, which visits every one of those files
  anyway.

## Decisions log

- 2026-07-27: Infrastructure created; state file lives in-repo (survives
  sessions), moves to archiev/ at wrap-up.
- 2026-07-27: **Scope = everything**: patches/Accessibility (full treatment),
  tools/kdev, and installer/KotorAccessibilityInstaller. User notes the
  installer already had recent prep/cleanup work, so expect fewer findings there.
- 2026-07-27: **Merge strategy: regular merge commit** (keep branch history
  for bisectability).
- 2026-07-27: **Phases 3+4 merged into ONE per-file sweep** with two explicit
  checklists — general low-level cleanup AND a dedicated AI-pattern search per
  file. Report keeps two separately approvable candidate sections. (Phase
  numbering below unchanged; "Phase 3+4" is a single sweep.)
- 2026-07-27: All plan suggestions from the 2026-07-27 report approved by user
  (K2-portability lens in Phase 2, Phase-0 baseline, do-not-touch list,
  approval workflow, Phase-1 items may defer to Phase 2).
- 2026-07-27: Code-index refresh approved: full re-index of ALL patch files
  (stale entries too), plus NEW index coverage for kdev (code-index/kdev/) and
  installer (code-index/installer/). ~15 Sonnet agents.

## Codebase snapshot (2026-07-27)

- patches/Accessibility: 264 files — 130 .cpp, 128 .h, hooks.toml (1214 lines),
  allard.hooks.toml, manifest.toml, exports.def. ~86k lines total.
- Biggest files: wall_topology.cpp 3404, menus.cpp 2269, turret_game.cpp 2072,
  strings.h 2003, menus_listbox.cpp 1982, engine_area.cpp 1901,
  menus_extract.cpp 1896, menus_chain.cpp 1827, engine_offsets.h 1820.
- Module prefixes (cpp counts): menus 23+, engine 17, strings 6, probe 6,
  guidance 5, combat 5+, audio 5, map 4, diag 3, core 3, camera 3, plus
  singletons (wall_topology, transitions, pazaak, prism, log, ...).
- Localisation: strings_{en,de,fr,it,es,ru}.cpp ~850 lines each, keyed via
  strings.h Get(Id).
- Prior structural work already done: menus.cpp refactor (5327→1906 across
  8 TUs, see memory project_menus_refactor_plan) — reuse its seam patterns.

## Session log

- 2026-07-27 (session 1): Branch + infrastructure created. Plan-improvement
  suggestions delivered to user; awaiting decisions.
- 2026-07-27 (session 1, cont.): User decisions recorded (scope=all, merge
  commit, phases 3+4 merged with explicit AI-pattern checklist). Build baseline
  captured (0 warnings). Full code-index refresh executed and verified.
  Phase 0 COMPLETE. Next action: start Phase 1 (structure audit) — user said
  to report candidates before executing anything.
- 2026-07-27 (session 2): Phase 1 structure scans executed — 3 Sonnet agents
  (menus/engine/strings; rest of patch; kdev+installer), within the ≤3-agent
  no-go-ahead limit. Scan reports: phase-1-scan-a/b/c under reports/.
  Cross-cutting checks in main session: build glob is flat (subfolders in
  patches/ would need kdev BuildCommand change + break --bat), header
  hygiene good, fan-out hotspots quantified, kdev/installer code index
  verified complete. Consolidated report + numbered candidate list written:
  reports/phase-1-structure.md (28 candidates in 5 batches, plus
  rejected-list and Phase-2/3 carry-forwards). Notable: probe_priority_groups
  confirmed dead in practice; combat_diag hosts a shipped production hook;
  strings.h split explicitly rejected. NO code changed. Awaiting item-by-item
  user approval.

## In-game smoke-test checklist (Phase 1 execution, 2026-07-28)

Everything below built clean, but a clean build is not a working mod. Test
these before merging. Each line names the commit(s) to bisect to if it
misbehaves.

1. **Menu navigation** — arrow through inventory, a dialog, chargen, and one
   listbox screen (save/load or container). Titles, focus announcements and
   Enter activation must sound exactly as before.
   → Refactor(1) menus split, Refactor(2) chain input.
2. **Leader announce + input restore** — Tab between party members; run a
   dialog or scripted action and confirm control comes back afterwards.
   → Refactor(5) engine_player split.
3. **Combat announces + queue** — fight something. Attack/damage/absorb
   lines, and the "X, Platz N" / "Warteschlange voll" queue announces.
   → Refactor(7) combat_log, Refactor(14) combat_queue_hooks.
4. **Region and landmark narration** — walk a nav-heavy area (Taris Upper
   City or the Sewers). Korridor / Kreuzung / Bereich labels, door and exit
   descriptions, landmark announces.
   → Refactor(12,12b) wall_probe + room_topology rename.
5. **Hotkey routing** — Shift+N action bar, examine view, combat queue,
   unified action menu, bare 1-7, bare R, Enter/Shift+Enter interact.
   Order matters: each should still win the key it used to.
   → Refactor(19) input_poll_router.
6. **Minigames** — a swoop race and a turret sequence, briefly.
   → Refactor(20) minigame_* renames (file renames only, but they touched
   every includer).
7. **Focus guard** — alt-tab away and back; confirm the keyboard still
   works. → Refactor(21) focus_guard rename.
8. **Installer** — a full install and an uninstall pass before the next
   release. → Refactor(17) installer split. This one is end-user facing;
   do not ship without it.

Areas deliberately NOT touched and not needing a test: input_pipeline.cpp,
hook addresses/byte patterns, engine_offsets.h values, exports.def.

## Phase 2 status (2026-07-29)

Branch `refactor/phase2-coupling`, from main @ 4c4e216. Build green
throughout (194 TUs, 0 warnings); kdev and installer build 0/0.

**Executed:**
- A1-A3 state ownership — transitions per-group Reset()s (17 variables
  verified preserved), SnapshotDoors owns landmarkName, corrected the
  dispatch-order comment that contradicted core_tick.
- B4-B6 duplication — minigame SEH primitives + ResolveMgoArray +
  CallAsCast + follower-position read consolidated into minigame_aim;
  engine_panels_internal.h AppManager constants folded into
  engine_player.h's; kHoverPauseMs published from view_mode.h.
- B7 kdev — BWM parsing hoisted to Core/BwmFile.cs (3 consumers).
- C9, C11 — K2 portability recorded in docs/llm-docs/CLAUDE.md.
- C8 engine_offsets.h four-way split (2026-07-29) — see the status block
  at the top of this file.

**Findings that changed the plan:**
- C10 was a FALSE ALARM. audio_bus.h's kAddrCExoSoundPtr is a .data
  global; R() covers .text only, so wrapping it would have been a bug,
  not a consistency fix. Documented in place. Also recorded: R() is a
  same-game build-variant seam and does nothing for a K2 port.
- C11 found the upstream AddressDatabases are real and K2 is SEEDED —
  both K2 dbs carry all 14 global pointers under K1's names. User decided
  to adopt the mechanism; this reshaped C8's design.

- C8 finding, now FIXED: the spec's "19 unwrapped .text addresses" was a
  miscount — engine_offsets.h had none. 12 real ones existed elsewhere and
  were provably wrong on the Allard Russian build. Root cause was kdev's
  address-harvester regex, not the header. Harvester widened, rebase table
  regenerated against the Allard exe, all 12 wrapped and guarded. Full
  analysis in `reports/phase-2-cleanup.md`.

**Next action: the in-game smoke test** (see the status block at the top of
this file for the per-build list). Nothing in Phase 2 is code-blocked.

**Also open:** candidate 23 (menus_listbox picker, carried from Phase 1),
C4 (doorMatched split, deferred to Phase 3), candidate 28 (includer
migration, falls out of Phase 3).

**Untested in game:** everything in Phase 2. B4 rewired the read
primitives all three minigames use — a short swoop race and turret
sequence is the check. A1/A2 touch room + landmark narration.

## Phase 3 status (2026-07-29) — SCAN COMPLETE, NOTHING EXECUTED

Merged Phase 3+4 per-file sweep (general cleanup + AI-pattern pass, two
separately approvable sections). User approved the full-sweep option.

**Method:** 24 Sonnet agents, batched by line count, read-only on the
codebase, each writing only its own report. Coverage was complete: all
362 files / ~103,000 lines (287 patch, 30 kdev, 45 installer), every file
assigned to exactly one batch. Per-batch reports are `reports/phase-3-*.md`
(~9,000 lines total). Consolidated candidate list:
**`reports/phase-3-sweep.md`** — read that, not the 24 individual reports.

**Nothing was executed. No source file was modified.**

### Headline conclusions

1. The Phase-1 splits were executed but never cleaned up after: ~120 dead
   includes, ~26 dead using-declarations, stranded declarations, stale
   file banners. All compiler-verifiable.
2. Phase 2 created shared helpers but did not migrate callers —
   `FindPanelByKind`, `Core/BwmFile.cs` (imported by all 3 consumers,
   zero constants used, 22 raw hex literals remain), `kHoverPauseMs`.
   "Finish Phase 2" is a real, bounded piece of work.
3. The AppManager→(Client/Server/Gui/Camera) resolve chain is hand-walked
   at ~20 sites across 8+ files. Biggest architectural finding, and the
   most K2-relevant — no single seam to change on a port.
4. Engine reads missing the SEH guard their siblings use: 4 independent
   batches, bounded to panel/control-reading paths.
5. Six-language localisation has holes in fallback/startup paths,
   including the startup greeting every player hears.

### Verified by hand in the main session (not taken on trust)

Two agent claims were CORRECTED: the mojibake count/severity (52 lines,
49 in comments, cosmetic — not 48 text-corruption instances), and
`strings.h`'s claim that `lang_fr` aliases `lang_en`, which is FALSE —
the French table is 651 real entries. That stale comment was handed to
the agent as fact in its brief; it checked anyway.
Also verified: the F1 linked-list hop, the F2 guard gap and its local
convention, F3's silent installer status, the `Get()` never-null
contract (~20 dead null-checks, not 30+), the BwmFile non-adoption, and
that `kHoverPauseMs`'s two copies agree (drift risk, not a live bug).

### Also settled this phase

- **C4 has a concrete proposal**: `IterateLandmarks` already receives the
  `area` and discards it; thread it through and gate the cache read, one
  caller to update. Turns a silent stale-cache failure into a logged one.
- **Candidate 28's premise in this file was partly WRONG.** Only
  `engine_offsets.h` has narrow siblings. `engine_player.h` /
  `engine_area.h` / `engine_panels.h` / `engine_reads.h` are single
  headers — Phase 1 split the .cpp files only. There is nothing to
  migrate to for those four; splitting them is NEW work, not a migration.

### NEXT ACTIONS, in order (2026-07-29 — all three since done)

1. **Phase-2 in-game smoke test — this gates Phase-3 execution.** Nothing
   in Phase 2 has been played. Stacking a per-file sweep on top of an
   unverified Phase 2 makes any later in-game failure much harder to
   bisect. Priority list is in the "SESSION END 2026-07-29 (afternoon)"
   block above (equipment picker, workbench arm/close/reopen, loot
   container, bark bubble, galaxy map, tutorial popup).
2. **Walk the Phase-3 candidate list with the user item by item** (per
   the standing rule — bulk approval lists do not work here).
3. **Add an encoding check to the execution protocol** before executing
   anything (see P3-A9) — the mojibake is isolated to the one file a
   recent edit rewrote, so our tooling can reintroduce it.

## Phase 3 Section A — EXECUTED 2026-07-29 (11 commits, da24fac..d6123e0)

User lifted the Phase-2 smoke-test gate (Phase 2 was tested in the interim)
and approved Section A as cleanup not needing per-item approval.

Verification: `kdev build --clean` = 195 TUs, 0 warnings. kdev and installer
`dotnet build` = 0 errors. Every candidate built green before commit.

Executed: A1 (118 dead includes, 34 files), A1b (input_pipeline's 2, own
commit), A2 (27 dead using-declarations + the 3 seam comments describing
them), A3 (dead functions/constants), A4 (37 dead string Ids across enum +
6 tables), A5 (30 narrowed Get() guards), A6 (8 raw hex -> named constants),
A7 (local redeclarations), A8 (stale comments), A9 (55 mojibake chars),
A11 (4 linkage fixes).

### Three scan claims were WRONG and the build/verification caught them

1. A1: three "dead" includes were live — engine_manager.h in menus_chain.cpp
   (IsPanelLive reads kAddrGuiManagerPtr directly; the read IS the usage),
   <type_traits> in prism.cpp, engine_reads.h in combat_query.cpp. Restored.
2. A5 was mis-framed as "dead null-checks". The guards are compound
   `if (!x || !x[0])` and the EMPTY-STRING half is load-bearing (Get() does
   return "" for unmapped ids). Deleting the whole guard would have been a
   bug. Only the null half was removed, scoped strictly to Get()-assigned
   variables — an identical guard on a ReadCExoString result IS a real null
   check.
3. A7 contained a live trap: map_ui_cursor.cpp declared
   `kWaypointHasMapNoteOff = 0x22c`, but the canonical constant of that NAME
   is 0x228. Substituting by name would have silently changed behaviour.
   Substituted by VALUE (kWaypointMapNoteEnabledOffset).

### NOT executed from Section A, deliberately

- **A10 (leftover diagnostics)** — the combat_special_watch logging is
  evidence for bug F1, which is still open. Removing it would destroy the
  trace for a live bug. The other three sites are still available.
- **A12 (per-tick recompute)** — a behaviour/performance change, not
  cleanup. Needs its own decision.
- **probe_camera_state.cpp's 4 unused offset constants** — offset constants
  are on the do-not-touch list; an unused one is a question, not a removal.
- **core_settings.h pillar structs** — they ARE referenced as members of a
  parent struct; "zero consumers" meant their fields are unread, which is a
  different and non-mechanical change.
- **minigame_swoop_audio.cpp design commentary** — the constants were
  deleted but the broader design prose was left, since that file is under
  active tuning. One residue: the settle-gate paragraph now describes a gate
  with no implementation.

### NEXT: the 9 possible bugs (Section F) were reported to the user.
Section B (structural, 7 items) is untouched and still needs approval.

## Phase 3 bugs F1-F8 — FIXED 2026-07-30 (6 commits, 890fa07..17337dc)

User approved F1-F8 and REJECTED F9 (gated dialog replies): there are no
unavailable dialogue options in KOTOR 1, so a gating cue would produce false
positives rather than catch edge cases. F9 is closed, not deferred.

**F1 — combat action-list walk, one deref short.** CountSpecialsForCreature
walked list -> internal then read the internal header as an action node.
Now three derefs, matching combat_queue.cpp. Also deleted the
kLinkedListHeadOffset alias that hid the bug (defined equal to
kListInternalOffset, zero remaining users).

**F2 — six unguarded engine reads.** SEH guards added to
HasActiveDialogPanel, HasActiveSubScreen (engine_panels_state),
GetForegroundPanel (engine_manager), FindActiveDialogPanel (dialog_speech),
ReadControlNameFields (engine_reads - highest risk, called straight from the
OnHandleFocusChange detour with an unchecked `this`, and it now initialises
its out-params), FindMatchingPanel (menus_editbox, a TU with no SEH anywhere).
Plus menus_journal ForceRepopulate gained the acc::addr::Ok() rebase check
its sibling uses.

**F3-F6 — installer accessibility.** ValidatePath now routes through
UpdateStatus (new isError overload) so a wrong folder is SPOKEN, not just
turned red; the enabled/disabled transition is announced via two new locale
keys in all six JSON files; the Install button's AccessibleDescription is
refreshed instead of captured once; UninstallForm announces progress;
Escape works in all four dialogs (UninstallForm gets CancelButton ONLY -
Enter must not start a destructive action; WelcomeForm handles Escape in
ProcessDialogKey rather than adding a phantom button, and sets AcceptButton
per page); three ProgressBars got AccessibleName.

**F7 — kdev short-file crash.** WalkmeshFaceTypesCommand single-file mode now
uses BwmFile.HasValidHeader (length THEN magic) plus BwmFile's named offsets.
Verified against a real 1-byte file: clean error, exit 1. This also closes
part of the "Phase 2 helpers never adopted" finding, in the file where the
gap was actually crashing.

**F8 — hardcoded speech.** Five new Ids in all six tables (CP1251 generated
for Russian). The startup greeting - the first thing every player hears - was
hardcoded English. Chargen spoke the developer tag "BTN_BACK" on a failed
button read, and a hardcoded "Talent %u". All LOCALISED, not removed, per the
never-silence-a-fallback rule. The two probe_* Speak calls are deliberately
left English and now commented as developer-facing RE tooling.

Verification: `kdev build --clean` 195 TUs 0 warnings; kdev + installer
dotnet build 0 errors.

### NEEDS IN-GAME / MANUAL VERIFICATION (none of this is play-tested)
1. F1: fight, queue a special (power/grenade/medpac) on a party member,
   confirm the "you can act now" cue stays quiet while specials pend and
   fires on the transition to none pending.
2. F2: normal menu nav; a conversation interrupted by an area transition;
   the chargen name editbox. Guards must be invisible in normal use.
3. F8: launch and listen to the greeting in German; force a chargen feat
   button-text read failure if practical.
4. F3-F6 with a screen reader: browse to a wrong folder (reason spoken +
   button state announced), run an uninstall (progress spoken), press Escape
   in all four dialogs.

## Phase 3 Section B — B1 EXECUTED 2026-07-30 (4 commits, b2627cd..c7b5eab)

User approved B1 with **option 1: per-chain slices, four commits**, after a
walk that put four options (per-chain slices / server-only-then-re-decide /
one big commit / defer to the K2 port).

**The scan undercounted every chain.** Reported "~20 sites across 8+ files";
actual was ~40 walk sites across ~25 files, six different names for the
AppManager `+0x8` hop, three private copies of `kAddrAppManagerPtr`, five
copies of the module offset and four of the camera offset.

**New `engine_app.{h,cpp}`** owns the whole walk: `kAddrAppManagerPtr`, the
client/server facade hops, both facade→internal hops, the module and camera
hops, and seven SEH-guarded primitives (`GetAppManager`, `GetClientApp`,
`GetClientAppInternal`, `GetClientModule`, `GetCamera`, `GetServerApp`,
`GetServerAppInternal`). The GUI continuation went into `engine_panels`
instead, next to the `ResolveGuiInGame` that was already published there:
`ResolveMainInterface()` is new.

- **Slice 1 (server)** — engine_area, engine_area_map, engine_reads_items,
  engine_player_party, engine_subscreen, tutorial_popup,
  minigame_swoop_race. Two byte-identical private `GetServerApp` copies
  deleted.
- **Slice 2 (client)** — 15 files. **Found a real defect**: `ResolveGuiInGame`
  had NO SEH guard at all — three raw dereferences called from panel code
  that runs during teardown, the exact F2 shape. It inherits the seam's guard
  now.
- **Slice 3 (GUI)** — the four-function quartet duplicated verbatim across
  engine_radial / engine_actionbar / engine_picker: twelve functions and
  seven offset copies deleted, 199 lines gone for 56 added.
  `acc::engine_actionbar::ResolveMainInterface()` kept its name (nine callers
  including input_pipeline.cpp) and became a one-line forward.
- **Slice 4 (camera)** — engine_player's two camera readers, camera_orient,
  probe_camera_distance, probe_camera_state. Two now-dead `SafeDeref`
  helpers fell out.

**Invariant now true and worth re-checking with grep:** exactly ONE
dereference of `kAddrAppManagerPtr` exists in the codebase, and zero uses of
the hop constants outside `engine_app`.

Verification: `kdev build --clean` 196 TUs, 0 warnings. Encoding checked per
the A9 rule (em-dashes byte-identical to the rest of the tree).

### B1 IS VERIFIED IN GAME (2026-07-30) — user tested and reported working.
DLL freshness was confirmed before the test (installed accessibility.dll
11:18:48, built from HEAD f259ea5), so the stale-DLL trap does not apply to
this result. The list below is what was covered.

### In-game coverage that was run
This touched nearly every subsystem the player hears. Worth covering:
1. Slice 3 is the highest risk: radial menu, action bar (bare 1-7 and
   Shift+N), and the picker / Enter-interact path.
2. Leader announce + Tab, party switching, input restore after a dialog.
3. Camera: N-key orient-to-cardinal, compass announces.
4. Combat announces and the queue; journal Enter; examine view.
5. A swoop race (slice 1 touched the race timer read) and any minigame.
6. Pause paths: open an in-game menu and Esc back out; a tutorial popup.

## Phase 3 B3 — EXECUTED 2026-07-30 (2 commits)

Approved as "option 2": do both halves, as two commits. **Two of B3's three
reported items were not what the report said.**

**B3 item "kHoverPauseMs local duplicate" — ALREADY DONE.** Section A's A7
removed it. `view_mode.h` defines it once; `view_mode.cpp` and
`map_ui_cursor.cpp` both use that one. Struck, nothing to do.

**B3a (BwmFile constants) — smaller than reported.** The report said 22 raw
literals across 3 consumers; F7 had already fixed one while closing a crash,
so 14 across 2 files remained. Both now read through `MinLength` / `Magic` /
the six `Off*` constants. `WalkmeshStatsCommand` deliberately keeps TWO checks
instead of `HasValidHeader()` so its error still says WHICH way the file was
wrong. Also deleted the stale "kept local ... hoist if a third walkmesh
consumer appears" note that survived the exact hoist it warned about.
Verified beyond the build: output on real extracted m01aa walkmeshes is
byte-identical before and after (diff clean), and both commands still give a
clean warning rather than a stack trace on a 1-byte and a 22-byte garbage file.

**B3b (panel array) — the report's framing was WRONG, and the truth was
better.** It said "adopt FindPanelByKind". The surviving scans CANNOT adopt
it: they are predicate scans (`IsDialogPanelKind`, `FindSpec`), full
iterations (`MonitorPanelContents`), or control-list searches
(`FindOwningPanel`) — not single-kind lookups. What actually repeated was the
panel-array ACCESS: manager deref, two offset reads, null-check, clamp.
**18 sites across 8 files, 8 of them with no SEH guard** — including
`IsPanelInManager` and `FindOwningPanel`, which sit directly above
`GetForegroundPanel` in the same file, reading the same array, where the
guarded version's comment spells out the hazard verbatim.

`engine_manager` now publishes `GetGuiManager()`, `ReadPanelArray()` and
`ReadModalStack()`. They COPY into the caller's buffer inside the guard, so
callers iterate their own memory — strictly safer than the old guarded sites,
which looped over engine memory inside the `__try`.

**The cap discrepancy — surfaced, then RESOLVED on the user's call.** The old
sites disagreed: most clamped panels[] to 16, four to 32, so a panel at index
17-31 was visible to some of our queries and invisible to others. Checked
against every `panels.size` our own diagnostics ever logged: normal play never
binds (1/3/5 dominate, 99.9% of samples ≤9), BUT
`patch-20260530-112606.log` recorded panels.size climbing 17→27 with
modal.size 14→24 inside one second. So the divergence was real, not
theoretical. **Unified on 32 everywhere** (13 sites raised from 16) — it is a
128-byte stack buffer and can only let a query see more of the truth.
The user explicitly declined to chase the 2026-05-30 growth itself ("won't
care about a month-old one-time bug until it appears again"); it has not
recurred in any later log. If it does, that log is the starting point.

One thing deliberately NOT normalised:
1. **`GetForegroundPanel` was left alone.** Its last-resort return indexes
   `panelData[panelSize - 1]` using the RAW size while its scan covers only
   the first 32 — above 32 the fallback reaches outside its own scan window.
   Migrating would have quietly changed that. It is already guarded (an F2
   fix), so it is not part of the class B3b closes.
Also out of scope and left: the "top of modal stack" reads in
engine_panels_state, engine_subscreen (2), input_pipeline and view_mode —
those index the stack top, they do not iterate.

### B3 IS VERIFIED IN GAME (2026-07-30) — user tested and reported working.
DLL freshness confirmed first (installed 11:47:31 from the 11:46:06 kpatch,
HEAD 9bfa806). Coverage run is the list below.

### In-game coverage that was run
1. Arrow through several menus; sub-screen entry announces (MonitorPanelContents,
   FindActiveSubScreenPanel).
2. A conversation with replies — the dialog-reply monitor.
3. The chargen feats picker (FindFeatsCharGenPanel) and a name editbox
   (menus_editbox's FindMatchingPanel).
4. A bark bubble, the in-game map, a level-up.
5. Esc from an in-game menu back to the world (foreground/modal routing).
B3a is CLI-only and was verified by output diff; it needs no in-game pass.

## Phase 3 B4 — EXECUTED 2026-07-30 (installer UIA helper)

The report said four copies; there are now FIVE — F5's fix added the
UninstallForm one, so the bug fix itself grew the duplication it was caused by.

**Only the notification block was extracted, because only it is identical.**
All five copies of the `RaiseAutomationNotification` call are byte-for-byte
the same: same kind (ActionCompleted), same processing (MostRecent), same
try/catch, same warning text. The `UpdateStatus` wrappers around them are NOT
interchangeable and stayed per-form — they differ in `Invoke` (synchronous)
vs `BeginInvoke`, whether the message also goes to the install log, and
whether the caller can suppress the announcement via an `announce` flag.
Folding those together would have been a real behaviour change on the
threading model.

New `ScreenReaderAnnouncer.Announce(Control, string)` (flat top-level file,
matching the installer's one-class-per-file layout). Five call sites, five
dead `using System.Windows.Forms.Automation;` removed.

`MainForm.RaiseNotification` was kept as a named method rather than inlined:
it has a direct caller at MainForm.cs:228 that announces the Install button's
enabled/disabled transition without changing status text (the F6 work).

**Invariant now checkable by grep, and it is the point of the item:** there
are exactly five `_statusLabel.Text =` writes in the installer and exactly
five `Announce` call sites, one per form, each inside the same method. A form
that adds a status write without an announcement is now visibly odd. F3 and
F5 were both "a copy that was never made", and silence is the one failure a
blind user cannot notice.

Verified: F3's `ValidatePath -> UpdateStatus(message, isError: notFound)`
routing, F4's `RefreshAccessibleDescription`, and F6's enabled/disabled
announcement all still in place. `dotnet build --no-incremental` 0 errors,
warning baseline unchanged (1 pre-existing installer warning in
PriorityGroup2da, 3 in third_party KPatchCore).

### NEEDS MANUAL VERIFICATION with a screen reader (not play-testable)
Same pass F3-F6 already owed, now also covering B4:
1. Browse to a wrong folder — reason spoken AND the Install-disabled
   transition announced.
2. Run an uninstall — progress spoken.
3. A TSLRCM / KOTOR2-mods / workshop-TLK run if convenient — those three
   forms' status updates went through the same extraction.

## Phase 3 B6 — PARTIALLY EXECUTED 2026-07-30 (2 commits: 6767107, cfe54e5)

B6 was nine independent sub-items. Six executed, one rejected on evidence,
two held back for a user decision.

**EXECUTED, patch side (6767107):**
1. `CResRef` / `FillResRef` — audio_loop's copy literally commented itself as
   "Local mirror of the 16-byte tag from audio_bus.cpp". Declared once in
   audio_bus.h (already included there). NB: first attempt put it at file
   scope *after* the namespace closed, which made the call ambiguous against
   the `acc::audio` definition — the compiler caught it.
2. `CategoryNameId` — diffed byte-identical in view_mode / passive_narrate,
   moved next to `CategoryName` in filter_objects (same mapping, other
   direction: English for logs, localised for speech).
3. cycle_input's 4-line activation cue, spelled out at all four activation
   paths → `PlayActivationCue(a)`. `bindings` had no other use at any of the
   four, so the lookup collapsed with it.
4. menus_pending's conditional is_active raise, inline at SEVEN sites →
   `RaiseIsActiveIfZero`. **The reason this mattered:** the rule is
   raise-only-0→1 (tab and equip-slot buttons carry engine bookkeeping there;
   clobbering it crashed the game in an unrelated subsystem a frame later).
   The rationale was full at the first site, abbreviated in the middle, and
   ENTIRELY ABSENT by the last copy — exactly the drift that reintroduces an
   unconditional write.
5. `ControlHasVtable` → `HasVtable`, adopted by the five standalone panel
   detectors that hand-rolled it. Six further comparisons left alone: inside
   larger detectors with compound logic and an enclosing `__try`, so
   converting would nest a redundant guard.

**EXECUTED, installer (cfe54e5):** the ~65-line hand-rolled JSON parser →
`System.Text.Json` (already a dependency via GitHubClient). Because these six
files supply every string the installer speaks, equivalence was PROVED, not
assumed: a scratch harness ran both parsers over all six locales and compared
key-by-key — **1146 keys, zero differences**. Side finding, checked and
cleared: en/ru have 195 keys vs 189 for de/es/fr/it, but all six extras are
`Russian_*` notices shown only on a Russian install, with the fallback chain
covering the rest. Deliberate. One behaviour difference recorded in the
commit: malformed input used to yield a partial dictionary, now yields an
empty one plus a warning and English fallback — louder, and better.

**REJECTED on evidence — K1cp / K2cp "skeletons".** Not near-duplicates:
K1cpInstaller is 293 lines with translation-overlay logic, K2cpInstaller is
120 and its own docs say "NOT yet part of any pipeline the installer runs",
gated on two prerequisites. Abstracting a common base over a shape that is
not finished would have to be redone when K2CP is actually wired up.

**HOLD-BACKS — user approved 2026-07-30; both EXECUTED with test harnesses.**
Both write to a real game install, so neither was taken on a clean build
alone. Harnesses live in the session scratchpad (`initest`, `introtest`);
they copy `SwkotorIniTweaker.cs` / `IntroMovieDisabler.cs` next to a stub
Logger, so they can be recreated in minutes if either file changes.

- **`SpatialAudioManager.SetEaxValue` → `SwkotorIniTweaker.ApplyEaxSetting`.**
  The tweaker's general routine (`ApplySectionPairs`) was private behind three
  `Apply*Defaults` wrappers; added a fourth, parameterised because EAX is a
  toggle rather than a default. File 249 → 184 lines. The write step was
  already byte-identical in both (`\r\n` + `UTF8Encoding(false)`), which is
  what made the swap safe. Verified against a COPY of the real swkotor.ini
  across four cases — key present/changed, present/already-correct, key
  missing, whole section missing — plus an integrity check: writing EAX=0 to
  the real 166-line file changes EXACTLY ONE LINE and preserves length.
  One behaviour improvement recorded: the tweaker skips the write entirely
  when nothing changed, where the old copy rewrote the file every time.

- **`IntroMovieDisabler` Disable/Restore merged into `MoveIntros(disable:)`.**
  242 → 190 lines. `DisableIntros` / `RestoreIntros` keep their names and
  signatures (one caller each: MainForm, UninstallFlow). The pair was
  perfectly symmetric — same five-branch shape, differing only in which name
  is the source — which is exactly where a fix lands on one side only.
  Verified across NINE states: vanilla→disable, disable→disable (idempotent),
  disabled→restore, restore→restore, BOTH-forms-present in each direction
  (the mid-state a failed run leaves behind), no intro files at all, missing
  Movies folder (must fail with a message), and a full round trip that has to
  return the folder to vanilla exactly. All pass. The real install was not
  touched by the harness — confirmed afterwards.

### NEEDS IN-GAME VERIFICATION (B6 patch side)
1. Activation cues: `-`, Shift+-, Ctrl+-, Alt+- on a door, an NPC and a
   container — the cue must still fire before speech, at the target.
2. Menu activation: Enter on a MessageBox OK, an options tab, an equip slot,
   a workbench slot and its assemble button (the seven is_active sites).
3. Any panel identification: save/load, workbench, level-up, main menu,
   pazaak start + wager popup (the five vtable detectors).
4. A looping sound and a one-shot cue (the CResRef move).
Installer side needs no game test; the locale swap is proved by the harness.

### Section B remaining: B2, B5, B7 (3 items). B6 is fully closed.
B3 was flagged in the report as the highest value-to-risk ratio; B4 should be
done with the F3/F5 fixes that are already in (the helper extraction that
would have prevented them).

## SESSION END 2026-07-30 — Section B, four items closed

Eleven commits, `b2627cd..HEAD`. All four items were play-tested by the user
in the same session they were written, each against a freshness-checked DLL.

**What was done:** B1 (engine resolve seam, 4 slices), B3 (BwmFile constants
+ panel-array accessor + cap unification), B4 (installer announcement
helper), B6 (nine sub-items: six executed, one rejected, two hold-backs
later approved and executed with test harnesses).

### The finding that should shape how B2/B5/B7 are approached

**Every single Section B item's report entry was wrong in some way**, and in
three of four cases the correction changed what the right fix was. Verify
before proposing, every time:

- **B1** — report said "~20 sites across 8+ files". Actual: ~40 sites across
  ~25 files, six names for one hop, three copies of the root pointer.
- **B3** — reported as "adopt FindPanelByKind". The surviving scans CANNOT
  adopt it; they are predicate scans and whole-array iterations. The real
  finding was the array ACCESS, duplicated 18x with 8 unguarded. Also, one
  of B3's three sub-items (`kHoverPauseMs`) was already done by Section A.
- **B4** — said four copies; there were five, because F5's own fix added one.
- **B6** — said 22 raw literals across 3 BwmFile consumers; F7 had already
  fixed one, leaving 14 across 2. And the "K1cp/K2cp skeletons" sub-item was
  rejected outright: K2cp is a 120-line stub its own docs mark as not wired
  up, against K1cp's 293 lines.

### Two defects found by refactoring, not by looking for bugs

- `ResolveGuiInGame()` had **no SEH guard at all** — three raw dereferences
  called from panel code that runs during teardown. Exactly the F2 shape,
  invisible to the per-file scan because each caller looked fine alone.
  Fixed by construction in B1 slice 2.
- **8 of 18 panel-array walks were unguarded**, including two sitting
  directly above `GetForegroundPanel` in the same file, reading the same
  array, where the guarded neighbour's comment states the hazard outright.
  Fixed by construction in B3b.

Both are the "closes F2 by construction" the report predicted, and neither
would have been found by reading the files individually.

### Behaviour questions surfaced and deliberately NOT auto-resolved

- **Panel-array cap** (16 vs 32) — raised as a question, evidenced from the
  logs (normal play ≤9, but `patch-20260530-112606.log` hit 27), then
  unified on 32 on the user's explicit call. The user declined to chase the
  underlying 2026-05-30 panel growth: "won't care about a month-old one-time
  bug until it appears again." That log is the starting point if it recurs.
- **`GetForegroundPanel`'s raw-size fallback** — indexes
  `panelData[panelSize - 1]` while its scan covers only the first 32, so
  above 32 it reads outside its own window. Left alone and recorded; it is
  already guarded, so it was not in the class B3b closed. **Still open.**

### Method notes worth reusing

- Two throwaway harnesses proved equivalence where "it should be fine" was
  not good enough, both in the session scratchpad and both cheap to rebuild:
  locale JSON (1146 keys, zero diffs, across all six languages) and the
  intro-movie rename (nine folder states including both mid-states and a
  round trip). The INI change got the same treatment against a copy of the
  real swkotor.ini — one line changed out of 166.
- `git add -A` swept an installer change into a commit whose message said
  "patch side". Caught and split with `reset --soft`. Stage explicitly when
  two areas are in flight.

## Phase 3 B2 — EXECUTED 2026-07-30 (5 commits, 6acf591..ac843e7)

User approved three of the sixteen candidates (FromControl, RebindChain,
HandleInputEvent) and then OVERRODE the recommendation to defer
room_topology: "the comment is old and it becomes less likely, and even
then I will change against a better base than we have at the moment,
that's still a win. Core feature should be code-side very well as well."
So BuildForArea and ClassifyCluster went in too. `DriveSelectedPeg` stays
rejected; the remaining ten candidates are untouched.

**B2's report entry was ACCURATE** — the first Section B item where it
was. All sixteen reported line counts matched the code within a line or
two. What was wrong was the report's RISK framing, in both directions:

- `FromControl` was called the most hazardous ("an early `return nullptr`
  mid-function needs explicit tri-state handling"). It was the easiest of
  the five: five return statements in 1472 lines, two top-level locals,
  and a uniform `if (!source) { ... }` ladder across 19 sections.
- `DriveSelectedPeg` was called "8+ jobs". It is one data-dependency
  pipeline (position -> lead solution -> aim error -> hitbox -> cue ->
  steering -> sign calibration), each stage consuming the previous
  stage's floats. Rejected on that basis, and it should stay rejected.

### Results (all bodies moved mechanically, not retyped)

- `FromControl` 1472 -> 69, menus_extract.cpp
- `RebindChain` 682 -> 93, menus_chain.cpp
- `HandleInputEvent` 406 -> 127, unified_action_menu.cpp
- `BuildForArea` 804 -> 156, room_topology.cpp
- `ClassifyCluster` 504 -> 192, room_topology.cpp

### Method that made this safe, and is worth reusing

Every body was SLICED BY LINE RANGE with a shell script and re-wrapped,
never retyped. Then a code-line diff (comments and whitespace stripped,
sorted, `comm`) proved that the only differences were the intended ones.
That caught nothing wrong in four of five files and came back completely
empty for ClassifyCluster. The scripts are in the session scratchpad
(`build_extract.sh`, `build_chain.sh`, `build_uam.sh`, `build_room.sh`,
`build_cc.sh`) and each rebuilds its file from the pre-refactor original.

Two mistakes the diff could NOT catch, both caught by the compiler:
1. `menus_chain`: the listbox block's three `continue` statements belonged
   to the caller's walk loop. Nothing followed that block in the loop
   body, so they became plain `return` — equivalent, but invisible to a
   line diff.
2. `room_topology`: the store-button resolver re-declared its out-params.

**One tooling trap worth writing down: `\b` is NOT a word boundary in
awk — it is a backspace.** `awk '/\barea\b/'` matches nothing, silently.
It told me Passes 2 and 3 of BuildForArea do not use `area`; both do. An
earlier `grep -E` had reported it correctly and I discounted it. Use
`grep -E` (or awk's `\y`) for word-boundary scans.

### Three findings — ALL THREE NOW FIXED (2026-07-30, commits cdd9610 + 4743805)

1. **menus_extract step 1 (tooltip) is ungated and clobbers step 0.**
   Every other rung of the ladder is `if (!source)`. Step 1 is not: a
   control that has a tooltip gets it even when the step-0 per-kind
   formatter already produced a phrase, and its `__except` sets
   `source = nullptr`, wiping a step-0 result outright. Step 0 (the
   charsheet/credits/equip-stat/pazaak/journal/keybinding row anchors)
   was added years after step 1, which is almost certainly how this got
   missed. Preserved verbatim — `TryTooltip` now takes and returns the
   running `source`, which is what makes the clobber visible at the call
   site. **Whether it is a live bug depends on whether any step-0 anchor
   control carries a tooltip; that needs a log check, not a reading.**
2. **Two per-kind steps deref the owner panel with no SEH.** Step 9a
   reads `ownerForPerkind + kPanelControlsOffset` as a CExoArrayList and
   step 9d reads its vtable, both raw. They are downstream of the
   `IsPanelInManager` filter, so this is not the naked F2 shape, but it
   is the same class B1 and B3 closed elsewhere.
3. **menus_extract carries a stale comment.** The "CSWGuiEditbox — we
   don't yet know its struct layout well enough to read fields by
   speculative offsets" note sits directly below step 6b, which does
   exactly that. Left in place (Section A owned stale comments); it is a
   one-line delete whenever someone wants it.

One stale comment WAS dropped, because the split is what exposed it: the
post-fire block in `HandleInputEvent` carried its rationale twice, and
the outer copy was the pre-stack-mode version claiming "Out of combat:
fire-and-close" flatly — untrue since the paused/stack-mode branch was
added below it.

### NEEDS IN-GAME VERIFICATION — none of B2 is play-tested

This is the whole announce path plus the room-shape labeller, so the
coverage list is wide:
1. **Menus, broadly** — arrow through inventory, equipment (slot names +
   equipped item), character sheet (stat rows), the in-game menu strip,
   the map, a workbench, chargen class + portrait + name editbox, the
   keyboard-mapping screen, a store, the journal, party selection,
   pazaak start + wager. Every one of those is a distinct FromControl
   step; if one step was mis-wired it will be silent or say "control N".
2. **Chain nav** — the same screens, checking that nothing appeared in
   or vanished from Up/Down order (RebindChain's decorative filter and
   the four virtual-row anchors: credits, charsheet stats, wager, equip
   stats), plus the Mod settings entry on the Options screen.
3. **Unified action menu** — open on a target, arrow the categories,
   Shift+arrow for a description, Enter to fire; then cycle with `,`/`.`
   while it is open (follow-cycle re-anchor) and Enter again; Esc close
   in and out of combat; the out-of-combat paused (stack) vs running
   (fire-and-close) split.
4. **Room shape** — walk a nav-heavy area (Taris Upper City, the Sewers,
   the Ebon Hawk). Korridor / Kreuzung / Bereich labels, door and exit
   directions, dead ends.
   **The strongest check for 4 is a DumpGraphToLog comparison**, per the
   Phase-1 execution protocol for candidate 12: capture the dump for a
   fixed area and diff it against a pre-B2 build. It should be
   byte-identical. That is worth more than any amount of listening,
   because the merge passes are exactly where a threading mistake would
   show up as a subtly different cluster set.

Verification so far: `kdev build --clean` 196 TUs, warning baseline
unchanged. That is all — a clean build is not a working mod.

### B2 follow-up: all three findings fixed (cdd9610, 4743805)

User asked for all three. Corrections to the finding list as written above:

- **Finding 2 was UNDERCOUNTED — four sites, not two, and the worst one is
  in another file.** `IsClassSelectionIcon` (menus_internal.cpp) read the
  panel vtable raw, and it is step 9c's GATE, so it ran for every control
  that reached that far on every panel, not just chargen. A per-file scan
  cannot pair it with the menus_extract sites; only asking "who derefs the
  owner" across the call graph finds it. The other three were
  `TryClassSelectionIcon` (active_control), `TryInGameMenuIcon`
  (controls[] array, iterated in engine memory) and
  `TryPortraitCharGenArrow` (vtable).

- **Finding 1 turned out to be provably not-live, with numbers.** All 9202
  `Menus.FocusChange` samples across every log in the install read
  `tip[0]=""`, and no chain entry has ever been tagged `src=tooltip`. K1
  does not appear to populate CSWGuiControl+0x28 at all. So gating step 1
  changes nothing observable on any path we have evidence for — it turns
  an accident of the data into a structural guarantee. Worth remembering
  the method: the answer was in the logs, not in the code.

**`HasVtable` is now published** in `engine_panels.h` (moved out of
engine_panels.cpp's anonymous namespace). B6 folded five hand-spelled
copies of that guarded vtable deref *inside* engine_panels.cpp; B2 found
two more outside it. If a third file needs a guarded panel-identity check,
use this rather than writing another `__try`.
`engine_panels.h` gained `#include <cstdint>` — it previously declared no
includes at all and leaned on its includers, which broke the moment it
named `uintptr_t`.

**The useful distinction this fix wrote down:** `IsPanelInManager` proves a
panel pointer is LISTED, not that the object is ALIVE — panels[] holds
freed panels during teardown. `IdentifyPanel` is safe against a stale
panel *by construction* because it never dereferences one (it compares the
pointer against CGuiInGame slots); that is why the steps gated on it never
needed a guard and the four above did.

## SESSION END 2026-07-30 (afternoon) — B2 done, tested, and two bugs fixed

Eleven commits `6acf591..34b0f95`. Section B is now five of seven, all
play-tested. **Next session: B5 or B7, nothing else is owed.**

### B2 is VERIFIED IN GAME
User tested the full list (menus broadly, chain nav, unified action menu,
room shape) against freshness-checked DLLs and reported everything working
except one thing, which turned out to be a pre-existing bug — below.

### The class-icon double-announce: pre-existing, found by testing B2

Chargen class icons announced twice on the first pass. The user correctly
called it as NOT caused by the refactoring; `patch-20260725-215552.log`
carries the identical signature five days earlier.

**It took two attempts, and the first was wrong, so record the shape.**

There are THREE paths that can speak a focused control, and they run in
this order:
1. `AnnounceControl` (menus_monitors.cpp) — the chain-step path. Its
   success branch speaks, calls `MarkSpoken(0, text)`, AND primes
   `s_focusMonitorControl` / `s_focusMonitorText`.
2. `DrainPendingAnnounce` (menus.cpp) — drains the engine's
   SetActiveControl echo a tick later, via `SpeakIfChanged(0, text)`.
3. `MonitorFocusedControl` (menus_monitors.cpp) — the per-frame re-extract.

Two dedup mechanisms, and they are NOT the same thing:
- channel-0 `s_lastSpoken` — `MarkSpoken` primes it, `SpeakIfChanged`
  checks it. `AnnounceControl` and the monitor both used to speak with
  raw `prism::Speak`, which neither checks nor marks.
- `s_focusMonitorControl` / `s_focusMonitorText` — what actually keeps the
  monitor quiet in the normal case, and it is primed only by
  `AnnounceControl`'s SUCCESS branch.

The bug: step 9c's per-icon class-name cache starts cold, so on a first
visit `AnnounceControl`'s `FromControl` returns nullptr (visible as
`SetActive src=none`) and it leaves via its class-icon early-out — having
neither spoken NOR primed. The cursor warp then makes active_control the
icon, `DrainPendingAnnounce` fills the cache and speaks, and the unprimed
monitor speaks the same line again. On a revisit the cache is warm,
`AnnounceControl` wins and primes, and there is no double. That is why it
presented as "doubles when arrowing down, stops when arrowing up" — the
user's first pass was downward. In the same log, arrowing DOWN over
already-visited icons announces once.

Fix: the monitor now speaks via `SpeakIfChanged(0, text)` instead of
`prism::Speak`, so it both checks and marks.

**The failed first attempt is the lesson.** I added `MarkSpoken` to the
monitor — right dedup, wrong direction, because I had the speaker order
backwards. The log said so plainly and I read it too fast: the
`cache+speak` line sits immediately BEFORE the first `Speech.spoke`, which
identifies the cache-filling call as the FIRST speaker, not the monitor.
**When ordering matters, tag the speak sites rather than inferring order
from adjacent log lines.**

Residual risk now carried: if two adjacent controls have identical text
AND `AnnounceControl` is gated on both, the monitor will suppress the
second. Only the class-icon path is gated that way today and its six
labels are distinct. **A focus change that goes SILENT is the failure mode
to listen for** — on any screen, not just chargen.

Also still untouched and still fine: the monitor's other speak site (the
text-changed re-announce) has the same raw-`prism::Speak` shape. It was
explicitly checked and is NOT involved here — it logs
`"focused=%p text changed"` and that line appears nowhere in the traces.

## SESSION END 2026-07-30 (late) — Section B CLOSED, 7 of 7

Seven commits `f302233..d1783e4`. Every item play-tested by the user in
the same session it was written, each against a freshness-checked DLL.

### The session did not start where it planned to, and that was right

It opened on B5 sub-item 1 (refactor `charsheet::MaybeAnnounce` so it
stops hand-duplicating the offset-to-format logic its own spec table
encodes). Two things killed that plan, in order:

1. **`MaybeAnnounce` had zero callers.** Its call site was removed at some
   point, leaving a comment in `menus_monitors.cpp` that already spelled
   out the reasoning ("legacy workaround from when the panel wasn't
   keyboard-navigable"). Section A's dead-function sweep missed it. So the
   sub-item was a 98-line DELETE, not a refactor.
2. **The user then reframed the whole thing**, correctly: the
   read-everything-on-open behaviour is a relic of early menu
   accessibility, and what they want is panel name + focused control, with
   the user reading the rest themselves.

### The first-sight investigation, and what it actually found

"Remove first-sight speech at the core" is not one switch, because
"first sight" names three unrelated things:

- `SpeakPanelTitleOnFirstSight` — title only, already skips strip
  sub-screens. **Already correct.**
- `WalkAndCaptureOnFirstSight` — **speaks nothing at all**; it is a
  diagnostic child-walk plus cycle-category cache priming. Pure misnomer.
- The **content-fingerprint monitor** in `menus_monitors.cpp` — the actual
  bulk speaker, and the only one worth changing.

Its "first sight" suppression only covers the FIRST TICK. The engine fills
labels a tick later, so real content arrives as a *change* and is spoken
as a batch. That flaw had already been diagnosed and fixed three times the
same way — by dropping a screen from `IsContentMonitored` once it had real
keyboard navigation (Inventory + Equipment 2026-05-30, Abilities
2026-06-03, Container and Messages before them).

**The evidence came from the logs, not from reading the code** — the same
method that settled B2's tooltip finding. Grepping what that path has ever
spoken across all 969 patch logs:
- Character sheet: bare context-free numbers — "1", "14", "120000".
- Journal: whole quest bodies plus the entire entry list as one run-on line.
- Map: area names and map-note names.

The user kept the map deliberately ("details on opening is indeed more
useful there, that's bonus info") and confirmed the journal sort button is
chain-navigable.

**The trap the naive fix would have hit:** dropping the two kinds from
`IsContentMonitored` outright would have silently broken the character
sheet's chain rebind — a content change is what fires
`RebindChainPreserveIndex` when Tab swaps the displayed party member and
the Force-points row appears or disappears. So the fingerprint's two jobs
were separated instead: `IsContentMonitored` still TRACKS, the new
`IsContentSpoken` decides whether the diff is READ OUT.

### B5 — executed, with one sub-item rejected

- **charsheet opener** — deleted (dead, 98 lines).
- **chargen attr/skills** — the four near-verbatim pairs folded behind a
  `PanelDesc` struct rather than the report's long parameter lists. Every
  public per-panel name kept as a one-line wrapper (callers in menus.cpp,
  menus_chain, menus_chain_input, menus_focus, menus_monitors,
  menus_pending). Net -92 lines. Domain variation stayed per-file.
- **clamp-cursor** — report said four sites; there are THREE. The
  `menus_abilities` one steps an index into a filtered array, is Up/Down
  only, and re-announces the current tab on a clamp. New header-only
  `menus_nav.h` (menus_internal.h declares itself a private two-TU
  contract, engine_input.h is engine-side).
- **listbox announces + dialog specs** — three identical bodies folded to
  `SpeakRowAndPosition`; four hand-copied 14-field spec literals now come
  from one `constexpr MakeDialogSpec`.
- **anchor trio — REJECTED.** The three files are not the same shape:
  credits keys on PANEL KIND with a hardcoded sortCy and no sortCy column,
  the other two key on OFFSET; charsheet drops a row conditionally and the
  proposed spec has no field for it. A format function pointer per row
  would mean following an indirection to learn what a row does. Same trade
  B2 rejected for `DriveSelectedPeg`.

### B7 — three of eight executed, ONE REFUTED BY THE COMPILER

Removed: camera_orient's discarded `camera` parameter; map_user_markers'
dead in-world gate; WalkmeshGeometryAnalysis's unreachable
`cells.Count == 0` guard.

**`LaunchCommand.TryPin`'s `OperatingSystem.IsWindows()` guard is NOT
removable, and the report rated it "mechanical".** Both call sites are
already inside `if (OperatingSystem.IsWindows())` and kdev.csproj pins
win-x64 — dead twice over by inspection. Deleting it FAILS THE BUILD:
CA1416 does not propagate a caller's platform guard across a method
boundary, so that line is the only thing proving
`Process.ProcessorAffinity` is reached on Windows. Restored WITH A COMMENT
saying why, so the next sweep does not delete it again.

Rejected on reasoning: cycle_input's beacon-focus re-check (its comment
says it exists so the failure path SPEAKS rather than going silent —
removing it makes a failure silent, the one failure mode a blind user
cannot notice); cycle_input's `|| waypoints.empty()` (a post-condition
check against a documented contract, not a duplicate of a check one frame
up); `GetCachedWalls`' null return (exactly the caution B7 itself issued
about engine-derived pointers); prism's `g_sapiReady` recheck (the report
itself said "not urging action").

### THE FINDING THAT SHOULD SHAPE PHASE 5's END REPORT

**The same label-reading helper family was found re-implemented locally
three separate times in one session** — and a fourth was nearly written by
this session before catching it:
- `menus_chargen_layout` — a local two-path read, replaced with the
  existing `acc::engine::ReadLabelText` (which also clears the buffer on
  fault, i.e. the local copy was worse).
- `menus_equipstats::ReadEquipLabel` and
  `menus_charsheet::ReadCharSheetLabel` — both hand-rolling
  `acc::engine::ReadLabelTextAt`, whose own header comment names it as THE
  panel+offset convenience form.
- `menus_credits` inlines it a THIRD time but NOT identically: it falls
  back only when `ReadGuiString` returns false, without the non-empty
  check the others use, and its comment is about the "9999999" placeholder
  the panel carries before populate. **Left alone — switching it changes
  when the fallback fires, which is a behaviour question, not cleanup.**

This is the recurring failure mode CLAUDE.md already warns about ("before
adding a helper, search for an existing one"). Worth a Phase-5 note that
the engine_reads helpers are under-discovered, not under-provided.

### Behaviour questions surfaced and deliberately NOT auto-resolved

- **`InGameMessagesAnnounce` has no `rowCount <= 0` guard** where its two
  siblings do. Each site kept its own guard, both ends commented. Almost
  certainly never matters (a non-null row with rowCount 0 should not
  occur), but it is a behaviour call.
- **`menus_credits`' text-read fallback condition**, above.
- Still open from the previous session: `GetForegroundPanel`'s raw-size
  fallback indexing `panelData[panelSize - 1]` while its scan covers only
  the first 32.

## SESSION END 2026-07-30 (evening) — loose ends closed, only A12 left

Three more commits `ac8ad98..85db604`, all play-tested. Phase 3 now has
exactly one open item (A12).

### C4 — the report's premise was wrong in a way that changed the work

It said "`IterateLandmarks` already RECEIVES the `area` and discards it".
It has no `area` parameter at all. The discarded `area` is on
`AttachLandmarksToDoors` — and more importantly **the landmark cache never
recorded which area it was built for**, so there was nothing to compare
against. The fix had to add the state, not just thread a parameter.

`RebuildLandmarkCache` now stores `g_landmark_area` (set even when handed
a null area, so "never built" and "built for nothing" stay
distinguishable); `IterateLandmarks` takes the area the CALLER believes it
is walking; `AttachLandmarksToDoors` un-discards its parameter. On a
mismatch the walk yields nothing — same outcome as before — but logs it,
and returning false on the first call exits the caller's loop so it logs
once per attempt, not once per landmark.

### Probes retired — what had to be checked first

The user's call ("not required, I don't use them any more"). This is a
live-code removal, not a cleanup — `probe_priority_groups` was ticked
every frame from `core_tick`. Three checks before deleting:
- None of the three writes shipped state; they read, log and speak.
- **Saved keybindings could not be corrupted**: bindings persist by NAME
  (`Bind_<Name>` in acc_settings.ini), and both probe actions were already
  excluded from the configurator by `IsUserRebindable`, so they never
  persisted at all.
- `kActionNames` is indexed by enum ordinal, so removing enum values could
  have silently misaligned every name after the cut. Verified afterwards:
  73 entries against 73 enum values, matching name-for-name across the
  boundary.

`kdev build --clean` 193 TUs (was 196), 0 warnings. Ctrl+F9 and
Shift+AltGr are now unbound.

### The log-spam thread — and the fourth under-discovered helper

The user asked for block-aware dedup and then asked whether we already had
it. **We did.** `acclog::BlockLog` has existed all along and does exactly
that, including a `Key()` mechanism for stripping volatile pointers out of
block identity. Its header comment even names the failure mode:
"Trace's line-level dedup can't fold these because the repeats are
interleaved across the block's stride".

**Measure before choosing a fix — distinct-count is the wrong metric.**
`Trace` folds only CONSECUTIVE repeats. In a current 18k log:
- `Menus.PerKind` 3715 lines, 89% consecutive — Trace territory (and it
  already works around C2712 by splitting tags).
- `MapCursor.dump` 1421 lines, 7 distinct, **0% consecutive**.
- `Combat.Diag` 2649 lines, 36 distinct, **0% consecutive**.
- `Menus.SpecRead` 370 lines, 48 distinct, **0% consecutive**.
The three with huge redundancy and zero consecutive repeats emit a
repeating CYCLE of lines per tick; a per-tag last-message dedup cannot see
that. Judging by distinct-count alone would have picked exactly the wrong
mechanism.

**Why those sites never adopted BlockLog, and the way around it.** MSVC
C2712 forbids an object needing unwinding in a function that uses `__try`;
`BlockLog` has a destructor and all three sites are SEH-heavy engine
readers. The fix needs no restructuring of the SEH code: **declare the
BlockLog in an SEH-free caller and pass a POINTER in.** A pointer is
trivially destructible so C2712 never fires. Done for `MapCursor.dump`
(`Tick()` owns the block, `FindNearestExploredMapNote` takes
`acclog::BlockLog*`). Reuse this pattern for any other cycling dump.

Deliberately NOT changed, having looked:
- **`Combat.Diag` is not spam.** Its `CLEAR` lines come from a per-round
  engine HOOK — there is no enclosing scope to own a block. Its 360/s
  episode was already fixed by the `actions == 0` early-out; the ~2600
  that remain are genuine state changes logged once each.
- `Menus.SpecRead` at 370 lines is below the threshold for surgery.

Also worth knowing before anyone panics at a huge log: the 852,540-line
`patch-20260603-214547.log` is 82% the no-op combat-round clear that was
already fixed. Current logs run 2k-19k lines.

### THE HEADLINE FINDING FOR PHASE 5's END REPORT

**Four separate times in one day, an existing helper turned out to be
under-discovered rather than missing** — and two of those nearly became
new duplicates written by this session:
- `acc::engine::ReadLabelText` (chargen work — the local copy was WORSE,
  it did not clear the buffer on fault)
- `acc::engine::ReadLabelTextAt` (equipstats + charsheet, both
  hand-rolling it; its own header names it as THE panel+offset form)
- `acclog::BlockLog` (offered to BUILD it before grepping log.h; the user
  asked "don't we have that already?" and was right)

The pattern is not that these helpers are missing or badly written. It is
that they are not findable from the call site. That is a documentation /
discoverability problem, and it is the single most repeatable defect the
whole phase surfaced. Worth a Phase-5 recommendation more concrete than
"search first" — e.g. an index of the engine_reads + acclog surface in
docs/llm-docs/, since CLAUDE.md's existing rule did not prevent any of the
four.
