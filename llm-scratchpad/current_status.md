# LLM Refactoring Workflow — Current Status

## Working branch
`claude-mod-cleanup` (branched from `main`)

## Workflow source
Prompts from `C:/Users/fabia/Dev/llm-mod-refactoring-prompts` (JeanStiletto's PR branch `ai-bloat-audit-and-extraction-diagnosis` of ahicks92/llm-mod-refactoring-prompts).

## Prompts completed
- `sanity-checks-setup.md` — branch + scratchpad
- `information-gathering-and-checking.md` — CLAUDE.md patched, docs/llm-docs/{game-flow.md, CLAUDE.md} added, gitignore exception
- `code-directory-construction.md` — 178 .md index files under `llm-scratchpad/code-index/`, one per source file

## Prompts in progress
- `large-file-handling.md` — 4 of 5 splits done. **See `llm-scratchpad/large-file-splits.md` for the full plan and the 1 remaining split.** Done: swoop_race → swoop_spatial_audio (`7c2827a`), audio-glossary wiring (`549beb4`), spatial_change_detector → spatial_wall_surfaces (`509ec03`), menus_listbox tail → diag_chargen_feats (`e2f4cbc`), menus.cpp tail (dead listbox hooks tombstoned + OnSetMoveToModuleString → transitions.cpp) (`cecb549`). Next: combat_query.cpp 2A/2C split.

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
- `diag_play3doneshotsound.{h,cpp}` — hook commented out in hooks.toml, investigation concluded per memory — deletion candidate
- `probe_audio_frame`, `probe_camera_state`, `probe_camera_distance` — review whether their investigations are concluded; lightweight, low priority
- `probe_pathfind`, `probe_mouselook`, `probe_priority_groups` — still wired and useful; keep
- `diag_input_pipeline` — outgrew "diagnostic" framing; carries production logic; rename candidate

### Index anomalies
- `engine_area.cpp` — token-limit chunked read; .md notes incomplete coverage of late-file functions
- `wall_topology.cpp` — anonymous helpers not enumerated in the index; public API matches the header

## Prompts pending
- large-file-handling.md (next — triggered by >2000-line files)
- ai-bloat-audit.md
- high-level-cleanup.md
- input-handling.md
- string-builder.md
- low-level-cleanup.md
- finalization.md

## Scratchpad files
- `current_status.md` (this file)
- `code-index/` — 178 .md index files
- `code-index/_files.txt` — source file inventory

## Game / project context
KOTOR 1 (BioWare Odyssey Engine) accessibility mod. Native-binary patch via KPatchManager; sources under `patches/Accessibility/`. See `CLAUDE.md` + `docs/llm-docs/CLAUDE.md` for full project map.
