# Phase 1 — Structure audit: consolidated report and candidate list

Date: 2026-07-27. Inputs: three scan reports in this directory
(`phase-1-scan-a-menus-engine-strings.md`, `phase-1-scan-b-rest.md`,
`phase-1-scan-c-kdev-installer.md` — detail for every item below lives
there, keyed A*/B*/C*/I*/K*), plus cross-cutting include-graph and
build-system checks done in the main session. Synthesis and
recommendations: main session (Fable), per the subagent policy.

**Nothing in this list is approved or executed.** Per the rules of
engagement, each candidate needs explicit user approval; discussion happens
item by item. All proposals are behavior-preserving; the do-not-touch list
(hook addresses/byte patterns, engine_offsets.h values, calling-convention
typedefs, exports.def names) is respected throughout — engine_offsets.h
appears only as *regrouping*, values untouched.

## Cross-cutting facts established this phase

- The build is flat: `kdev build` discovers sources via a non-recursive
  glob, and the legacy `create-patch.bat` fallback is flat too. Physical
  subfolders under `patches/Accessibility/` would need a kdev build change
  and would break `--bat`. Consequence: module grouping stays a *naming*
  convention in the patch; no folder moves proposed there.
- Header-to-header hygiene is already good: no header includes more than 3
  other local headers.
- Include fan-out hotspots (includers excluding own cpp): log.h 109,
  engine_offsets.h 77 (24 of them headers), strings.h 72, engine_player.h
  63, prism.h 61, engine_area.h 49, engine_panels.h 39, engine_rebase.h 36,
  hotkeys.h 35, engine_reads.h 35. engine_rebase.h was checked: minimal,
  fan-out is inherent to the rebasing design, not a problem.
- Hub files: core_tick.cpp and menus.cpp each include 55 local headers —
  expected for the tick dispatcher and the menu hook hub.
- Code index verified complete for kdev and installer (every .cs has an
  entry and vice versa; no stale entries).

## Candidate list

Grouped into batches; a batch is also the intended commit granularity
(small thematic commits, bisectable). Verification bar per item is noted:
"build" = clean `kdev build` (or `dotnet build`) suffices; "in-game" =
user smoke test of the named subsystem required before the batch counts as
done.

### Batch 1 — mechanical patch-side splits (low risk)

1. **menus.cpp (2269) three-way split** (A1): new `menus_internal.cpp`
   (~500, definitions for the already-existing menus_internal.h seam),
   `menus_focus.cpp`/`.h` (~400, first-sight/focus-capture, needs
   de-staticizing), `menus_dispatch.cpp` (~550, OnHandleInputEvent +
   OnHandleFocusChange). menus.cpp shrinks to ~830. Includes the A16
   comment fix in menus_internal.h. Verify: build; in-game menu smoke test
   (inventory, dialog, chargen, a listbox screen) because
   OnHandleInputEvent is the most game-critical dispatch function.
2. **menus_chain.cpp (1827) → menus_chain_input.cpp** (A2): move the five
   input handlers (~725 lines) already declared individually in
   menus_chain.h. Verify: build; quick in-game chain-nav check.
3. **engine_area.cpp (1901) → + engine_area_map.cpp + engine_area_walls.cpp**
   (A5): object model stays (~1045), map/fog/map-pin block (~300) and
   walkmesh wall-edge pipeline (~555) move out. Caveat: move the
   out-of-order `AreaObjectIterator::Next()` back with the core block.
   Verify: build.
4. **engine_reads.cpp (1026) → + engine_reads_items.cpp** (A6): generic
   GUI-control readers stay (~360); item/creature-domain reads move
   (~665). Verify: build.
5. **engine_player.cpp (843) → + engine_player_party.cpp +
   engine_player_inputlock.cpp** (A7): core reads stay (~175). Caution:
   non-contiguous cuts — the groups interleave in source order. Verify:
   build; in-game spot check of leader name announce + input-lock restore.
6. **engine_panels.cpp (1182) → + engine_panels_state.cpp** (A8):
   foreground-blocking/input-class half (~370) moves out. Verify: build.
