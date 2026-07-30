# Phase 3 scan — engine area and navigation interface

Scope:
- engine_area.cpp (991 lines)
- engine_area.h (627 lines)
- engine_area_walls.cpp (581 lines)
- engine_area_map.cpp (387 lines)
- engine_navgraph.cpp (175 lines)
- engine_navgraph.h (49 lines)
- engine_compass.cpp (55 lines)
- engine_compass.h (32 lines)

Method: full read of all eight files, top to bottom (no truncation). Then
targeted greps against the whole `patches\Accessibility` tree (never just
this batch) to check liveness before calling anything dead or duplicated,
per the brief's traps section. Greps run and what they proved:

- `GetMapPinFlags` — 2 hits total in the whole tree (its own declaration in
  engine_area.h and its own definition in engine_area_map.cpp). Also checked
  `hooks.toml` / `exports.def` for the name — zero hits there too. No caller
  anywhere.
- `ScanRoomAllTriangleEdges|ScanRoomWallEdges` — confirmed both are
  single-TU-only (engine_area_walls.cpp), one caller each, both from
  `BuildAreaWallCache` in the same file.
- Liveness pass over every other function declared in engine_area.h
  (`GetRoomAtIndexed`, `GetAreaTag`, `IsMapNoteEnabled`, `TrapDetectedByAnyOf`,
  `IsDoorOpen`, `GetObjectLocalBoolean`, `GetAreaMap`, `GetFogCellSizeM`,
  `GetMapRotateCCWFromWorldOrientation`, `CreateMapPin`, `GetMapPinNoteText`,
  `GetWaypointMapNote`, `EnableMapNote`, `GetRoomRepresentativeWorld`,
  `ResolveClientObject`, and the rest) — all have live callers elsewhere in
  the patch (transitions.cpp, room_topology.cpp, cycle_state.cpp,
  map_ui_cursor.cpp, map_user_markers.cpp, discovery.cpp, view_mode.cpp,
  engine_picker.cpp, announce_degrees.cpp, floor_puzzle.cpp,
  passive_narrate.cpp, spatial_change_detector.cpp, trap_watch.cpp,
  cycle_input.cpp). `GetMapPinFlags` above is the sole exception.
- Per-include liveness grep for the four .cpp files in this batch (symbol
  greps per header, e.g. `GetPlayerArea|kAddrAppManagerPtr` for
  engine_player.h, `ReadCExoString|ExtractTextOrStrRef|ReadU32|LookupTlk`
  for engine_reads.h, `acc::strings::`/`strings::Id` for strings.h,
  `map_note_renames::` for map_note_renames.h, `std::` for `<cmath>`,
  `memcpy|strlen|strcpy|strcmp|memset` for `<cstring>`) — found four
  provably-unused includes, listed in A2-A4 below.
- Grepped `kInvalidObjectId` and `0x7F000000` across the tree to check
  whether engine_area.cpp's hand-written sentinel value already has a
  named constant elsewhere — it does (`engine_offsets_values.h:49`).
- Grepped `BuildAreaWallCache(` / `SnapshotNavGraph(` to find every caller,
  specifically to check for a repeat of the just-fixed unbounded
  per-frame nav-graph retry bug. Both functions' callers
  (guidance_pathfind.cpp, room_topology.cpp, spatial_wall_surfaces.cpp)
  are all outside this batch, so any caching/backoff logic around them is
  out of scope here — nothing to report inside this batch's own files.
  Checked separately for the same shape *inside* engine_navgraph.cpp /
  engine_area_walls.cpp themselves (a retry loop with no backoff/latch) —
  none found; both files build/return a snapshot once per call with no
  internal looped retry.

## Section A — general low-level cleanup

### A1 — Stale header comment on CSWSArea.rooms layout (engine_area.h:7)

What's there now: the file-top doc block says
`+0x230 rooms CSWSRoom[] inline, stride 0x4c`, i.e. an inline array
embedded directly at +0x230.

