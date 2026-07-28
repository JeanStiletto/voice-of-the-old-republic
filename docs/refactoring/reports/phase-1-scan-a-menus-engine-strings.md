# Phase 1 structure scan — menus*, engine_*, strings*, strfmt.h

Scope: every file in `patches/Accessibility/` whose name starts with
`menus` (including `menu_speak.*`), `engine_`, or `strings`, plus
`strfmt.h`. Method: read the 2026-07-27 code-index summary for all 96
files in scope, cross-checked against `docs/refactoring/file-inventory.txt`
line counts, then opened source only to confirm exact function boundaries
for the findings below (grep on function signatures, not full reads).
Structure only — no dead-code, duplication, or behavior findings; one-line
"defer to Phase 2/3" notes where something relevant surfaced in passing.

## General note on split safety

For every `.cpp`-only split proposed below, the public interface a caller
sees is the unchanged header — moving a function's *definition* to a
different `.cpp` file is invisible to every includer as long as the
declaration stays where it is (or, for file-static/anonymous-namespace
helpers, as long as a small header is added). This makes `.cpp`-side
relocation mechanical almost by construction; I call out the exceptions
(de-staticizing, new headers, non-contiguous cuts) explicitly per finding.
For the one header split proposed (A10, `engine_offsets.h`), the safe
technique is to turn the original header into a thin aggregator that
`#include`s the new narrower headers — every existing includer keeps
compiling unchanged, and only future call sites need to pick the narrower
header.

## Findings

### A1 — menus.cpp (2269 lines): hook glue, detail helpers, and
first-sight/focus logic are interleaved in one TU

The file's own header says the earlier refactor (Steps 1-5) pulled
listbox handlers, chain state, and monitors into sibling TUs and "this
file kept the hook glue." Confirmed by declaration order that three
distinct concerns remain fused:

- `acc::menus::detail::*` implementations that `menus_internal.h` already
  declares as the shared cross-TU seam (used by `menus_listbox.cpp` and
  `menus_keymap.cpp` too, not just this file): `GetControlCenter` (L346),
  `GetListBoxRowScreenCenter` (L366, file-static helper for the same
  purpose), `IsChainNavigable` (L391), `FindControlById` (L603),
  `IsSaveLoadPanel` (L651), `ReadSaveLoadEntryString` (L687),
  `DriveListBoxSelection` (L709), `DriveListBoxSelectionEngine` (L772),
  `QueueButtonByIdActivate` (L843), `IsClassSelectionIcon` (L866),
  `ClassLabelCacheLookup`/`Store` (L891-993), and — scattered all the way
  at the tail of the file, away from its siblings — `FindListBoxChild`
  (L2054). ~490 lines total.
- First-sight / focus-capture announce logic, only ever called from
  `OnSetActiveControl`: `AnnouncePanelTitle` (L413, 190 lines),
  `PrefillClassIconCacheOnTransition` (L993), `UpdateFocusedPanelState`
  (L1024), `WalkAndCaptureOnFirstSight` (L1035),
  `SpeakPanelTitleOnFirstSight` (L1104), `AnnounceNewFocusedControl`
  (L1129). ~396 lines total.
- The actual hook glue: `OnSetActiveControl` (L1199, 122 lines),
  `OnListBoxSetActiveControl` (L1321, 184 lines), `OnHandleFocusChange`
  (L1505, 23 lines), and `OnHandleInputEvent` (L1528, **526 lines** — the
  single largest function in the file by a wide margin, the ordered gate
  list that tries mod-settings/help/peek/Fähigkeiten/listbox/editbox/
  chargen/cycle/Pazaak/galaxy/keymap before falling to chain nav), plus
  the public per-tick surface `ValidatePanels`/`TickMonitors`/
  `PollHomeEndKeys`/`TickPendingOps`/`DrainPendingAnnounce`/
  `ClearPendingAnnounce`/`IsDrilledIntoSubScreen`/`SetDrilledIntoSubScreen`
  (L2086-2269).

Proposal (three new sibling TUs, same naming convention as the existing
`menus_listbox`/`menus_chain`/`menus_pending` split):

