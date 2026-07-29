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

## Current status  (updated 2026-07-29 — read this block, not the older
## per-phase sections further down, which are history)

- **Active phase:** Phase 2 (high-level cleanup). Batches A and B done and
  committed, C partly done. See "Phase 2 status" near the end of this file
  for the itemised list.
- **Branch:** `refactor/phase2-coupling`, cut from main @ 4c4e216 (the
  Phase-1 merge). Phase 1 is already merged to main and smoke-tested by
  the user; nothing about it is outstanding except candidate 23.
- **Build state:** green everywhere. `kdev build --clean` = 194 TUs, 0
  warnings. kdev and installer `dotnet build` = 0 errors / 0 warnings
  (the installer's single warning is pre-existing, in a file we never
  touched).

### C8 is DONE (2026-07-29). Next action: user decision on the Allard bug.

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

NOT fixed here (behaviour change on a shipped build = user's call, and the
naive fix makes it worse: wrapping them returns 0 on Allard, turning a
probable crash into a certain one). The three-step fix is written up in
the report. Recommend step 1 — widening the kdev harvester — regardless,
since it is cheap and prevents a silent repeat.

Phase-2 work is NOT yet in-game tested. B4 rewired the SEH read
primitives all three minigames share (a short swoop race + turret
sequence covers it) and A1/A2 touch room + landmark narration. C8 itself
is compile-verified only, but it is a pure constant move with a proved
identical constant set — the in-game risk is the same as B4/A1/A2's.

**Also still open:** candidate 23 (menus_listbox picker, carried from
Phase 1 — moves state, so measure the variables not just the function
names), C4 (doorMatched split → Phase 3), candidate 28 (includer
migration → falls out of Phase 3).

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
- **Candidate 23 (menus_listbox picker split) — NOT REACHED.**
  Ran out of session before it. It is the one remaining approved,
  unexecuted, un-attempted candidate. Note before anyone picks it up: it
  moves state (s_equipPickerActive / s_workbenchUpgradePickerActive and
  their panel pointers), which is precisely the category that broke
  candidates 13 and 24 — measure the *variables*, not just the function
  names, before cutting.

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

- C8 finding: the spec's "19 unwrapped .text addresses" was a
  miscount — engine_offsets.h had none. But 12 real ones exist elsewhere
  in the patch and are provably wrong on the Allard Russian build. Root
  cause is kdev's address-harvester regex, not the header. Full analysis
  and the three-step fix in `reports/phase-2-cleanup.md`; awaiting a user
  decision because it is a behaviour change on a shipped build.

**Next action: user decision on the 12 Allard-wrong addresses** (see the
status block at the top of this file). Nothing else in Phase 2 is
blocked on it.

**Also open:** candidate 23 (menus_listbox picker, carried from Phase 1),
C4 (doorMatched split, deferred to Phase 3), candidate 28 (includer
migration, falls out of Phase 3).

**Untested in game:** everything in Phase 2. B4 rewired the read
primitives all three minigames use — a short swoop race and turret
sequence is the check. A1/A2 touch room + landmark narration.