Why it's a problem: every actual consumer treats +0x230 as a POINTER to
the stride-0x4c array, not an inline array — `engine_area.cpp:204`
(`void* rooms = *reinterpret_cast<void**>(base + kAreaRoomsOffset);`) and
`engine_area_walls.cpp:279` do the same dereference. The file even has an
inline comment at `engine_area.cpp:200-203` that already flags this exact
header line as wrong: *"kAreaRoomsOffset holds a POINTER to the
inline-stride rooms buffer, not the rooms themselves. (Header comment was
misleading...)"* — that fix note was never carried back to the header it
names. The correct version is already sitting 400 lines further down, at
`engine_area.h:430`: `kAreaRoomsOffset = 0x230; // CSWSRoom* (deref first)`.

Proposed change: reword `engine_area.h:7` to
`+0x230 rooms CSWSRoom* (deref first), stride 0x4c` to match line 430 and
the actual read pattern.

Risk: mechanical (comment-only).
Estimated line delta: 1 line changed.

### A2 — Unused include in engine_area.cpp (line 11)

What's there now: `#include "map_note_renames.h"  // curated map-note
label overrides`.

Why it's a problem: zero uses of `acc::map_note_renames::` anywhere in
engine_area.cpp (grep confirms the only hit in the file is the #include
line itself). The map-note consumers that need this header
(`GetWaypointMapNote`, `GetMapPinNoteText`) live in
engine_area_map.cpp — where the include is correctly present and used —
after the Phase-1 candidate-3 three-way split. This is a leftover from
that split that was never dropped from the file it moved out of.

Proposed change: delete the include line.
Risk: mechanical.
Estimated line delta: -1.

### A3 — Unused include in engine_area_map.cpp (line 24)

What's there now: `#include "strings.h"`.

Why it's a problem: zero uses of `acc::strings::` in engine_area_map.cpp.
The one consumer that needs strings.h (`BuildDoorSuffix`'s door-state
suffix, via `acc::strings::Get(acc::strings::Id::DoorLocked)` etc.) stayed
in engine_area.cpp after the same split noted in A2. Mirror leftover of
A2, opposite direction.

Proposed change: delete the include line.
Risk: mechanical.
Estimated line delta: -1.

### A4 — Four unused includes in engine_area_walls.cpp (lines 16, 18, 20, 21)

What's there now: `<cmath>`, `<cstring>`, `"engine_player.h"`,
`"engine_reads.h"`.

Why it's a problem: grepped each header's symbols against the file body:
- `engine_player.h` (`GetPlayerArea`, `kAddrAppManagerPtr`, etc.) — 0 hits.
- `engine_reads.h` (`ReadCExoString`, `ExtractTextOrStrRef`, `ReadU32`,
  `LookupTlk`, and everything else it declares) — 0 hits.
- `<cmath>` — 0 uses of `std::` anywhere in the file (the only match for
  "sqrt" is inside a prose comment, not a call).
- `<cstring>` — 0 uses of memcpy/strlen/strcpy/strcmp/memset.

`<windows.h>` and `<cstdint>` remain genuinely needed (SEH's
`EXCEPTION_EXECUTE_HANDLER` macro, and `uint32_t`/`uint8_t` used
throughout).

Proposed change: delete the four unused include lines.
Risk: mechanical.
Estimated line delta: -4.

### A5 — Magic number where a named constant already exists (engine_area.cpp:83)

What's there now:
```cpp
bool IsSentinelHandle(uint32_t handle) {
    return handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u;
}
```

Why it's a problem: `0x7F000000u` already has a named constant,
`acc::engine::kInvalidObjectId` (`engine_offsets_values.h:49`), reachable
from this TU through the existing `engine_area.h` → `engine_offsets.h`
include chain — no new include needed. The comment directly above this
function (`engine_area.cpp:80-82`) even names it: *"0x7F000000
(kInvalidObjectId — the 'no object' marker...)"*, and `engine_area.h:69`'s
doc comment on `ResolveServerObjectHandle` does the same. The literal in
the code was simply never swapped for the constant it's already
documented against.

Proposed change: `handle == kInvalidObjectId` in place of
`handle == 0x7F000000u`.
Risk: mechanical (identical value; compiler-checked).
Estimated line delta: 0 (one-token substitution).

