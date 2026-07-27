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

## Current status

- **Active phase:** Phase 1 (structure audit) — scans COMPLETE, consolidated
  report written, **awaiting user approval of the candidate list**.
- **Next action:** Walk the candidate list in
  `docs/refactoring/reports/phase-1-structure.md` with the user (item by
  item or per batch), record approvals/rejections here, then execute
  approved batches with a build (+ in-game check where marked) after each.
- **Branch:** `refactor/pre-k2-cleanup` (created 2026-07-27 from main @ bc9492b).
- **Working tree:** clean except docs/refactoring/.

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