- `menus_internal.cpp` (NEW, ~500 lines) — move every `detail::` function
  listed above verbatim. These are already declared in `menus_internal.h`,
  so this is the header finally getting the matching `.cpp` every other
  seam header in this directory has (`menus_chain.h`/`.cpp`,
  `menus_pending.h`/`.cpp`, `menus_listbox.h`/`.cpp`,
  `menus_editbox.h`/`.cpp`, `menus_monitors.h`/`.cpp`). Also consolidates
  `FindListBoxChild` next to its siblings instead of living 1400 lines
  away from them. `menus_internal.h`'s own comment ("most in menus.cpp")
  would need a one-line update after this move.
- `menus_focus.cpp` (NEW, ~400 lines) + a small `menus_focus.h` — move the
  first-sight/focus-capture block. These functions are currently
  `static`/anonymous-namespace, so this split needs an extra step beyond
  A1's other two pieces: de-staticize them and declare them in the new
  header so `OnSetActiveControl` (staying in `menus.cpp`) can still call
  them. Still no logic change.
- `menus_dispatch.cpp` (NEW, ~550 lines) — move `OnHandleInputEvent` +
  `OnHandleFocusChange` verbatim (both already `extern "C"`, no header
  change needed beyond what `menus.h` already exposes).
- `menus.cpp` shrinks to ~830 lines: `s_lastSpoken` dedup, core globals
  (`g_currentPanel`, `g_drilledIntoSubScreen`, pending-announce state),
  `OnSetActiveControl`, `OnListBoxSetActiveControl`, and the public
  per-tick surface.

Risk: the `menus_internal.cpp` piece is mechanical/low-risk (pure
relocation into an already-declared seam). The `menus_focus.cpp` piece is
mechanical but touches more surface (de-staticize + new header) — still
low behavioral risk. The `menus_dispatch.cpp` piece is a single
copy-paste move, but `OnHandleInputEvent` is the single most
game-critical dispatch function in the whole mod (every keypress in every
menu funnels through it), so recommend an in-game smoke test across a
few menu shapes (inventory, dialog, chargen, a listbox screen) after this
specific move even though the diff itself has zero logic changes.
Confidence: high on the boundaries (verified against source, not just the
index).

### A2 — menus_chain.cpp (1827 lines): chain construction and chain input
handling are two distinct responsibilities in one TU

Confirmed via grep: `RebindChain` (L419-1102, **683 lines** — a single
function) plus the small helpers around it (`ComputeTabClickOffsetY`,
`IsModalTextPanel`, `IsPanelLive`, `ReadPanelActiveControl`,
`FindChainEntry`, `SpeakLevelUpDoStepFirst`, `DetectTabsCluster`,
`ResetTabbedState`, `RebindChainPreserveIndex`, `InvalidateChain`,
`ValidateTabbedPanel`, `ValidateChainPanel`, `IsTabButton`,
`FindAdjacentArrow`, `FindCloseButton`, `FindCancelButton`,
`AppendChainEntry`, `AppendChainTextOnly`) make up the "build/maintain the
chain" half, ~1080 lines. The four input handlers —
`HandleEnterActivation` (L1102, 295 lines), `WalkChildren` (L1397, 60
lines), `HandleNavStep` (L1457, 159 lines), `HandleLeftRight` (L1616, 77
lines), `HandleEsc` (L1693, 134 lines) — are the "consume input against an
already-built chain" half, ~725 lines, and are already declared
individually in `menus_chain.h`.

Proposal: `menus_chain_input.cpp` (NEW, ~725 lines) — move
`HandleNavStep`, `HandleEnterActivation`, `HandleLeftRight`, `HandleEsc`,
`WalkChildren` verbatim; no header change needed, `menus_chain.h` already
declares all five individually. `menus_chain.cpp` shrinks to ~1100 lines
(state + small helpers + `RebindChain`/`RebindChainPreserveIndex`/
`InvalidateChain`/`Validate*`).

One-line defer note: `RebindChain` itself stays a 683-line single
function after this split — decomposing it into named per-concern steps
(decorative filter / virtual-row registration / y-sort) is a Phase 2/3
function-decomposition question, not a file-structure one; flagging here
so it isn't lost.

Risk: mechanical (whole named functions, already individually declared in
the existing header). Confidence: high.

### A3 — menus_extract.cpp (1897 lines): the file is essentially one
1473-line function