### A6 — ScanRoomAllTriangleEdges has external linkage it doesn't need (engine_area_walls.cpp:214)

What's there now: `ScanRoomAllTriangleEdges` is defined at namespace
scope (`acc::engine`) with plain external linkage — not `static`, not
inside an anonymous namespace.

Why it's a problem: it has exactly one caller, `BuildAreaWallCache`, in
the same TU (grep confirms 2 hits total in the tree: the definition and
that one call site). Its sibling `ScanRoomWallEdges` — same file, same
role (mirror of it, per its own doc comment), same "used only from
BuildAreaWallCache" shape — is correctly placed inside the file's
anonymous namespace (`engine_area_walls.cpp:27-189`). Leaving
`ScanRoomAllTriangleEdges` with external linkage is an unintended API leak
and an inconsistency against its own neighbour in the same file.

Proposed change: move `ScanRoomAllTriangleEdges` into an anonymous
namespace (or mark it `static`), matching `ScanRoomWallEdges`.
Risk: mechanical (linkage-only change).
Estimated line delta: 0.

### A7 — TryResolveDisplayNameOnce breaks the file's own anonymous-namespace convention (engine_area.cpp:442)

What's there now:
```cpp
static bool TryResolveDisplayNameOnce(void* clientApp, uint32_t handle,
                                      char* outBuf, size_t bufSize) {
```
declared with C-style `static` directly in `namespace acc::engine`, not
inside an anonymous namespace.

Why it's a problem: every other file-local helper in this exact file uses
an anonymous namespace instead — `GetServerObjectArray`, `IsSentinelHandle`,
`GetClientExoApp`, `TryReadLocString`, `TryReadTag`, `AppendCommaSeparated`,
`BuildDoorSuffix` are all wrapped in `namespace { ... }` blocks.
`TryResolveDisplayNameOnce` is the sole exception, and it is just as
file-local as the rest (its only caller is
`GetObjectDisplayNameByHandle`, later in the same file).

Proposed change: fold it into the neighbouring anonymous namespace (the
one that currently ends at line 437), dropping the explicit `static`.
Risk: mechanical.
Estimated line delta: 0.

### A8 — MaybeDrivePassiveSelection recomputes rebased addresses every call (engine_area.cpp:801-862)

What's there now: `kAddrIsGlobalFading` and `kAddrDoPassiveSelection` are
declared as local (non-static) `const uintptr_t` initialised via
`acc::addr::R(...)` inside the function body.

Why it's a problem: `acc::addr::R()` does a binary search over the
address-rebase table on every call (confirmed by reading
`engine_rebase.cpp:121-144` — two binary searches, .text table then
.rdata table, when running on the Allard build). This function's own doc
comment says it is meant to be "call[ed] once per frame from OnUpdate" —
so those two binary searches currently run every frame for no reason.
Every other address constant touched by this batch is a
file-scope `const uintptr_t kAddrXxx = acc::addr::R(...)`, computed once
at static-init time (see the whole back half of engine_area.h). This
function is the only place in the batch computing a rebased address
inside a hot per-frame body.

Proposed change: hoist `kAddrIsGlobalFading` / `kAddrDoPassiveSelection`
(and, for consistency, the four `constexpr size_t` offsets alongside them)
to file scope, or mark them `static` so C++11 magic-statics compute them
once.
Risk: low — touches a hot per-frame path whose only externally-visible
effect is keeping the Q/E candidate halo and passive narration alive
during the Endar-Spire held-fade window. In-game check: reach that
Endar-Spire opening stretch (or grep `patch-*.log` for the existing
`"FadeUnstick"` line) and confirm the halo/narration still stays live
across the fade with no behavior change, just fewer address lookups.
Estimated line delta: ~0 net (restructure only).

### A9 — BuildAreaWallCache does three distinct phases in one 230-line function (engine_area_walls.cpp:272-503)

