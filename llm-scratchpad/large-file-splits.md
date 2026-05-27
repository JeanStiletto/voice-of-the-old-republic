# Large-file-handling — split plan

Triage produced by subagent on 2026-05-27 against the >500-line files in `patches/Accessibility/`. Of 31 candidates, 26 were classified single-concern (kept), 5 mix concerns (split). Out of 5, **1 done**, **4 remaining**.

## Done
- **swoop_race.cpp → swoop_spatial_audio.{h,cpp}** (commit `7c2827a`). Spatial-audio sweep (obstacle + accelpad loops, MGO array walk, AsObstacle/AsEnemy vtable downcasts) moved into its own TU behind `TickSpatialAudio` + `ResetSpatialAudio`. Tested live by user. Audio glossary additions for the four samples in commit `549beb4`.

## Remaining (high value)

### 1. spatial_change_detector.cpp (1614 lines)
Mixes three algorithms:
- Public T1 per-sector distance tracking + T2 foremost-in-front cone scan (keep)
- Wall cache build (`g_walls`, `g_wall_count`) + surface clustering via union-find (`ClusterEdgesIntoSurfaces`, `BuildSurfaceDescriptors`, ~400 lines) — **split out**

Target: `spatial_wall_surfaces.cpp` with the union-find math and wall-cache build.

Risk: `g_walls` and `g_edge_surface_id` are read from both halves. The file already exposes public accessors (`GetCachedWalls`, `GetEdgeSurfaceId`, `GetWallSurfaceCount`, `GetWallSurfaceDesc`) — keep the arrays in `spatial_wall_surfaces.cpp` and have the original TU call through the accessors. No shared writeable global needs duplication.

### 2. menus_listbox.cpp (1779 lines)
Mostly single-concern (listbox spec dispatch), but a diagnostic block at the tail (L1616-L1779, ~130 lines) doesn't belong:
- `DumpUshortListSEH`, `DumpChartCells`, `DumpFeatsCharGenStructureIfNeeded`

Caller of the dump is `TickListboxMonitors` (~L1594), so it's effectively diagnostic instrumentation for the feats chargen panel. Options:
- (a) Move to `menus_chargen_feats.cpp` with a forward-declaration / extern
- (b) New TU `diag_chargen_feats.cpp`

(b) is cleaner if more diagnostic-only code accumulates; (a) is minimal-touch.

## Remaining (marginal)

### 3. menus.cpp (2663 lines)
Most growth since the prior 5327→1906 refactor (memory `project_menus_refactor_plan.md`) is organic feature growth on the core hook (`OnHandleInputEvent` expanding for new panel types). Only two small displacements live in the wrong file:
- 4 diagnostic listbox hooks (L2564-L2647, ~80 lines): `OnListBoxLMouseDown`, `OnListBoxLMouseUp`, `OnListBoxHandleInput`, `OnListBoxSetSelectedControl` → move to `menus_listbox.cpp`
- `OnSetMoveToModuleString` (L2647-L2661, ~15 lines): thin entry hook that just calls `transitions::AnnouncePreLoadDestination`. Move to `transitions.cpp`.

Both are mechanical (no shared state). `hooks.toml` references the symbol names, not the TU, so no manifest changes needed.

Defer note: split only if menus.cpp continues to grow. Currently low payoff.

### 4. combat_query.cpp (916 lines)
Splits into two phase concerns:
- Phase 2A: self status (`SpeakSelectedPcStatBlock`, `TickLeaderChangeAutoAnnounce`, `BuildTargetCombatBrief`)
- Phase 2C: Shift+H target examine (`HotkeyShiftH`, `PollWin32Hotkey`)

Shared infrastructure: `ReadCreatureStats`, `CallIntAccessor`, the `StatSnap` type. A small `combat_query_internal.h` would be needed.

Phase 2C is structurally adjacent to `examine_view.cpp` (both operate on LastTarget). One option: merge Phase 2C with `examine_view.cpp` rather than a fresh TU.

Defer note: requires architectural decision (shared header vs merge with examine_view). Lower priority than #1 and #2.

## Files explicitly kept (single concern)
For the record so the audit doesn't get re-run:

wall_topology.cpp (2890), engine_area.cpp (1501), menus_extract.cpp (1639) — single mega-function, in-file decomposition only if at all, menus_chain.cpp (847), transitions.cpp (977), combat.cpp (1038), examine_view.cpp (865), cycle_input.cpp (851), engine_radial.cpp (832), engine_panels.cpp (772), interact_hotkey.cpp (761), view_mode.cpp (746), menus_pending.cpp (705 — Drain mega-function is in-file decomposition only), menus_monitors.cpp (594), menus_editbox.cpp (580), guidance_autowalk.cpp (562), menus_store.cpp (559), combat_queue.cpp (548), menus_modsettings.cpp (534), menus_chargen_feats.cpp (530), engine_player.cpp (526), menus_powers_levelup.cpp (511), hotkeys.cpp (504), engine_offsets.h (1305 — single include path, structurally unsplittable), strings.h (1178 — enum must be contiguous), map_ui_cursor.cpp (1216).
