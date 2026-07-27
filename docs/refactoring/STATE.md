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

- **Active phase:** Phase 0 (infrastructure) — set up, awaiting user feedback
  on the overall plan before Phase 1 starts.
- **Next action:** User reviews the plan-improvement suggestions (delivered in
  chat 2026-07-27); incorporate decisions here, then start Phase 1.
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
