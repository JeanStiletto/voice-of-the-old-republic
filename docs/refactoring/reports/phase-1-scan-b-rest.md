# Phase 1 structure scan — everything except menus/menu_speak, engine_, strings/strfmt

Scope: all files in `patches/Accessibility/` except those starting with
`menus`/`menu_speak`, `engine_`, `strings`/`strfmt`, plus `hooks.toml`,
`allard.hooks.toml`, `manifest.toml`, `exports.def` (structure/organisation
only — no addresses, byte patterns, or `engine_offsets.h` values touched or
proposed). Method: read the 2026-07-27 code-index summaries for every
in-scope file, cross-checked line counts against
`docs/refactoring/file-inventory.txt`, opened real source only to confirm a
specific finding (seam locations, live-wiring via grep). Structure only —
no dead-code/stale-comment findings (Phase 3) and no cross-file
duplication/coupling findings (Phase 2) except one-line defer notes.

Findings are numbered B1, B2, ... in rough order of impact (large-file
splits first, then naming/placement, then config-file structure, then
small-file merge judgments).

## Large-file split candidates

### B1 — wall_topology.cpp (3404 lines) should split into 4-5 files along its own documented phases

This is the largest file in scope by a wide margin and the seams are
already implied by the file's own section comments and the code-index's
declaration order:

- Probe primitives + core state + public API (`ProbeWall`/`ProbeDistance`/
  `IsAlcoveAlongAxis`/`ProbeClearance8`, `AreaGraph` struct, `Reset`/
  `HasGraphForArea`/`GetClusterInfo`/`LookupAt`/`DumpGraphToLog`) — keep in
  `wall_topology.cpp`, roughly 500-600 lines.
- Door/transition snapshot + rendering (`SnapshotDoors`,
  `LogDoorSnapshotDetails`, `SnapshotTransitionTriggers`,
  `RenderTransitionExit`, `FindDoorOnEdge`, `FindDoorNearPoint`,
  `RenderDoorDirection`, `RenderCorridorAxis`, `WalkmeshAgreesDeadEnd`,
  `AttachLandmarksToDoors`) → `wall_topology_doors.cpp`, roughly 700 lines.
- Classification (`Degree`, `ComputeNodeShapeFeatures`, the ~530-line
  `ClassifyCluster`, `ComputeCentroidAxis`, `AxisOctantMask`, the
  union-find pair, direction-list helpers) → `wall_topology_classify.cpp`,
  roughly 900-950 lines.
- Build orchestration (`BuildForArea`'s ~780-line graph build,
  `MaybeRefreshDoors`) → `wall_topology_build.cpp`, roughly 850-950 lines.
- Diagnostics (`LogNavWallCrossings`, `LogTopologyMetrics`,
  `LogClusterMemberAdjacency`) → `wall_topology_diag.cpp`, roughly 300
  lines.

Caveat that makes this more than a pure mechanical move: the file leans
heavily on file-scope statics shared across these groups (`s_uf_parent[]`,
the `s_class_clear`/`s_class_door`/`s_class_blocked` counters, the cached
`AreaGraph` itself). Splitting cleanly needs a `wall_topology_internal.h`
declaring those as `extern` instead of file-static, which is still
mechanical but touches every function that reads them — worth a careful
pass, not a blind cut.

Risk: mechanical extraction, but the shared-static entanglement means a
missed `extern` silently produces a second copy of state (e.g. a second
union-find array) rather than a compile error in some cases — needs a
clean build plus an in-game pass through a nav-heavy area (Taris upper
city or Sewers) confirming perceptual-region narration is byte-for-byte
the same as before. Confidence: high on the seam identification (the
file's own comments already name these phases), medium on effort given
the shared-state work.

### B2 — combat.cpp (1491 lines) has a clean, already-labelled split at line ~343

The file's own code-index summary describes it as "two halves": Phase 1
(`TickCombatMode`/`TickCombatLog`, roughly the first 340 lines) tracks
combat-mode state and logs the message listbox; Phase 2 (from L315/343 on,
~1150 lines) is the entire `msg::Router` rule set that parses the
engine's localized combat-log lines (`ParseSummary`/`ParseAngriff`/
`ParseAbwehr`/`ParseSchaden`, the absorb/deflect/effect coalescing state
machines, `RegisterCombatMsgRules`, `TickCombatAbsorb`/`TickCombatDeflect`/
`TickCombatEffects`).

