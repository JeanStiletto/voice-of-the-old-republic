# Large-file-handling — split plan

Triage produced by subagent on 2026-05-27 against the >500-line files in `patches/Accessibility/`. Of 31 candidates, 26 were classified single-concern (kept), 5 mix concerns (split). Out of 5, **4 done**, **1 remaining**.

## Done
- **swoop_race.cpp → swoop_spatial_audio.{h,cpp}** (commit `7c2827a`). Spatial-audio sweep (obstacle + accelpad loops, MGO array walk, AsObstacle/AsEnemy vtable downcasts) moved into its own TU behind `TickSpatialAudio` + `ResetSpatialAudio`. Tested live by user. Audio glossary additions for the four samples in commit `549beb4`.
- **spatial_change_detector.cpp → spatial_wall_surfaces.{h,cpp}** (commit `509ec03`). Wall cache + union-find clustering + per-surface descriptors moved into the new `acc::spatial::wall_surfaces` namespace (~476 line cpp + 92 line header). `spatial_change_detector.h` keeps its legacy public API as thin wrappers that forward to `wall_surfaces::*`; `WallSurfaceDesc` re-exported via `using`. No external caller updates required. change_detector.cpp dropped 1614 → 1180 lines.
- **menus_listbox.cpp tail → diag_chargen_feats.{h,cpp}** (commit `e2f4cbc`). The 163-line `DumpFeatsCharGenStructureIfNeeded` + helpers (`FeatNameStrref`, `DumpUshortListSEH`, `DumpChartCells`) lifted into a new TU under `acc::diag::chargen_feats`, matching the existing `diag_*` convention. menus_listbox.cpp 1779 → 1612 lines. **Plan correction**: the original note said the caller was `TickListboxMonitors`; the actual caller was always `AnnouncePanelTitle` in menus.cpp:406.
- **menus.cpp tail displacements** (commit `cecb549`). Two unrelated moves:
  - 4 shelved CSWGuiListBox entry-point diagnostic hooks + `DumpListBoxState` helper **deleted** (not relocated). All four had been commented out in hooks.toml with a recorded postmortem — entry-point hooks on CSWGuiListBox cause title-screen focus oscillation regardless of whether the hook fires. The four ~17-line commented-out hook blocks in hooks.toml collapsed into one consolidated tombstone covering all four addresses + the failure mode. Any re-enable needs a mid-function redesign with a register source per `feedback_hook_design_register_sources.md`, not a restoration of these handlers.
  - `OnSetMoveToModuleString` moved to `transitions.cpp` next to `AnnouncePreLoadDestination`. Live exported hook, unchanged behaviour; transitions.cpp picked up a one-line forward decl for `EnsurePrismInitialized`. menus.cpp 2664 → 2565 lines (−99 total).

## Remaining

### combat_query.cpp (916 lines)

Splits into two phase concerns:
- Phase 2A: self status (`SpeakSelectedPcStatBlock`, `TickLeaderChangeAutoAnnounce`, `BuildTargetCombatBrief`)
- Phase 2C: Shift+H target examine (`HotkeyShiftH`, `PollWin32Hotkey`)

Shared infrastructure: `ReadCreatureStats`, `CallIntAccessor`, the `StatSnap` type. A small `combat_query_internal.h` would be needed.

Phase 2C is structurally adjacent to `examine_view.cpp` (both operate on LastTarget). One option: merge Phase 2C with `examine_view.cpp` rather than a fresh TU.

Defer note: requires architectural decision (shared header vs merge with examine_view).

## Files explicitly kept (single concern)
For the record so the audit doesn't get re-run:

wall_topology.cpp (2890), engine_area.cpp (1501), menus_extract.cpp (1639) — single mega-function, in-file decomposition only if at all, menus_chain.cpp (847), transitions.cpp (977), combat.cpp (1038), examine_view.cpp (865), cycle_input.cpp (851), engine_radial.cpp (832), engine_panels.cpp (772), interact_hotkey.cpp (761), view_mode.cpp (746), menus_pending.cpp (705 — Drain mega-function is in-file decomposition only), menus_monitors.cpp (594), menus_editbox.cpp (580), guidance_autowalk.cpp (562), menus_store.cpp (559), combat_queue.cpp (548), menus_modsettings.cpp (534), menus_chargen_feats.cpp (530), engine_player.cpp (526), menus_powers_levelup.cpp (511), hotkeys.cpp (504), engine_offsets.h (1305 — single include path, structurally unsplittable), strings.h (1178 — enum must be contiguous), map_ui_cursor.cpp (1216).
