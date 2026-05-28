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
- `low-level-cleanup.md` — round 1 done across 15 commits `6d185d2`, `8131285`, `4c2c7de`, `648e298`, `fc28e7b`, `8209cb0`, `1e59f9e`, `b22fa5e`, `39a65b3`, `d0a3422`, `65cac7e`, `8b77317`, `26b1251`, `afab34b`, `0582365`. Triage via 16 parallel subagents (one per module group) producing ~30 raw items; pruned to 15-item shortlist, all approved bulk-execute. Net −241 lines across 13 files (167 added / 408 removed). Mix of dead-code deletions (engine_offsets orphan combat constants, examine_view TickEnginePanelLifecycle, core_dllmain DumpFunctionBytes, log FindEntry, menus_chargen_attr empty namespace, guidance_autowalk pre-dispatch snapshot), helper extractions (engine_area ReadFaceVertexIndices + TransformEdgeEndpoints + kMinEdgeXYLengthSq, engine_options WriteMouseLook, cycle_input TryResolveOrAnnounceNoFocus, map_ui_cursor ResolveAmbientText), and reuse passes (IsSentinelHandle, ResetSessionState, engine_reads::ReadGuiString, MapPin offset constants). In-game test (map cursor, autowalk, beacon, examine, walk) confirmed green. See `low-level-cleanup-round-1.md` for the per-item breakdown + dropped-from-shortlist rationale.
- `input-handling.md` — round 1 done; user diverged from prompt structure into a 6-item enumerated F1-F6 cleanup of frictions surfaced by the architecture survey (see `input-handling-survey.md`). 7 commits: F1 dispatcher decomposition (5 commits `388f580`, `004cde5`, `3a9904c`, `5d2436f`, `45bb22e` — `OnHandleInputEvent` 1037→528 lines via extraction of `HandleEsc`/`HandleLeftRight`/`HandleNavStep`/`HandleEnterActivation` into menus_chain + IsModalPopupPanel promotion), F2 dead op deletion (`792efd9`), F3 mutex helper extraction (`fb0d106` menus_submenu). F4/F5/F6 dissolved on closer reading — all false positives from the survey subagent. In-game test confirmed green after F1. See `input-handling-round-1.md` for the survey-quality lesson + per-item breakdown.
- `string-builder.md` — skipped after gross check + spot-reads. NOT a string-builder mod: codebase has heavy localization scaffold (strings.h ~1180 lines, ~300 enum-keyed format strings, strings_{de,en}.cpp ~400 lines each), and the dominant pattern is `snprintf(buf, sz, strings::Get(Id::FmtFoo), args…)` — placeholder-substitution against a localized template, not fragment concatenation. Per-language grammar lives inside the format strings. A fragment-based builder would actively work against the localization model. One real micro-opportunity flagged for future round: examine_view.cpp has ~15 instances of `if (idx < kMaxRows) snprintf(rows[idx], …); ++idx;` boilerplate that a small RowsBuffer helper would collapse — not in scope for string-builder.

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
- `probe_audio_frame`, `probe_camera_distance` — review whether their investigations are concluded; lightweight, low priority (probe_camera_state confirmed still in use during the ai-bloat-audit pass)
- `probe_pathfind`, `probe_mouselook`, `probe_priority_groups` — still wired and useful; keep
- `engine_options.cpp::SetMouseLook` — zero callers after low-level-cleanup round 1 (ToggleMouseLook stopped routing through it); candidate for deletion next round if no external user surfaces

### Index anomalies
- `engine_area.cpp` — token-limit chunked read; .md notes incomplete coverage of late-file functions
- `wall_topology.cpp` — anonymous helpers not enumerated in the index; public API matches the header

## Prompts pending
- finalization.md

ai-bloat-audit and low-level-cleanup are structured as re-runnable passes — follow-up rounds of either are fair after finalization lands. One examine_view RowsBuffer micro-cleanup is queued for a low-level-cleanup round-2.

## Scratchpad files
- `current_status.md` (this file)
- `code-index/` — 178 .md index files
- `code-index/_files.txt` — source file inventory

## Game / project context
KOTOR 1 (BioWare Odyssey Engine) accessibility mod. Native-binary patch via KPatchManager; sources under `patches/Accessibility/`. See `CLAUDE.md` + `docs/llm-docs/CLAUDE.md` for full project map.