Proposal: keep `TickCombatMode`/`TickCombatLog`/`IsCombatActive`/
`IsPartyInCombat` (already exposed via `combat.h`) in `combat.cpp`
(~340-400 lines); move everything from `AttackBlock` through
`TickCombatEffects` into `combat_log.cpp` (fits the existing `combat_*`
family alongside `combat_queue`/`combat_query`/`combat_strings`/
`combat_diag`/`combat_special_watch`). The move needs an internal header
for the rule-file's own statics (`g_pending`, `g_absorb_*`, `g_deflect[]`,
`g_lastAbility`, `g_fx[]`) since `TickCombatAbsorb`/`Deflect`/`Effects` are
called from `core_tick.cpp` (out of scope) but only touch this file's own
state — no cross-seam sharing needed beyond the new file itself.

Risk: mechanical, low — no shared state crosses the proposed boundary
except through already-public functions. Confidence: high.

### B3 — examine_view.cpp (1530 lines) is ~700 lines of pure locale data tables that could move out

`EffectNameEn/De/Es/Fr/It` and `EffectIconNameEn/De/Fr/It/Es` (L67-775)
are flat `EFFECT_TYPES`/`effecticon.2da`-row-to-localized-name switch
tables — pure data, no control flow, no shared state with the
row-building logic below them. Splitting them into
`examine_view_effect_names.cpp` (keeping the `EffectName`/`EffectIconName`
public dispatchers there or leaving thin forwarders in `examine_view.cpp`)
would shrink `examine_view.cpp` itself to roughly 800 lines of actual
target-row-building/input logic (`BuildRows`, `HandleInputEvent`, `Tick`,
`ResolveFeatName`/`ResolveSpellName`, etc.).

Note: this is data-table content, not the same thing as the project's
existing `strings_<lang>.cpp` files (those map `strings::Id` → localized
UI text via `acc::strings::Get`; these tables map raw engine type/row ids
→ name, a different keyspace) — so this is a size-driven split proposal,
not a "should this just live in strings_de.cpp" duplication question
(that framing would be Phase 2).

Risk: mechanical, near-zero — pure data move, no logic touched. Confidence:
high.

### B4 — update_checker.cpp (994 lines) mixes generic WinHTTP/JSON plumbing with update-specific orchestration

`OpenSession`, `HttpGetToString`, `HttpGetRedirectLocation`,
`ParseTagFromLocation`, `HttpDownloadUrlToFile`, `SkipColon`/
`ReadQuotedString`, `StripTagToVersion`, `ExtractRawTagName`/
`ExtractTagName`, `ExtractAssetApiUrl`, and the `ParsedVersion`/
`ParseVersion`/`IsRemoteNewer` version-compare trio (L207-614, roughly 400
lines) are generic "talk to GitHub over WinHTTP, hand-parse JSON strings"
primitives with no reference to update-specific state. `CheckVersionWorker`/
`DownloadWorker`/`WriteHandoffBatch`/`SpawnHandoffBatch`/
`LaunchHandoffAndExit`/`StartBackgroundCheck`/`PollF5`/`Tick`/`HandleF5`
(L620-994) are the actual update orchestration and own all the atomic
state flags.

Proposal: extract the L207-614 block into `update_checker_http.cpp` (own
header declaring the GET/redirect/download/JSON-extraction primitives +
the version-compare trio), leaving `update_checker.cpp` at roughly 550-600
lines of worker-thread/handoff/Tick logic.

Risk: mechanical, low. Confidence: high.

### B5 — transitions.cpp (1412 lines): landmark-cache subsystem is a separable ~500-line chunk

`RebuildLandmarkCache`, `RecomputeLandmarkRanges`, `TryAutoDiscoverMapNote`,
`TickLandmarkCacheRecheck`, `TickProximityLandmarks`, `FindLandmarkNear`,
`IterateLandmarks`, `MarkLandmarkClaimedByDoor`, and the `Landmark`
struct/`g_landmarks[128]` cache (L130-990-ish, non-contiguous but a
coherent responsibility: "the flat landmark proximity cache") could move
to `transitions_landmarks.cpp`, leaving cluster-change/room-speech
(`ResolveRoomSpeech`, `SpeakRoomChange`, `TickPendingPlatz`,
`TickGatedClusterRefire`, the top-level `Tick()`, and the public API/
module-load hook) in `transitions.cpp` at roughly 900 lines.

