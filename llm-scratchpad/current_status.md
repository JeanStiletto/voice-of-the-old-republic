# LLM Refactoring Workflow — Current Status

## Working branch
`claude-mod-cleanup` (branched from `main`)

## Workflow source
Prompts from `C:/Users/fabia/Dev/llm-mod-refactoring-prompts` (JeanStiletto's PR branch `ai-bloat-audit-and-extraction-diagnosis` of ahicks92/llm-mod-refactoring-prompts).

## Prompts completed
- `sanity-checks-setup.md` — branch + scratchpad
- `information-gathering-and-checking.md`
  - Audited CLAUDE.md (subagent), applied 5 patches (stale "empty" claims, third_party listing, `cd` self-contradiction, kdev launch description, machine-specific path)
  - Audited docs-and-API gaps (subagent), confirmed strong existing coverage from headers + memory + archiev/
  - Synthesized `docs/llm-docs/game-flow.md` (lifecycle reference, 544 lines)
  - Added `docs/llm-docs/CLAUDE.md` index, linked from root CLAUDE.md
  - Deferred (low payoff for refactoring): file-format reference, full CSWGuiDialog struct dump
  - Stale comment noticed in `combat.h::TickCombatLog` (claims poll path is live; actual live path is `AppendToMsgBuffer` hook) — defer to low-level-cleanup phase

## Prompts pending
- code-directory-construction.md (next)
- large-file-handling.md
- ai-bloat-audit.md
- high-level-cleanup.md
- input-handling.md
- string-builder.md
- low-level-cleanup.md
- finalization.md

## Scratchpad files
- `current_status.md` (this file)

## Game / project context
KOTOR 1 (BioWare Odyssey Engine) accessibility mod. Native-binary patch via KPatchManager; sources under `patches/Accessibility/`. See `CLAUDE.md` + `docs/llm-docs/CLAUDE.md` for full project map.