What's there now: `BuildAreaWallCache` inlines three logically separate
passes: (1) the raw per-room edge scan (already delegated to the named
helper `ScanRoomWallEdges`), (2) the cross-room portal-coincidence filter
(~80 lines, building the global triangle-edge index and matching against
it), and (3) the same-room duplicate dedup (~45 lines, an O(N²) pairwise
pass with its own matching rules). Phases 2 and 3 are each already
self-contained (local lambdas, no cross-phase state beyond `outBuf` /
`kept` / `written`) but are not factored into named functions the way
phase 1 is.

Why it's a problem: this is exactly the oversized-function shape the
brief calls in-scope for Phase 3 (comparable to `ClassifyCluster` /
`BuildForArea` in room_topology.cpp). The function is well-commented, but
finding "where phase 2 ends and phase 3 begins" currently means reading
prose rather than a function name.

Proposed change: extract the portal-filter block into a named helper
(e.g. `FilterPortalEdges(WallEdge* outBuf, int written, int roomCount,
void* rooms, ...)` returning the kept count) and the same-room-dedup block
into another (e.g. `DedupSameRoomEdges(WallEdge* outBuf, int kept)`),
mirroring the existing `ScanRoomWallEdges` / `ScanRoomAllTriangleEdges`
naming pattern in the same file.
Risk: low — mechanical extraction of blocks that are already
self-contained, but this is the geometry hot path room_topology.cpp
depends on for wall detection. In-game check: enter a multi-room area
(the AreaWalls log lines already report the before/after edge counts —
`"portal filter: emitted=... -> kept=..."` and
`"same-room dedup: dropped..."`) and confirm the counts are unchanged and
room-shape/door narration still reads clean (no phantom-wall regressions).
Estimated line delta: ~0 net (organizational; +10-20 for new function
signatures/braces).

### A10 — GetMapPinFlags has no caller anywhere (engine_area.cpp / engine_area.h:309)

What's there now: `GetMapPinFlags` is declared in engine_area.h:309 and
defined in engine_area_map.cpp:282. Grep across the whole patch tree,
plus `hooks.toml` and `exports.def`, finds exactly two hits: its own
declaration and its own definition.

Why it's listed here rather than proposed as dead-code removal: the
header comment right above the declaration already explains this is
deliberate — the field it reads (`reference_number`) "CANNOT discriminate
the mod's saved markers from engine note pins" for its original intended
purpose, and it's "kept as a raw field accessor; no current caller." That
is a documented, considered decision, not an oversight — so per the
brief's evidence standard this is a judgment call for the user, not a
mechanical removal.

Proposed change: none from this scan; flagging for the user to decide
whether to actually delete it now or keep it as a documented raw
accessor.
Risk: n/a (question, not a proposed edit).
Estimated line delta: n/a.

## Section B — AI-pattern findings

### B1 — Duplicated GetGameObject miss/out resolve block (engine_area.cpp)