This is a real seam but a looser one than B1-B4: `ResolveRoomSpeech`
reads the landmark cache too (friendly-room-name tier), so the split needs
a small header exposing 2-3 landmark-cache read functions across the
boundary. Risk: needs in-game verification (room/landmark narration text
must be unchanged, not just "compiles") in addition to a clean build.
Confidence: medium — the seam is real but the two halves aren't as
cleanly independent as B1-B4.

### B6 — swoop_spatial_audio.cpp (1426 lines): coherent enough to leave, optional split noted

`TickObstacleCues` (~200 lines) and `TickAccelpadCues` (~350 lines,
including the lateral steering magnet) are two distinct sweeps over the
same MGO array, called together from `TickSpatialAudio`/
`ResetSpatialAudio` and sharing `SpatialAudioState`, `ResolveMgoArray`,
`ReadTrackFollowerPosition`, `CallAsCast`, and `SwoopVolumeByte`. A split
into `swoop_obstacle_audio.cpp` + `swoop_accelpad_audio.cpp` sharing a
`swoop_spatial_common.h/.cpp` is possible (accelpad is the larger, more
tunable half — lateral magnet + predictive wall-overshoot cue — and would
benefit most from being isolated), but the shared read/resolve helpers and
combined reset/tick entry points mean the payoff is smaller than B1-B4.
Judged: optional, low priority. Confidence: medium.

### B7 — map_ui_cursor.cpp (1292 lines): assessed, no split warranted

Single coherent feature (the virtual map W/A/S/D cursor), already
internally organised (coordinate-transform primitives → hover-target
resolvers → `Tick`). No section duplicates itself and no natural
"this half doesn't need to know about that half" boundary the way B1-B4
do. Leave as one file.

### B8 — spatial_change_detector.cpp (1231 lines): assessed, no split warranted

T1 (distance-delta sector tracking) and T2 (front-cone foremost tracking)
are conceptually distinct but interleaved in one per-tick orchestration
(`Tick()` runs T1's four wall passes, the object scan, then T2's debounce,
sharing `CalibrateInRange`/`OnAreaChange`). The file already delegates the
heavy wall-cache/clustering work to `spatial_wall_surfaces.{h,cpp}` (its
own doc says "this file only reads through `ws::` accessors"), so it's
already had its natural split done. Leave as one file; defer any further
T1/T2 separation to Phase 2 if the coupling turns out to be worth
unwinding.

### B9 — interact_hotkey.cpp (1104 lines): PollHotkey is a general input router, not "interact hotkey" logic

Only `DispatchInteractImpl`/`ArmInteractApproach`/`ResolveInteractTarget`/
`ClassifyForInteract`/`OnInteract`/the bare-key announce helpers (roughly
650 lines) are genuinely about interact dispatch. `PollHotkey` itself
(L652-1104, ~450 lines) reads every Action rising edge and routes, in
order, to action-bar/target-row Shift+N openers, Shift+L level-up, bare
1-7 announce, `examine_view` input, `combat_queue` input,
`unified_action_menu` input, bare-R narration, and only *then* Enter/
Shift+Enter interact dispatch. That is a general per-tick Win32 hotkey
router for several unrelated subsystems, sitting in a file named for one
narrow feature — and it overlaps in charter with `input_pipeline.cpp`,
which already exists as "the" central input-pipeline module (its two
engine detours do the equivalent routing on the engine-hook side).

Proposal (naming/placement judgment call, not clear-cut): either (a) split
`interact_hotkey.cpp` into `interact_dispatch.cpp` (the ~650 lines that
are actually about interact) and fold `PollHotkey`'s routing shell into
`input_pipeline.cpp` so there's one recognized home for "poll-driven
cross-subsystem routing," or (b) rename the router half out to its own
file (e.g. `hotkey_poll_router.cpp`) if merging into `input_pipeline.cpp`
is undesirable for other reasons. Flagging this for a decision rather than
prescribing one — legitimate case either way. Risk: mechanical if done as
a pure move; Confidence: medium (this is a naming/responsibility judgment,
not a size-driven split).

### B10 — turret_game.cpp (2073 lines): assessed, largely coherent; minor optional extraction

