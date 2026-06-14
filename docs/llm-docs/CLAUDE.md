# llm-docs

Reference material curated for LLM consumption. Progressive-disclosure index — read the entry that matches your current task, not the whole folder.

## Files

- **`game-flow.md`** — lifecycle map (DLL attach → main menu → chargen → world → dialog → combat → menus → save/load → game-over) with the engine signal and hooking module for each phase. Start here when placing or refactoring a hook.
- **`accessibility-map.md`** — pillar map of hook candidates by accessibility goal (dialog, combat, world, UI). Less linear than game-flow.md; complementary.
- **`sarif-cookbook.md`** — jq recipes for querying Lane's local SARIF export (`re/k1_win_gog_swkotor.exe.sarif`). Use when `re/swkotor.exe.h` shows undefined fields or when you need cross-references for an address.
- **`turret-minigame-model.md`** — engine-confirmed model of the swoop/turret minigame subsystem (decompiled). The reference shape for an RE-model doc: input→state→action→effect chain, function addresses, struct offsets, and a "what we got wrong" section. Produced by `kdev re` then curated here. When reconstructing another subsystem, `kdev re "<ClassRegex>" --decompile` and curate the skeleton to a sibling of this file.
- **`code-index/`** — per-file LLM-readable summary for each source file under `patches/Accessibility/` (one `.md` per `.cpp`/`.h` plus `_files.txt` inventory). Generated during the llm-mod-refactoring-prompts `code-directory-construction` phase; useful as fast context before reading a file in full. May drift if not refreshed on major restructuring.
- **`interaction-dispatch-model.md`** — how player/leader walk + verb dispatch (use/talk/take) flow through the engine; the message-bus, ActionManager priming, and the native walk-then-talk model.
- **`persistence-scriptvartable.md`** — engine-confirmed model for persisting named per-object variables into the `.sav` via `CSWSScriptVarTable` (player creature **+0x100**; Ghidra mislabels the +0x110 field, which is actually a `CSWVarTable`). The reusable in-save storage primitive (no sidecar file, no save hook). Distinguishes the named string-capable table from the fixed NWScript `CSWVarTable`. Read when building any save-persistent mod state (discovery index, custom flags). Includes the object-identity model and the verified write recipe.

### Subsystem RE reference (migrated from the agent memory store, 2026-06-14)
Each is a consolidation of former memory notes (offsets, addresses, decompiled mechanics). Read the one matching your subsystem; they replace the always-on memory index for engine RE. See `feedback_memory_vs_docs_discipline` in memory for the rule (RE → here, not memory).
- **`engine-objects-and-architecture.md`** — client/server (CSWS/CSWC) split, object-handle namespaces, party-table indirection, creature/HP/name accessors, player-control toggle, AI action queue.
- **`gui-and-input-internals.md`** — CSWGui* struct offsets, gui_string text indirection, panel/foreground routing, cursor + hit-test surfaces, listbox model, in-DLL input pipeline + menu chain.
- **`ingame-screens-reference.md`** — per-screen surfaces: workbench, map, options sub-screens, save/load, party-select, galaxy map, abilities, charsheet, placeables, level-up.
- **`action-menu-and-combat.md`** — radial/personal action surfaces, the engine action picker, Q/E targeting + ShowObject focus signal, bare-key dispatch, combat-log funnel.
- **`walk-nav-and-walkmesh.md`** — leader-walk recipe, per-area nav-graph layout, dialog-speaker resolution, WallTopo walkmesh clustering.
- **`audio-internals.md`** — Play3DOneShotSound gain chain, CExoSoundSource lifecycle, sound-mode pause exemptions, footstep paths, cue/party filtering, droid subtitles.
- **`camera-and-swoop.md`** — camera screen-edge turn, A/D-vs-W/S decoupling, mouse-look gating, swoop accelerator-pad classification.

## `re/` — reverse-engineering assets

- **`swkotor.exe.h`** — Ghidra-exported C header, ~25k lines. Primary source for struct layouts; ~205 structs have real bodies, ~797 are `PlaceHolder`. Cross-check with SARIF when a struct returns garbage.
- **`k1_win_gog_swkotor.exe.xml`** — Lane's full Ghidra XML export. Function names, addresses, comments.
- **`k1_win_gog_swkotor.exe.sarif`** — Lane's full SARIF (~490 MB). Query via jq per `sarif-cookbook.md`.
- **`KotOR_1_System_Layout-2.pdf`** — community RE reference.

## Deferred / nice-to-have

These were identified as future-useful but not built (would require write-up effort without immediate refactoring payoff):

- **File-format reference** (GFF / 2DA / TLK / ERF binary layouts) — pending content-mod work; defer until needed.
- **CSWGuiDialog full struct dump** — current `engine_offsets.h` covers the touched fields; SARIF query to add speaker/cinematic/node-index when a dialog-side feature requires them.