7. **combat.cpp (1491) → + combat_log.cpp** (B2): the whole msg-router
   combat-log parser rule set (~1150) moves next to the existing combat_*
   family; needs a small internal header for its own statics. Verify:
   build; in-game combat announce check.
8. **examine_view.cpp (1530) → + examine_view_effect_names.cpp** (B3):
   ~700 lines of pure per-language effect-name tables move out. Near-zero
   risk. Verify: build.
9. **update_checker.cpp (994) → + update_checker_http.cpp** (B4): generic
   WinHTTP/JSON/version-compare primitives (~400) move out. Verify: build.
10. **engine_offsets.h (1820) regroup into ~5 subsystem headers behind a
    thin aggregator** (A10): engine_offsets.h remains and includes the new
    narrower headers, so all 77 includers compile unchanged; values
    byte-for-byte identical (do-not-touch respected). Verify: build
    (ideally with a diff-of-preprocessed-constants sanity script).
11. **hooks.toml subsystem banner comments** (B16): apply the banner style
    the file already uses once to every subsystem group. Comments only —
    zero functional content touched. Verify: build (kpatch packaging
    parses the file).

### Batch 2 — patch-side splits with shared-state entanglement (need care + in-game checks)

12. **wall_topology.cpp (3404) split into 4–5 files along its documented
    phases** (B1): core+API / doors / classification / build / diag, with
    a new `wall_topology_internal.h` turning the shared file-statics into
    externs. Risk: a missed extern can silently duplicate state. Verify:
    build; in-game pass through a nav-heavy area confirming region
    narration is unchanged.
13. **transitions.cpp (1412) → + transitions_landmarks.cpp** (B5): the
    landmark-cache subsystem (~500) moves out; ResolveRoomSpeech keeps
    reading it through a small exposed surface. Looser seam than the
    Batch-1 items. Verify: build; in-game room/landmark narration check.
14. **combat_diag.cpp: extract the production hook into
    combat_queue_hooks.cpp** (B13, action part): OnCombatRoundAddAction
    (drives the shipped combat-queue announce) plus the shared queue-size
    readers move next to combat_queue.cpp; combat_diag.cpp becomes
    genuinely diagnostic-only. Verify: build; in-game combat-queue
    announce check.

### Batch 3 — kdev and installer splits

15. **kdev SoundScoreCommand.cs (1348) → extract WavAnalysis engine**
    (C1, ~835 lines, exact nested-class seam). Verify: dotnet build.
16. **kdev WalkmeshGeometryAuditCommand.cs (999) → extract the geometry
    engine** (C2, ~590 lines). Verify: dotnet build.
17. **installer Program.cs (1037) → GamePathDetector.cs + InstallFlow.cs +
    UninstallFlow.cs (+ optional VersionComparer.cs)** (I1): Program.cs
    becomes entry point + mode dispatch (~260). Verify: dotnet build +
    a manual install/uninstall pass before release (touches many call
    sites; no logic change).

### Batch 4 — decisions needed before any code moves

These are judgment calls; each needs a yes/no (or option pick) from the
user. Recommendations included.

18. **menu_speak.* → menus_speak.*** (A12): only file breaking the
    menus_ prefix; one includer (unified_action_menu.cpp) found.
    Recommendation: rename (cheap, restores the convention). Verify:
    build.
19. **interact_hotkey.cpp charter** (B9): PollHotkey (~450 lines) is a
    general cross-subsystem input router in a file named for one feature,
    overlapping input_pipeline.cpp's charter. Options: (a) split into
    interact_dispatch.cpp and fold the router into input_pipeline.cpp —
    one recognized home for poll-driven routing; (b) move the router to
    its own hotkey_poll_router.cpp. Recommendation: lean (a), but this
    touches the most sensitive input path in the mod (and the unified-menu
    hard rule lives nearby), so decide explicitly. Verify: build; in-game
    input smoke test either way.
20. **Minigame prefix story** (B12): turret_game / swoop_race /
    swoop_spatial_audio / pazaak / minigame_aim don't share a family
    prefix. Options: (a) rename the three drivers to minigame_*;
    (b) keep names, document that minigame_ means "shared cross-minigame
    primitives" only. Recommendation: (b) — the rename is broad churn for
    a grep-ability win only.