The core per-tick targeting logic (`DriveSelectedPeg`) is intentionally
one continuous ~650-line function computing one physical solve (intercept
lead, curve correction, on-target/behind gates, aim-assist steer) — same
design pattern the file's own `Tick()` comment says mirrors
`swoop_race.cpp`. Splitting the FILE around that function doesn't reduce
its complexity, so this is not a strong split candidate the way B1-B4 are.
The one separable piece is the low-level read/resolve helper cluster
(`ReadMiniGameViaArea`, `IsTurretMiniGame`, `ResolveMgoArray`,
`ReadFollowerPosition`, `QuatRotate`, `ResolveGunModel`, `ReadAimLine`,
`ReadOffset`/`WriteOffset`, `PushLeadProbe`/`DrainLeadProbes`,
`NumberForSlot`, L620-900, ~280 lines) which has no dependency on the
tick/announce state machine below it — could become
`turret_game_reads.cpp`. Low priority; noting it mainly because
`swoop_spatial_audio.cpp` has near-identical `ResolveMgoArray`-shaped
helpers (that overlap is a Phase 2 duplication question, deferred).

### B11 — unified_action_menu.cpp (1102) and cycle_input.cpp (1047): assessed, no split warranted

Both are single-feature files already decomposed into many small
functions (unified_action_menu: per-kind dispatch helpers, category
build, arm paths, input handler; cycle_input: per-key handlers sharing one
`AnnounceCurrent` path). Length comes from breadth of small functions, not
from an unsplit monolith. Leave as-is.

## Naming / module-prefix / misplaced-responsibility findings

### B12 — no coherent "minigame" prefix story

`minigame_aim.{h,cpp}` is the shared cross-game primitive layer (offset
read/write + magnetism curve), explicitly documented as such. But the
three actual minigame drivers don't share a family prefix with it or with
each other: `turret_game.cpp` (subject + `_game` suffix), `swoop_race.cpp`
+ `swoop_spatial_audio.cpp` (subject-first, two different suffixes),
`pazaak.cpp` (bare, no prefix at all). A reader grepping `minigame_*`
finds only the shared primitives, none of the three games themselves.

Not proposing a rename in Phase 1 — renaming would touch every includer
of `turret_game.h`/`swoop_race.h`/`swoop_spatial_audio.h`/`pazaak.h`
(roughly 10-15 include sites total, mechanical but broad) for a
cosmetic/grep-ability win only. Flagging as a decision point: if the team
wants `minigame_*` to mean something, the options are (a) rename the three
drivers to `minigame_turret.cpp`/`minigame_swoop_race.cpp`/
`minigame_swoop_spatial_audio.cpp`/`minigame_pazaak.cpp`, or (b) leave
`minigame_` meaning specifically "shared cross-minigame primitives" (its
current, narrower meaning) and document that explicitly instead of
touching filenames. Confidence: high that the inconsistency exists, no
strong recommendation on which fix is worth the churn.

### B13 — probe_* vs diag_* is not a clean distinction; three "diag_" files are actually production modules

The six `probe_*` files (`probe_pathfind`, `probe_camera_distance`,
`probe_mouselook`, `probe_priority_groups`, `probe_camera_state`,
`probe_audio_frame`) are uniformly one-shot or hotkey-triggered (F9-F12,
Ctrl+F11/F12) reverse-engineering investigation tools that dump state to
the log — none of them contain a shipped behavioral fix. That's a clean,
trustworthy convention.

`diag_*` is not used the same way:

- `diag_settings.cpp` (startup ini/DLL/Override-count dump for support-log
  bundles) and `diag_chargen_feats.cpp` (on-demand chargen-feats panel
  dump, called from `menus.cpp`) match the "probe" pattern — genuinely
  diagnostic-only, name fits.
- `diag_focus.cpp` is a load-bearing production module: it drives
  DirectInput reacquire/release on foreground change, runs the cold-start
  foreground-reclaim guard, and detects+warns on Steam Big Picture eating
  keystrokes. The window-activity logging is real, but the *point* of the
  file is the fix, not the log.