`FromControl` runs from L410 to L1883 — nearly the entire file. Everything
before it (L51-406, ~360 lines: portrait table, cycle-category cache,
sibling-label finder, sound-options slider fingerprint, Pazaak wager row
extraction) is preamble feeding into it. This is the most extreme
single-function bloat found in scope, but it is **not mechanically
splittable at the file level today** — there is nothing to cut-and-paste,
because it is one function, not several. A real fix requires first
decomposing `FromControl`'s sequential ladder (virtual-anchor
short-circuits → per-kind virtual-row formatters → tooltip → standard
control-kind reads → speculative vtable-override reads → per-kind
hardcoded legacy fallbacks → spatial sibling fallback → suffix appends)
into named sub-functions — that is function-level refactoring, which this
scan's brief places in Phase 2/3, not Phase 1. Flagging now so the
finding isn't lost: once decomposed, the "per-kind hardcoded fallback"
tail and the "speculative vtable-override" section look like the two most
promising pieces to eventually move to sibling files (mirroring the
already-existing pattern of `menus_charsheet`/`menus_credits`/
`menus_equipstats` as dedicated virtual-row extractors).

Risk: N/A (no proposal to execute in Phase 1). Confidence: high that this
is real, medium on the eventual decomposition shape (would need to read
the full function body to firm up sub-function boundaries).

### A4 — menus_listbox.cpp (1982 lines): externally-observed "armed"
picker specs could split from the purely-internal spec table

Verified via grep: the file's 13-entry `kSpecs` table (L1440) and its
generic `TryHandleInput` walker are the dispatcher core. Two of the
thirteen specs — EquipPicker and WorkbenchUpgrade — are qualitatively
different from the other eleven: they carry armed/bound state
(`s_equipPickerActive`/`s_equipPickerPanel`,
`s_workbenchUpgradePickerActive`/`Panel`, L74-133) with public
`Arm*`/`Disarm*`/`Is*Armed` accessors that `menus_listbox.h` documents as
having "two outside touch sites in menus.cpp" — i.e. this state is
observed outside the file, unlike Container/SaveLoad/SkillInfoBox/
InGameMessages/the 4 Dialog specs/WorkbenchItems/Examine/ScriptSelect,
which are purely internal to the dispatcher.

Proposal: `menus_listbox_picker.cpp` (NEW, ~575 lines) — move the
EquipPicker/WorkbenchUpgrade state block (L74-133, 59 lines), the
EquipPicker spec + callbacks (L433-571, 138 lines), the WorkbenchUpgrade
spec + callbacks (L1048-1227, 179 lines), `ParkPickerCursorOffList`
(L1740-1757, 17 lines), `MonitorEquipPickerSelection` (L1757-1862, 105
lines), and `MonitorWorkbenchUpgradePicker` (L1862-1939, 77 lines).
`menus_listbox.h`'s `IsEquipPickerArmed`/`ArmEquipPicker`/etc. declarations
don't need to move. `menus_listbox.cpp` shrinks to ~1400 lines (dispatcher
core + the 11 purely-internal specs + `MonitorContainerSelection` +
`PollContainerGiveModeKey`, which stay because Container isn't externally
observed).

Risk: mechanical (named functions + statics, already declared where
needed). This is a smaller win than A1/A2/A5-A7 — the remaining
1400-line dispatcher is close to a reasonable size for "one generic
table-driven walker over N panel specs," so treat as lower priority.
Confidence: medium-high (boundaries grep-verified; judgment call on
where exactly to draw the "externally observed" line).

### A5 — engine_area.cpp (1901 lines): three distinct subsystems share one
TU — object model, map/fog-of-war, and walkmesh wall-edge extraction

Verified via grep. Three clean blocks in source order:

- Core object model (L1-1009, ~1009 lines): object iteration, handle
  resolution (server + client namespaces), naming/tagging, door/waypoint/
  trigger sub-state, `AreaObjectIterator` (ctor at L319).
