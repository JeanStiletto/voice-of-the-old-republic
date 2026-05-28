# LLM Refactoring Workflow — Current Status

## Working branch
`claude-mod-cleanup` (branched from `main`)

## Workflow source
Prompts from `C:/Users/fabia/Dev/llm-mod-refactoring-prompts` (JeanStiletto's PR branch `ai-bloat-audit-and-extraction-diagnosis` of ahicks92/llm-mod-refactoring-prompts).

## Prompts completed
- `sanity-checks-setup.md` — branch + scratchpad
- `information-gathering-and-checking.md` — CLAUDE.md patched, docs/llm-docs/{game-flow.md, CLAUDE.md} added, gitignore exception
- `code-directory-construction.md` — 178 .md index files under `llm-scratchpad/code-index/`, one per source file
- `large-file-handling.md` — all 5 splits done across commits `7c2827a`, `549beb4`, `509ec03`, `e2f4cbc`, `cecb549`, `3099e24`. See `large-file-splits.md` for the full record + plan-vs-reality corrections.
- `ai-bloat-audit.md` — round 1 done across commits `c7aa323`, `804b0b7`, `87329e4`, `394db2d`, `8752624`, `414691b`, `f3a0b2a`, `2234a17`, `6f2b6e0`, `2134943`. ~640 lines net removed across dead-code deletions + one diagnostic-cascade purge + two helper hoists. probe_camera_state deletion deferred (user is still using the probe). GetPartyMembers wrong-base read filed under `docs/known-issues.md` Bugs rather than fixed in the audit pass. See `bloat-audit-round-1.md` for the per-item breakdown.
- `high-level-cleanup.md` — review by 4 parallel subagents (duplication / fragility / docs / stdlib). 9 items shipped across commits `57e4a14` (kControlIdOffset hoist), `c0b69c3` (kInvalidObjectId centralise), `e1b10d3` (combat.h doc fix), `dad829b` (diag_chargen_feats doc trim), `ee56f24` (audio_footstep_suppress WHY pointer), `f991e8a` (camera_orient quaternion comment), `7e249c1` (menu_speak helper), `8fa26c6` (input_pipeline rename), `fe7a033` (menus_skillflow_nav extract). Item 10 (SubMenuState template) skipped on user call after step-by-step discussion. Items 11-15 (stdlib adoption, SEH diagnostics, DebouncedValue template, GetPartyMembers consolidation, probe_mouselook rename) skipped per review (high-risk / blocked / premature). Side-fix commit `565c5b8` corrects a pre-existing Sprengstoff-Right bug uncovered during item 3 test: chargen-sub-close heuristic in menus_pending.cpp was over-matching same-id row arrows.

## Prompts in progress
(none)

## Findings carried forward from indexing
Flagged for later phases:

### Large-file-handling targets (>2000 lines)
- `wall_topology.cpp` — 2890 lines; anonymous helpers numerous, public API tidy
- `menus.cpp` — 2663 lines

### Embedded mega-functions (no helper decomposition)
- `menus_extract.cpp::FromControl` — ~1290 lines (L344-L1637); the entire announce ladder
- `menus_pending.cpp::Drain` — ~540 lines (L163-L703); 10-op dispatch in one switch

### Duplication candidates
- `menus_chargen_feats.cpp` and `menus_powers_levelup.cpp` — structurally near-identical; intentional per memory `project_powers_levelup_is_skillflow_tree.md` — re-examine in ai-bloat-audit and low-level-cleanup

### Stale-comment / cleanup candidates
- `combat.h::TickCombatLog` doc comment claims listbox-poll path is live; actual live path is `AppendToMsgBuffer` hook
- `probe_audio_frame`, `probe_camera_distance` — review whether their investigations are concluded; lightweight, low priority (probe_camera_state confirmed still in use during the ai-bloat-audit pass)
- `probe_pathfind`, `probe_mouselook`, `probe_priority_groups` — still wired and useful; keep
- `diag_input_pipeline` — outgrew "diagnostic" framing; carries production logic; rename candidate

### Index anomalies
- `engine_area.cpp` — token-limit chunked read; .md notes incomplete coverage of late-file functions
- `wall_topology.cpp` — anonymous helpers not enumerated in the index; public API matches the header

## Prompts pending
- low-level-cleanup.md (next — user's explicit choice for next session)
- input-handling.md (deferred — user opted to skip ahead to low-level-cleanup)
- string-builder.md (deferred — same)
- finalization.md

The ai-bloat-audit prompt is structured as a re-runnable pass — a follow-up round is fair after low-level-cleanup lands.

## Scratchpad files
- `current_status.md` (this file)
- `code-index/` — 178 .md index files
- `code-index/_files.txt` — source file inventory

## Game / project context
KOTOR 1 (BioWare Odyssey Engine) accessibility mod. Native-binary patch via KPatchManager; sources under `patches/Accessibility/`. See `CLAUDE.md` + `docs/llm-docs/CLAUDE.md` for full project map.