- `camera_spin_diag.cpp` similarly bundles a genuine production bug fix
  (the edge-turn spin guard: once a live spin is confirmed, it writes the
  GUI manager's mouse-X field directly to viewport centre) under a "diag"
  name; the quaternion-vs-position yaw cross-check is the actual
  diagnostic part, and it's the minority of the file's purpose.
- `combat_diag.cpp` is the strongest case: `OnCombatRoundAddAction` (one
  of its four hook handlers) forwards to
  `combat_queue::OnEngineActionAdded` — this is the mechanism that drives
  the shipped "X, Platz N" / "Warteschlange voll" combat-queue announce
  feature, not a diagnostic. Only `ReadState`/`Tick` (the INIT/DELTA
  snapshot logger) and the other three hook loggers are genuinely
  diagnostic.

Proposal: no rename urged for Phase 1, but worth recording the
distinction precisely for whoever next touches these: `probe_*` = safe to
treat as throwaway RE tooling; `diag_settings`/`diag_chargen_feats` = safe
to treat as diagnostic-only; `diag_focus`/`camera_spin_diag` = production
fix + diagnostics, do not remove the "diagnostic-looking" logging without
checking it isn't also the trigger path; `combat_diag.cpp` specifically
would benefit from splitting `OnCombatRoundAddAction` (and the
`ReadQueueSize`/`ReadOuterQueueSize` helpers it and `combat_queue`'s
announce logic both need) into a `combat_queue_hooks.cpp` living next to
`combat_queue.cpp`, leaving `combat_diag.cpp` as the genuinely
diagnostic-only state-snapshot file (`ReadState`/`Tick` plus the three
pure-logger hooks). Risk: mechanical if done, but touches a hook handler
so needs an in-game combat-queue announce check, not just a build. Medium
priority. Confidence: high on the "diag_ doesn't mean one thing" finding,
medium on the specific combat_diag.cpp split recommendation.

### B14 — probe_priority_groups.cpp is wired in but never actually called (confirmed dead in practice)

`core_tick.cpp` `#include`s `probe_priority_groups.h`, but grepping the
whole `patches/Accessibility/` tree for `probe_priority_groups::` finds no
call to `Tick()` or `DumpOnce()` anywhere — not in `core_tick.cpp`'s
`Dispatch()`, not from `OnRulesInit`, nowhere. The only other reference is
a comment in `audio_bus.cpp` ("Layout mirrors probe_priority_groups.cpp"),
which reads like the investigation concluded and its finding got folded
into `audio_bus.cpp`'s own priority-group handling — the probe's job is
done. (Compare: the other five `probe_*` files' `Tick()`/`PollWin32()`
calls are all present in `core_tick.cpp`'s `Dispatch()`, confirmed live.)

This is exactly the "still live or diagnostics-graveyard candidate"
question the scan was asked to answer for these files. Not proposing
deletion (that's the user's call per the task brief) — flagging it as the
clearest concrete instance of the diagnostics-area question below.

### B15 — a physical "diagnostics" subfolder would require a build-script change, not just a file move

If the intent behind grouping `probe_*`/`diag_*`/`camera_spin_diag`/
`combat_diag` is a dedicated diagnostics area, note that
`patches/Accessibility/` is entirely flat today (confirmed via directory
listing) and `tools/kdev/Commands/BuildCommand.cs` discovers sources via
`Directory.GetFiles(dir, "*.cpp")` — non-recursive. Moving files into a
subdirectory (e.g. `patches/Accessibility/diag/`) would silently drop them
from the build unless `BuildCommand.cs` is changed to recurse at the same
time. If a diagnostics grouping is wanted, doing it via naming convention
only (per B13, keeping the directory flat) is lower-risk than a physical
subfolder unless the build-script change is scoped into the same unit of
work.

## Config-file structure

### B16 — hooks.toml (1214 lines, ~25 hooks): section banners exist once but aren't applied consistently

The file already demonstrates a banner-comment convention exactly once
(L891-903, a `# ====...====` block introducing the combat-queue
diagnostic hooks) but nowhere else across its ~25 `[[hooks]]` entries.
The rest of the file reads as a roughly chronological add-order list —
some natural clustering exists by accident (the four
`OnCombatRoundAdd/RemoveAllActions/SetCurrentAction/RemoveLastAction`
hooks sit together, as do `OnTurretBulletHit`/`OnPlayerFire`), but there's
no navigational marker grouping hooks by subsystem (bring-up/input,
GUI/panel routing, audio, world/nav, combat, minigames).

Proposal: apply the same banner style the file already uses once, at each
natural subsystem boundary, purely as comments — zero functional risk,
no address/byte-pattern/parameter changes. Confidence: high, very low
effort, purely additive.

`manifest.toml` (25 lines) and `exports.def` (27 lines) are both small
enough that structural findings aren't warranted — `exports.def` is a
flat linker export list with no grouping, but at 24 entries adding
`;`-comment banners would be cosmetic at best; not worth flagging as an
action item.

## Small-file merge judgments (explicitly requested)

### B17 — minigame_aim.{cpp,h} (48/68 lines) and audio_pitch.{cpp,h} (78/28 lines): judged, no merge warranted

- `minigame_aim` is a deliberately extracted shared primitive (SEH-guarded
  offset read/write + the proximity-ramped magnetism curve) consumed by
  two otherwise-unrelated files (`turret_game.cpp`, `swoop_spatial_audio.cpp`).
  Its own header explicitly documents the shared-vs-per-game split
  rationale. Small size reflects genuine minimalism of the primitive, not
  premature fragmentation — merging it into either consumer would make
  that consumer own a primitive the other one also needs.
- `audio_pitch` is a single hook handler (`OnCalculatePitchVarianceFrequency`)
  plus the thread-local scoped-flag mechanism that gates it, with a narrow
  load-bearing contract (must bracket exactly the engine calls
  `audio_bus.cpp` issues). It's a hook-handler module (needs
  `hooks.toml`/`exports.def` wiring) sitting next to a helper module
  (`audio_bus.cpp`, which calls into it) — merging would mix those two
  concerns into `audio_bus.cpp`'s own charter. Keep separate.

### B18 — party_cache.cpp (113) + party_leader_announce.cpp (99): judged, no merge warranted

Different lifecycles and no shared state today: `party_cache` is
polled/lazily-refreshed background state feeding the combat message
filter (`IsPartyMember`); `party_leader_announce` is an edge-triggered
Tab-press speech feature with its own multi-tick pending-window state
machine. Nothing in either file references the other. Keep separate.

### B19 — filter_objects.cpp (90 lines): judged, no split/merge warranted

It's the single source of truth for "is this a Pillar 4 vocabulary
object," explicitly documented as inherited by `cycle_input`, T1/T2
(`spatial_change_detector`), and `passive_narrate`. Small size is
appropriate for a shared foundational classifier, not a sign it's an
orphaned fragment.

### B20 — tutorial_hints.cpp (293) vs tutorial_popup.cpp (233): judged, no merge warranted

Legitimately separate concerns: `tutorial_hints` is pure lookup-table data
(strref → hint text, three separate tables) with TLK-resolution logic;
`tutorial_popup` is the UI-mounting mechanism (calls
`ShowTutorialWindow`/`SetTutorialReason`/`SetMessage`, manages the
once-shown bitfield and pause state). Data module vs. mechanism module —
keep separate.

## Also reviewed, no findings

Read in full or in code-index summary and found no structural issue worth
flagging: `door_announce.cpp` (78, tiny but a legitimate one-hook-per-file
module matching the codebase's convention for `OnDoorOpen`/`locked_recall`/
etc.), `peek_description.cpp` (797, large but coherent — a single
table-driven feature across many panel kinds, no duplicate/dead halves
found), `hotkeys.cpp` (763, bulk is the necessary one-line-per-Action
`InitDefaults()` table), `help.cpp` (442), `view_mode.cpp` (744),
`floor_puzzle.cpp` (634), `core_dllmain.cpp` (340, bring-up plumbing that
genuinely belongs together), `core_settings.h` (80, header-only tunables
struct, no `.cpp` needed), `state_overrides.cpp` (172),
`mod_settings_store.cpp` (154), `camera_orient.cpp`/`camera_announce.cpp`/
`camera_spin_diag.cpp` (a coherently-sized `camera_*` family), the
`guidance_*` family (pathfind/autowalk/approach/beacon/description, all
194-376 lines, reasonable per-concern separation), `swoop_race.cpp` (882,
coherent single-lifecycle file), `pazaak.cpp` (884, coherent single-panel
driver), `msg_router.{h,cpp}` (55/185, clean small dispatcher),
`input_pipeline.cpp` (595, coherent).

## Summary of action items by risk/effort

Mechanical, low-risk, high-confidence (safe to schedule anytime):
B2 (combat.cpp), B3 (examine_view.cpp), B4 (update_checker.cpp), B16
(hooks.toml banners).

Mechanical but with shared-state entanglement (needs careful extraction +
in-game narration check, not just a build): B1 (wall_topology.cpp), B5
(transitions.cpp).

Judgment calls needing a decision before any code moves: B9
(interact_hotkey.cpp vs input_pipeline.cpp charter), B12 (minigame naming
family), B13 (diag_/probe_ meaning + combat_diag.cpp split).

Low priority / optional: B6 (swoop_spatial_audio.cpp), B10
(turret_game.cpp reads extraction).

Informational only (no action proposed): B7, B8, B11, B14, B15, B17-B20.