- Map/fog-of-war/map-pin CRUD (L1021-1318, ~297 lines): `GetServerApp`,
  `ReadGlobalNumber`, `IsLoadingSaveGame`, `IsWorldPointExplored`,
  `GetFogCellSizeM`, `GetMapRotateCCWFromWorldOrientation`,
  `GetClientArea`, `GetMapPinCount`/`At`/`Position`/`Flags`/
  `IsMapPinEnabled`/`GetMapPinNoteText`, `CreateMapPin`. (`IsMapNoteEnabled`/
  `EnableMapNote`/`GetWaypointMapNote`, just before this block at L958-1009,
  fit here too — same "map" concept even though they're object-state
  reads.)
- Walkmesh wall-edge extraction pipeline (L1318-1870, ~552 lines):
  `GetRoomSurfaceMesh`, `ReadFaceVertexIndices`, `TransformEdgeEndpoints`,
  `ScanRoomWallEdges` (L1380), the global triangle-edge index statics,
  `ScanRoomAllTriangleEdges` (L1503), `BuildAreaWallCache` (L1561),
  `SegmentCrossesWalkmesh` (L1795). This is the Pillar-1 foundation feeding
  `wall_topology.cpp` (out of scope, not touched here).

Caveat: `AreaObjectIterator::Next()` is defined out-of-order at the very
tail of the file (L1870-1901), after the walkmesh block, even though the
class's constructor lives in the core section (L319). When splitting,
move `Next()` back with the core block, not with walkmesh.

Proposal: `engine_area_map.cpp`/`.h` (NEW, ~300 lines) and
`engine_area_walls.cpp`/`.h` (NEW, ~555 lines), leaving `engine_area.cpp`
at ~1045 lines (core object model). New headers mirror the existing
`WallEdge` struct and map-pin declarations currently in `engine_area.h` —
splitting the header is optional/secondary (see A15) since 627 lines
isn't itself flagged as oversized.

Risk: mechanical (three self-contained blocks, minimal cross-block
coupling — the walkmesh block only needs `Vector`/basic area-resolution
already available generically). Confidence: high.

### A6 — engine_reads.cpp (1026 lines): generic GUI-control reading vs.
item/creature-domain reading

Verified via grep. `namespace acc::engine` opens at L11; generic control
readers (`ReadControlNameFields`, `CallDowncast`, `ReadCExoString`,
`ReadU32`, `LookupTlk`, `ExtractTextOrStrRef[Indirect]`,
`ReadControlTooltip`, `ReadGuiString`, `ReadLabelText`, `ReadButtonText`,
`IsToggle`/`Slider`/`ListBox`/`Editbox`, `ReadToggleState`,
`DumpControlVtable`) run L13-361 (~350 lines). Everything from
`ClientToServerObjectId` (L366) through `ReadCreatureForcePoints` (L974,
ending at L1026) is item-property-description reconstruction, workbench
slot/picker reads, and Force-point reads — a self-contained domain the
file's own summary calls out as grown "well past the original GUI-control
readers."

Proposal: `engine_reads_items.cpp`/`.h` (NEW, ~665 lines) — move
everything from `ClientToServerObjectId` onward (item resolution, feat/
spell/action description resolvers, item stack/charge reads, the
property-description block-offset reconstruction, workbench slot/picker
reads, Force-point reads). `engine_reads.cpp` shrinks to ~360 lines
(generic control readers only). Given 35 includers of `engine_reads.h`
today, many of which likely only want `ReadLabelText`/`ReadButtonText`/
`IsToggle` etc., splitting the header too would cut real fan-out (see
A15).

Risk: mechanical (single contiguous cut, clean domain boundary).
Confidence: high.

### A7 — engine_player.cpp (843 lines): three responsibilities
interleaved in source order

The file's own summary names three responsibilities — confirmed via grep,
but note the party/leader functions are **not contiguous**, they're
interleaved between two chunks of the input-disable state machine:

- Core position/camera/area reads (L1-176, 176 lines):
  `GetPlayerServerObject`, `GetPlayerPosition`, `GetPlayerFacing`,
  `GetPlayerYawDegrees`, `GetPlayerArea`, `GetCameraPosition`,
  `GetCameraYawRadians`.
- Party/leader resolution (L176-430, 254 lines + L558-767, 209 lines =
  463 lines total): `GetPlayerServerCreature`, `GetClientLeader`,
  `SetLeaderQueueModeBit`, `IsAnyPartyMemberInCombat`,
  `GetActiveLeaderName`, `GetPlayerCharacterName`, then later
  `GetPartyMembers`, `GetServerPartyTable`, `GetSoloMode`,
  `PartyTableIsNPCAvailable`/`Selectable`, `kCompanionNamesBySlot`,
  `GetPartyNpcNameForSlot`.
