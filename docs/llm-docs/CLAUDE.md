# llm-docs

Reference material curated for LLM consumption. Progressive-disclosure index — read the entry that matches your current task, not the whole folder.

## Files

- **`game-flow.md`** — lifecycle map (DLL attach → main menu → chargen → world → dialog → combat → menus → save/load → game-over) with the engine signal and hooking module for each phase. Start here when placing or refactoring a hook.
- **`accessibility-map.md`** — pillar map of hook candidates by accessibility goal (dialog, combat, world, UI). Less linear than game-flow.md; complementary.
- **`sarif-cookbook.md`** — jq recipes for querying Lane's local SARIF export (`re/k1_win_gog_swkotor.exe.sarif`). Use when `re/swkotor.exe.h` shows undefined fields or when you need cross-references for an address.

## `re/` — reverse-engineering assets

- **`swkotor.exe.h`** — Ghidra-exported C header, ~25k lines. Primary source for struct layouts; ~205 structs have real bodies, ~797 are `PlaceHolder`. Cross-check with SARIF when a struct returns garbage.
- **`k1_win_gog_swkotor.exe.xml`** — Lane's full Ghidra XML export. Function names, addresses, comments.
- **`k1_win_gog_swkotor.exe.sarif`** — Lane's full SARIF (~490 MB). Query via jq per `sarif-cookbook.md`.
- **`KotOR_1_System_Layout-2.pdf`** — community RE reference.

## Deferred / nice-to-have

These were identified as future-useful but not built (would require write-up effort without immediate refactoring payoff):

- **File-format reference** (GFF / 2DA / TLK / ERF binary layouts) — pending content-mod work; defer until needed.
- **CSWGuiDialog full struct dump** — current `engine_offsets.h` covers the touched fields; SARIF query to add speaker/cinematic/node-index when a dialog-side feature requires them.