What's there now: the exact same 8-line shape — call `resolve(objectArray,
id, &out)` under `__try`, interpret the engine's inverted
miss-on-true/hit-on-false return, and only return `out` when `!miss &&
out` — appears twice:
- `ResolveServerObjectHandle`, lines 100-106.
- `AreaObjectIterator::Next()`, lines 979-986.

Both sites even carry the same explanatory comment about the inverted
bool convention (once in full at line ~94-96, once summarized at
~974-978).

Why it's a problem: this is the copy-paste-needing-an-abstraction shape —
one primitive ("resolve a handle through a CGameObjectArray*, with the
engine's inverted-bool convention") implemented twice with the same SEH
guard shape.

Proposed change: extract a small anonymous-namespace helper, e.g.
`void* ResolveViaObjectArray(void* objectArray, uint32_t id)` that does
the `__try`/miss/out dance once, and call it from both sites.
Risk: low — behavior-preserving, but touches both the single-object
resolve path and the area-iteration path. In-game check: cycle through
objects in a room (exercises `AreaObjectIterator`) and target/examine a
specific object by handle (exercises `ResolveServerObjectHandle`);
confirm both still work identically.
Estimated line delta: ~-10.

### B2 — Three copies of "read the tag field" (engine_area.cpp)

What's there now:
- `GetObjectTag` (lines 279-287, public API) — validate args, `__try`,
  `ReadCExoString(gameObject, kObjectTagOffset, ...)`.
- `TryReadTag` (lines 351-357, anonymous-namespace) — the same `__try` +
  `ReadCExoString(obj, kObjectTagOffset, ...)` body, used only as
  `GetObjectName`'s final fallback (line 610).
- `GetAreaDisplayName`'s fallback (lines 272-276) re-types `GetAreaTag`'s
  entire body (`__try` + `ReadCExoString(area, kAreaTagOffset, ...)`)
  inline instead of calling the `GetAreaTag` function that already exists
  four lines below it (line 289).

Why it's a problem: `TryReadTag` and `GetObjectTag` are the identical
primitive with the identical offset constant, one with argument
validation the other's caller has already done. `GetAreaDisplayName`
duplicates `GetAreaTag` outright, in the same file, a few lines above the
duplicate.

Proposed change:
- Delete `TryReadTag`; change `GetObjectName`'s fallback (line 610) to
  call `GetObjectTag(gameObject, outBuf, bufSize)` directly.
- Change `GetAreaDisplayName`'s fallback branch to
  `return GetAreaTag(area, outBuf, bufSize);` instead of re-running the
  `__try`/`ReadCExoString` pair.
Risk: mechanical/low — the callers already pass exactly the arguments the
callees re-validate, so the extra null/size checks are a no-op, not a
behavior change.
Estimated line delta: ~-10.

## Findings (possible bugs — user decides)

None found with solid evidence in this batch. One item that looked
suspicious on first read but checked out: `discovery.cpp:242` calls
`GetAreaTag` as a per-area persistence key, and `GetAreaTag`'s own doc
comment warns it's "almost always the GFF default 'untitled'... NOT a
usable per-area key." Read the call site
(`discovery.cpp:232-259`) — `GetAreaTag` is only the documented
last-resort fallback *after* `GetCurrentAreaResName` fails, exactly
matching the header's own guidance. Not a bug; noted here only so it
doesn't get re-flagged by a later pass without the context.

## Candidate 28 — narrow-header include opportunities

- `engine_area.h` includes the full `engine_offsets.h` aggregator but
  only uses one thing from it directly: the `Vector` type (every other
  identifier the header needs — `CExoString`, `kAddrCClientExoAppGetObjectName`,
  etc. — is either only mentioned in comments or is declared inside
  engine_area.h's own back half). `Vector` lives in
  `engine_offsets_types.h`. Narrowing the header's include would need the
  three .cpp files in this batch to add their own explicit includes for
  whatever address/field constants they pull in transitively today
  (confirmed at least `kAddrCClientExoAppGetObjectName`, from
  `engine_offsets_addresses.h`, used directly in engine_area.cpp) — a
  small, mechanical follow-up once someone does this narrowing pass.
- `engine_navgraph.h` includes the full `engine_offsets.h` aggregator for
  the same single reason — `PathPointSnapshot::pos` is a `Vector`. Same
  narrowing target: `engine_offsets_types.h`.
- `engine_area.cpp`, `engine_area_walls.cpp`, `engine_area_map.cpp`
  correctly include the full `engine_area.h` (they need most of its
  declared API — this is exactly the three-way split candidate 3 already
  produced, one shared header by design) and `engine_player.h` /
  `engine_reads.h`. Both of those are themselves undivided aggregators
  per the brief's own list, so there is no narrower header to migrate to
  yet for the player-chain or GUI-read helpers these files use — nothing
  actionable here until one of those gets split.
- `engine_compass.h` / `engine_compass.cpp` already include only
  `strings.h` (not an aggregator) and `<cmath>` — nothing to narrow.

## Files scanned with nothing to report

- engine_navgraph.cpp — clean. Bounded reads (`kMaxNodes`/`kMaxEdges`
  caps), truncate-not-crash fault handling, no retry loops, no dead code,
  no unused includes.
- engine_navgraph.h — clean (see candidate-28 note above for its one
  actionable item).
- engine_compass.cpp — clean. Pure math, no state, no findings.
- engine_compass.h — clean.