- Input-disable/auto-restore state machine (L430-498, 68 lines +
  L498-558, 60 lines + L767-843, 76 lines = 204 lines total): session
  statics, `GetPlayerControl`, `GetPlayerActionQueueDepth`,
  `SetPlayerInputEnabled`, `TickPlayerInputRestore`, `TickActionQueueDiag`.

Proposal: `engine_player_party.cpp`/`.h` (NEW, ~465 lines) and
`engine_player_inputlock.cpp`/`.h` (NEW, ~205 lines), leaving
`engine_player.cpp` at ~175 lines (core reads only). Because the
party/leader and input-lock functions interleave in source order, this
split needs two non-contiguous cuts per new file rather than one clean
slice — still purely mechanical (no logic touched), just more surgical
than A2/A5/A6.

Risk: mechanical, but flag the non-contiguous cut explicitly when
executing so nothing is dropped. Confidence: high on the grouping,
medium-high on exact line ranges (interleaving makes off-by-a-few-lines
errors easier here than in the other findings).

### A8 — engine_panels.cpp (1182 lines): panel-identity registry vs.
foreground-blocking/input-class primitives

Verified via grep. `IsPanelKindInGameMenu` (L779) through
`IsModalPopupPanel` (L800-815) are still classification predicates
(PanelKind-based). The genuinely distinct second half starts at
`HasActiveDialogPanel` (L815) and runs through `IsForegroundUiBlocking`
(L1121-1182): `HasActiveBarkBubble`, `CallPrevSWInGameGui`,
`CallHideSWInGameGui`, `SetGlobalDialogState`, `CloseInGameMenuToWorld`,
`GetInputClass`, `SetGuiInputClass`, `HasActiveMapPanel`,
`HasActiveLevelUpPanel`, `IsInGameOptionsSubScreen`, `HasActiveSubScreen`,
`IsForegroundUiBlocking` — the header's own summary already calls this
out as something the file "also owns" beyond the core identity registry
("the render-independent dialog-reply-text reader, the foreground/
UI-blocking model, and input-class plumbing").

Proposal: `engine_panels_state.cpp` (NEW, ~370 lines) for the
foreground-blocking/input-class/pause-primitive half, declared either in
a new small header or appended to the existing `engine_panels.h` (simpler,
avoids header churn — this isn't the file's fan-out driver, most of
`engine_panels.h`'s 39 includers likely want `PanelKind`/`IdentifyPanel`
either way). `engine_panels.cpp` shrinks to ~815 lines (structural
detectors, `kPanelKindOffsets` table, `PanelKindName`, `ResolveGuiInGame`,
dialog-reply readers, unknown-panel dumper, `IdentifyPanel`, and the
PanelKind-based classification predicates).

Risk: mechanical (contiguous cut, boundary matches the doc's own framing).
Confidence: high.

### A9 — engine_radial.cpp (937 lines): diagnostics could split from the
operational surface (lower priority)

Verified via grep: `LogState` (L405, 61 lines), `LogStateWide` (L505, 82
lines, plus its private helpers `ReadResRefLocal`/`ReadButtonText` at
L466, 39 lines), `CallVtableAsClass` (L732, 23 lines, used only by
`LogTargetDiag`), and `LogTargetDiag` (L755, 164 lines) total ~369 lines
of debug-only logging that never participates in the operational
Select/Dispatch/Retarget path. `IsCreatureClientTarget` (L919) is used by
callers to decide row semantics, not diagnostics-only, so it stays.

Proposal: `engine_radial_diag.cpp` (NEW, ~370 lines) for the four
functions above; `engine_radial.cpp` shrinks to ~570 lines. Lower
priority than A1/A2/A5-A8 — 937 lines is large but not in the same tier
as the others in scope, and the diagnostics/operational boundary is a
nice-to-have rather than a load-bearing seam.

Risk: mechanical. Confidence: high on the boundary, medium on priority
(judgment call that this is worth doing at all in Phase 1).

### A10 — engine_offsets.h (1820 lines, 77 includers): monolithic
constants header forces every consumer to rebuild on any subsystem's
offset change

Pure constants, zero behavior — the file's own section index already
documents ~13 natural per-subsystem blocks (GUI control primitives →
chargen panels → generic panel/listbox container → equip/workbench →
combat → creature/inventory → in-game sub-screens → store → item
property-description builders → journal). Every one of the 77 includers
currently pays a rebuild whenever ANY block changes, even ones for a
subsystem they never touch. The task brief explicitly allows regrouping
this file ("numeric values in engine_offsets.h" must not change, but
"regrouping/renaming is fine").

Proposal: split into subsystem-grouped headers matching the file's own
section index, e.g. `engine_offsets_gui.h` (control primitives + text/
listbox/editbox + panel container layout), `engine_offsets_chargen.h`,
`engine_offsets_combat.h` (combat + creature/inventory + effects),
`engine_offsets_screens.h` (equip/workbench/abilities/levelup/journal/
store/dialog-messages), `engine_offsets_items.h` (property-description
builders). Critically, keep `engine_offsets.h` alive as a thin aggregator
that `#include`s all five — every one of the 77 existing `#include
"engine_offsets.h"` sites keeps compiling completely unchanged; only new
or refactored call sites would be encouraged to include the narrower
header. This is the safest possible version of this split.

Risk: mechanical (pure constant relocation, no logic, zero risk to
behavior) but a large diff (many constants to move + verify no name
collisions across the new headers, though collisions are unlikely since
these are already globally-unique file-scope names). Confidence: high on
safety, medium on the exact section boundaries (would want a full pass
over all ~1820 lines before executing, not just the section index, to
catch cross-block constants that got interleaved).

### A11 — strings.h (2003 lines, 72 includers): reviewed, NOT recommending
a split

The girth is one `enum class Id` with ~700 comment-grouped entries plus a
handful of tiny functions (`Get`, `SetLanguage`/`GetLanguage`,
`CodepageFor`). This is fundamentally different from the other "gigantic
file" findings — it isn't logic bloat, it's enum bloat, and the
project's own centralise-user-facing-strings convention (memory:
"no hardcoded literals; logs English, speech localised via Get(Id)")
means every feature across the whole mod legitimately needs some subset
of this one Id namespace. A real split would require either partitioning
the enum (breaks the single namespace every `switch (id)` in
`strings_de/en/fr/it/es/ru.cpp` depends on — those six files' parallel
per-language switches are explicitly called out as intentional and
out-of-scope for this scan) or introducing a translation/indirection layer
(pure churn, no coupling reduction, since consumers of `Id` values are
scattered across the whole codebase, not clustered by file). Splitting the
header would touch all 72 includers for no payoff since almost every
consumer needs `Id` + `Get()` together regardless of which section their
particular ids live in.

Risk: N/A — no change proposed. Confidence: high that a split isn't
worth it; recording this so it isn't re-litigated in a later phase.

### A12 — menu_speak.cpp/.h: naming breaks the `menus_*` convention

`menu_speak.cpp` (27 lines) and `menu_speak.h` (18 lines) are the only
files in scope using the singular `menu_` prefix — every other file uses
`menus_` (or `menus.cpp`/`.h` for the hub itself). Confirmed via grep that
today `menu_speak.h` has exactly one includer, `unified_action_menu.cpp`
(outside this scan's scope) — the doc's mention of `examine_view`/
`combat_queue` as historical consolidation sources appears to predate a
later refactor that removed their direct dependency, or they reference
`SpeakChoice` some other way not found by a text search for
`SpeakChoice`/`menu_speak`.

Proposal: rename `menu_speak.cpp`/`.h` → `menus_speak.cpp`/`.h` (or
`menus_speak_choice.*` if a closer-to-content name is preferred) and
update the one `#include` site. Small blast radius (one includer found),
but note the actual edit touches `unified_action_menu.cpp`, which is
outside this scan's `menus`/`engine_`/`strings` scope — flagging the
finding here since it lives in this batch of files, but the rename's
edit surface extends one file beyond it.

Risk: mechanical (pure rename + one include-path update). Confidence:
high on the inconsistency, medium on "exactly one includer" (a text
search may not catch every reference style, e.g. macro-generated
includes — worth a final grep immediately before executing).

### A13 — menus_skillflow_nav.cpp/.h, menus_chargen_layout.cpp/.h,
strings.cpp: reviewed tiny files, correctly scoped, no change

- `menus_skillflow_nav.cpp` (24 lines) / `.h` (28 lines): pure grid-nav
  primitives (`FirstFilledCol`/`NearestFilledCol`) shared by two otherwise
  independent consumers, `menus_chargen_feats.cpp` and
  `menus_powers_levelup.cpp`. Correctly scoped as its own tiny shared-
  helper TU rather than folded into either consumer (which would create a
  false ownership relationship) or into `menus_chargen_layout` (which
  serves a different consumer pair — `menus_chargen_attr`/
  `menus_chargen_skills` — and a different kind of shared logic, panel
  N-button/N-label layout math vs. 2D grid-cell scanning).
- `menus_chargen_layout.cpp` (59 lines) / `.h` (33 lines): same pattern,
  correctly scoped shared helper for `menus_chargen_attr`/
  `menus_chargen_skills`'s near-identical struct shape.
- `strings.cpp` (43 lines): the thin language-selection dispatcher tying
  together the six parallel `strings_xx.cpp` tables. Correctly separated
  from `strings.h` (declarations) and the per-language data files — this
  is the right shape for a hub this small.

Risk: N/A. Confidence: high.

### A14 — strfmt.h (45 lines): reviewed, correctly scoped, no change

Header-only `Format`/`VFormat` printf-to-`std::string` helper. Not
`menus`/`engine`/`strings`-prefixed because it isn't menu-, engine-, or
localisation-specific — it's a general formatting utility used broadly.
Its scope (one clearly-named pair of functions solving one documented
problem — silent truncation from fixed-buffer `%s`-heavy localised
strings) is exactly right for a standalone tiny header. No action.

### A15 — Header hygiene follow-ups paired with the A5-A7 `.cpp` splits
(secondary, optional)

If A5 (`engine_area.cpp`), A6 (`engine_reads.cpp`), A7
(`engine_player.cpp`), or A8 (`engine_panels.cpp`) are executed, their
headers could be split to match and would meaningfully cut real fan-out
given their include counts:

- `engine_player.h` — 63 includers; most likely want only the core
  position/camera/leader reads, not `GetServerPartyTable`/
  `TickPlayerInputRestore`.
- `engine_area.h` — 49 includers; object/handle consumers vs. map-pin/
  fog-of-war consumers vs. walkmesh consumers are probably disjoint sets.
- `engine_panels.h` — 39 includers; most want `PanelKind`/`IdentifyPanel`
  only, not the foreground-blocking/input-class primitives.
- `engine_reads.h` — 35 includers; generic control-text readers vs.
  item/Force-point domain readers are probably disjoint sets too.

Not proposing these as primary findings since the `.cpp` splits above
deliver the bulk of the maintainability win on their own and header
splits multiply the number of files touched; listing here so they aren't
lost if the team wants the fan-out reduction too. `engine_rebase.h` (36
includers) was checked and is NOT a candidate — it's already a minimal
4-declaration header; its high include count is inherent to the
address-rebasing design (every file with a hardcoded engine address needs
`R()`), not a hygiene problem.

### A16 — menus_internal.h documentation note (housekeeping, not a
functional finding)

`menus_internal.h`'s own comment says "most in menus.cpp" for where its
declared functions are defined. If A1's `menus_internal.cpp` extraction
happens, that comment becomes stale and should be updated to point at the
new file in the same change.

## Files read

All 96 in-scope `docs/llm-docs/code-index/*.md` summaries (`menus*.md`,
`menu_speak*.md`, `engine_*.md`, `strings*.md`, `strfmt.h.md`) plus
`docs/refactoring/file-inventory.txt`. Source files opened only for
grep-based boundary verification (function-signature line numbers): 
`menus.cpp`, `menus_chain.cpp`, `menus_listbox.cpp`, `engine_area.cpp`,
`engine_reads.cpp`, `engine_player.cpp`, `engine_panels.cpp`,
`engine_radial.cpp`, `menus_extract.cpp`, `engine_offsets.h` (line-count
check only), plus a repo-wide grep for `menu_speak`/`SpeakChoice`
includers.