21. **probe_ / diag_ convention** (B13, convention part): record the
    established meaning (probe_* = throwaway RE tooling; diag_settings /
    diag_chargen_feats = diagnostic-only; diag_focus / camera_spin_diag =
    production fix + diagnostics — do not treat their logging as
    removable). Recommendation: record in docs (e.g. a short section in
    docs/llm-docs/accessibility-map.md or a README note) rather than
    renaming files. No code change.
22. **probe_priority_groups.cpp is dead in practice** (B14): included by
    core_tick.cpp but its Tick()/DumpOnce() is called nowhere; its
    finding was folded into audio_bus.cpp. Decision: delete (git history
    preserves it) or keep. Recommendation: delete, since probe_* is by
    convention throwaway RE tooling and this one's job is done.

### Batch 5 — optional / lower priority (default: defer unless wanted)

23. **menus_listbox.cpp (1982) → + menus_listbox_picker.cpp** (A4, ~575
    lines of externally-observed picker specs). Real seam, smaller win.
24. **engine_radial.cpp (937) → + engine_radial_diag.cpp** (A9, ~370
    lines of debug-only logging).
25. **kdev smaller extractions** (C3/C4/C5): TlkFile out of
    CombatStringsExtractCommand; MSVC toolchain discovery out of
    BuildCommand; minidump-exception reader out of AnalyzeDumpCommand.
26. **kdev Core/ folder for shared infra** (K1): real namespace change,
    needs build verification; genuinely optional at current file count.
27. **swoop_spatial_audio.cpp split** (B6) and **turret_game reads
    extraction** (B10): assessed as optional; the files are coherent.
28. **Header splits mirroring the Batch-1 cpp splits** (A15):
    engine_player.h / engine_area.h / engine_panels.h / engine_reads.h
    could split to cut fan-out (63/49/39/35 includers). Optional because
    the cpp splits deliver most of the win; multiplies files touched.

## Considered and rejected (recorded so it isn't re-litigated)

- **strings.h split** (A11): enum bloat, not logic bloat; partitioning
  breaks the intentional parallel per-language switch design. No split.
- **Physical diagnostics subfolder / any patch subfolders** (B15 + build
  check): build is flat-glob; naming conventions already do the grouping.
- **Installer Forms/ folder** (I2): the sibling arena installer's flat
  layout is the intentional model.
- **Small-file merges** (B17–B20, A13, A14): minigame_aim, audio_pitch,
  party_cache vs party_leader_announce, filter_objects, tutorial_hints vs
  tutorial_popup, menus_skillflow_nav, menus_chargen_layout, strings.cpp,
  strfmt.h — all judged correctly scoped. No merges.
- **map_ui_cursor.cpp, spatial_change_detector.cpp,
  unified_action_menu.cpp, cycle_input.cpp, turret_game.cpp (core),
  pazaak.cpp, swoop_race.cpp, peek_description.cpp, hotkeys.cpp** (B7,
  B8, B10, B11 + scan notes): large but coherent; no splits.
- **GameLocale.cs split, MainForm method length as a Phase-1 item**
  (I3, I4): noted, not file-structure work.

## Carried forward to later phases

- Phase 2 (duplication/coupling): kdev BWM parsing duplicated between the
  two walkmesh commands; ResolveMgoArray-shaped helpers duplicated between
  turret_game and swoop_spatial_audio; T1/T2 coupling inside
  spatial_change_detector; examine_view effect-name tables vs the
  strings_* architecture (different keyspace — framing question only).
- Phase 2/3 (function decomposition): menus_extract.cpp FromControl
  (1473-line function — the most extreme in the codebase, not
  file-splittable, A3); menus_chain RebindChain (683 lines);
  MainForm.InstallButton_Click (~330 lines).

## Status

Awaiting user approval, item by item or per batch. No code has been
changed in Phase 1 so far; the only writes this phase are the four report
files and STATE.md.
