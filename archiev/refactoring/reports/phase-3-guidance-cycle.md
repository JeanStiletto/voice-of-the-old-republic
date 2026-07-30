# Phase 3 scan — guidance/navigation assistance + object cycling

Scope: guidance_approach.cpp (337) / .h (90), guidance_autowalk.cpp (344) /
.h (67), guidance_beacon.cpp (266) / .h (48), guidance_description.cpp (194)
/ .h (31), guidance_pathfind.cpp (376) / .h (65), cycle_input.cpp (1047) /
.h (29), cycle_state.cpp (402) / .h (95). 3391 lines total.

Method: full `Read` of every file in the batch (headers first to learn the
public surface, then each .cpp). Then targeted `grep`/`Bash` passes per file
for: symbol-qualified include usage (never just the header name), fixed-
width-int / cstring standard-header usage, hex-literal magic numbers,
`strings::Get()` null-checks, raw `prism::Speak("...")` literals, and
cross-file usage of file-local constants (e.g. `kMaxNodes`/`kAreaPath*`
offsets defined in `guidance_pathfind.h` but consumed by `engine_navgraph.cpp`
and `probe_pathfind.cpp`, confirmed intentional and documented in place).

## Section A — general low-level cleanup

### A1 — dead include `audio_cue_player.h` (guidance_beacon.cpp:8)

What's there: `#include "audio_cue_player.h"   // PlayCueAtPosition (3D) for
heartbeat`, but the file never calls `PlayCueAtPosition`. `EmitHeartbeat`
calls `acc::audio::PlayCue3D` and `EmitArrivalCue` calls `acc::audio::PlayCue`
directly — both declared in `audio_bus.h`, already included. The file's own
comment at line 101 explains why: "Bypasses audio_cue_player because its 80m
range gate would otherwise apply" — the header was deliberately routed
around, and the include was never removed.

Verified by: `grep -n "PlayCueAtPosition\|audio_cue_player\|acc::audio::"
guidance_beacon.cpp` — zero references to anything `audio_cue_player.h`
declares.

Proposed change: remove the include.
Risk: mechanical.
Estimated delta: -1 line.

### A2 — unused file-local constant `kMaxEdges` (guidance_pathfind.cpp:21)

`constexpr int kMaxEdges = acc::engine::navgraph::kMaxEdges;` is declared
alongside `kMaxNodes` but never referenced again in the file — only
`kMaxNodes` sizes the A* state arrays (`s_state[kMaxNodes]`,
`chain[kMaxNodes]`).

Verified by: `grep -n "kMaxEdges" guidance_pathfind.cpp` — one hit, the
declaration itself.

Proposed change: remove the line.
Risk: mechanical.
Estimated delta: -1 line.

### A3 — dead standard-library includes (four files)

None of these headers' symbols appear anywhere in the corresponding file
(checked via targeted regex for every `<cstring>` function — `strcpy`,
`strcmp`, `strlen`, `strchr`, `strtok`, `memcpy`, `memset`, `memmove`,
`memcmp` — and every `<cstdint>` fixed-width type — `int8_t`..`uint64_t`,
`intptr_t`/`uintptr_t`):

- `guidance_approach.cpp:5` `<cstdint>` — unused.
- `guidance_approach.cpp:7` `<cstring>` — unused.
- `guidance_description.cpp:5` `<cstring>` — unused.
- `guidance_pathfind.cpp:5` `<cstdint>` — unused.
- `guidance_pathfind.cpp:6` `<cstring>` — unused.

Proposed change: remove each line.
Risk: mechanical (the compiler doesn't require these; they're picked up
transitively through `<windows.h>` / `engine_offsets.h` on this toolchain
today, but the file itself doesn't need them).
Estimated delta: -5 lines total.

### A4 — stale comment: node cap (guidance_pathfind.h:19)

The header's design comment says:
```
//   - ~200 nodes max — open/closed sets are fixed-size arrays.
```
But the actual cap it's describing, `acc::engine::navgraph::kMaxNodes`
(`engine_navgraph.h:36`), is `512`, not `~200`. `guidance_pathfind.cpp:20`
aliases it (`constexpr int kMaxNodes = acc::engine::navgraph::kMaxNodes;`),
so the array sizing itself is correct and reads the live constant — only the
prose comment is stale.

Verified by: `grep -n "kMaxNodes\s*=" engine_navgraph.h` → `512`.

Proposed change: update the comment to say 512 (or "see
`engine_navgraph::kMaxNodes`" so it can't drift again).
Risk: mechanical (comment-only).
Estimated delta: 0 net lines.

### A5 — two anonymous namespaces in one file (cycle_state.cpp:16, :337)

The file opens one anonymous namespace at the top (16-57, holding
`SortByDistanceAscending` / `HorizontalDistance`) and closes it, then later
opens a second, separate anonymous namespace (337-362) that holds only
`CycleCategoryDirectional`, immediately before its two callers
(`CycleNextCategory` / `CyclePrevCategory`). Both compile to the same
internal linkage; this isn't a bug, just an inconsistency against the
one-anon-namespace-per-file pattern the rest of this batch (and the
project's stated convention: "anonymous namespaces hold file-local state")
follows.

Proposed change: fold `CycleCategoryDirectional` into the top anonymous
namespace, or leave as-is — purely a style call, low value either way.
Risk: mechanical.
Estimated delta: 0 net lines (namespace braces move, nothing else changes).

## Section B — AI-pattern findings

### B1 — duplicated "play focus cue" block, 4 call sites (cycle_input.cpp)

The same four lines appear verbatim in four functions:

```cpp
auto bindings = BindingsFor(a.category);
{
    acc::audio::NavCue cue = RefineDoorCue(bindings.cue, a.obj);
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(cue), a.pos);
}
```

Call sites: `OnAnnounceFocus` (568-572), `OnPathfindFocus` (675-679),
`OnBeaconFocus` (796-800), `OnPathfindFocusForce` (866-870). Checked whether
`bindings` is reused later in any of the four functions (it would block a
naive extraction) — it is not; in every case `bindings` is read only for
`.cue` inside this block and never touched again in that function (verified
with a per-function `awk`/`grep` pass).

Proposed change: extract a single helper, e.g.
`void PlayFocusCue(const NarratedActivation& a)` (or take
`acc::filter::CycleCategory` + obj + pos directly), and call it from all
four sites.
Risk: low (pure function of already-resolved `NarratedActivation`, no
shared anon-namespace state to untangle — the trap that burned candidates
13/24 doesn't apply here).
Estimated delta: -12 lines (4×4 → 1 helper of ~5 + 4 one-line calls).

### B2 — `AnnounceCurrent` is doing ~7 separable jobs (cycle_input.cpp:242-419)

178 lines covering: empty-state early-out, 3D cue playback, a four-way name-
resolution cascade (shipped hint / user pin / map-note waypoint / generic
object, lines 278-327), same-name disambiguation (329-357), clock+distance
computation (359-368), message formatting + speak (370-381), narrated-target
stamping (383-397), and map-cursor panning (399-413). This is the largest
single function in the batch and the natural target for the function-level
decomposition the brief calls in-scope for Phase 3 (comparable to
`ClassifyCluster`/`BuildForArea` in the already-logged Phase-1 deferral).

Proposed split (illustrative, not prescriptive): pull the name-resolution +
disambiguation cascade (278-357) into a `ResolveFocusedName(listing, ctx,
char* out, size_t outSize)` static, and the stamp + cursor-pan tail
(383-413) into a `StampAndPanFocus(...)` static. That alone would cut the
function to roughly half its current size while keeping the top-level
control flow (empty check → cue → name → speak) readable in one screen.
Risk: low — all the state involved is either the `CategoryListing` /
`CycleState` passed in or read through already-published accessors; no
anon-namespace variable/constant sharing to worry about (checked: nothing
outside `AnnounceCurrent` touches the locals it computes).
Estimated delta: roughly neutral line count (extraction, not deletion);
readability win, not a size win.

### B3 — `ComputePath` combines four phases in one function (guidance_pathfind.cpp:100-374)

274 lines: nav-graph snapshot + degenerate-case checks (100-152), A* search
(154-221), path reconstruction (223-250), and the string-pulling smoothing
pass (252-371). Each phase is already delimited by its own comment block in
the source, which is exactly the shape the brief describes as a
decomposition candidate.

Proposed split: `SolveAStar(graph, startNode, goalNode, chain[], len)` and
`SmoothPath(start, walls, wallCount, waypoints)` as file-local statics,
leaving `ComputePath` as the orchestrator (snapshot → degenerate checks →
solve → reconstruct → smooth). The `s_state[kMaxNodes]` static array and the
`kMaxNodes`/`kMaxEdges` aliases would need to move with `SolveAStar`.
Risk: low — this is a single self-contained TU with no external readers of
the intermediate state (the `AStarNode` struct and `s_state` array are both
already function-local/static and touched by nothing outside
`ComputePath`).
Estimated delta: roughly neutral; readability win.

### B4 — belt-and-braces re-check of `GetPlayerPosition` (cycle_input.cpp:779-789)

```cpp
Vector playerPos;
if (!acc::engine::GetPlayerPosition(playerPos)) {
    // Should be impossible — caller gated on GetPlayerPosition — but
    // defend so the failure path speaks rather than going silent.
    ...
}
```
Both callers of `OnBeaconFocus` (`TryHandleEvent` and `PollWin32`) already
gate on `GetPlayerPosition` before dispatching to any handler, so this is a
guard duplicating a check one frame up the call stack — exactly the pattern
Section B asks to be surfaced. It's already self-documented as a deliberate
choice by the original author ("but defend so the failure path speaks
rather than going silent"), so this is reported for the record, not as a
recommended deletion — removing it would mean a currently-impossible state
silently does nothing instead of speaking a fallback.
Risk: low either way (the check is 8 lines and costs nothing at runtime).
Estimated delta: -8 lines if removed; 0 if kept.

### B5 — redundant post-condition check (cycle_input.cpp:805)

```cpp
bool pathOk = acc::guidance::ComputePath(area, playerPos, a.pos, waypoints);
if (!pathOk || waypoints.empty()) {
```
`guidance_pathfind.h`'s own documented contract for `ComputePath` (see its
header comment, "Returns: true — non-empty path... false — ...
outWaypoints is cleared on every failure") guarantees `waypoints` is
non-empty whenever `pathOk` is true, and cleared whenever it's false. The
`|| waypoints.empty()` half of the condition can therefore never be true
without `!pathOk` also being true — a redundant re-validation of a
guarantee the callee already documents.
Risk: low — removing the redundant half is behavior-preserving given the
documented contract; flagging rather than proposing because it does add a
small amount of defense against the contract ever being violated by a
future edit to `ComputePath`.
Estimated delta: -1 clause (cosmetic).

## Findings (possible bugs — user decides)

None with enough confidence to report. One thing looked at closely and
ruled out: `ArmApproach` treats a supplied `targetPos` of exactly
`(0,0,0)` as "not supplied" (`guidance_approach.cpp:139-140`). This reads
like a sentinel-value hazard at first glance, but it's consistent with
`ApproachArm`'s own documented default (`guidance_approach.h:48`,
`targetPos = {0.0f, 0.0f, 0.0f}`) and the function immediately falls back to
snapshotting a live position from `targetObj` when available — i.e. the
zero-vector-as-"unset" convention is the arm struct's own documented
contract, not an accidental collision with a legitimate world position. Not
raised as a bug.

## Candidate 28 — narrow-header include opportunities

Six headers in this batch include the full `engine_offsets.h` aggregator
but use only `Vector` from it, which lives in `engine_offsets_types.h`
(confirmed: `grep -n "struct Vector" engine_offsets_types.h` is the only
hit across the four split headers):

- `guidance_approach.h:34`
- `guidance_beacon.h:21`
- `guidance_description.h:19`
- `guidance_pathfind.h:24` (its own bottom-of-file offset constants are
  plain `size_t`, not sourced from `engine_offsets.h`)
- `cycle_state.h:12`
- `guidance_autowalk.h:10` — **with a caveat**: `guidance_autowalk.cpp`
  also uses `kInvalidObjectId` (5 call sites), which lives in
  `engine_offsets_values.h`, not `_types.h`. Narrowing this header to
  `_types.h` alone would break the .cpp's transitive include; the header
  would need to include both `_types.h` and `_values.h`, or the .cpp would
  need to pick up `_values.h` directly. Flagging this explicitly since it's
  the same shape of miss (a narrow function-name/header-name scan missing a
  shared constant) that already burned candidates 13 and 24.

None of the other five headers' consuming `.cpp` files were found to need
any other `engine_offsets_*` symbol beyond `Vector` (checked with a
scoped grep across the whole batch for stray `k`-prefixed identifiers).

**Discrepancy worth flagging for whoever owns candidate 28 broadly:** the
brief describes `engine_area.h` / `engine_panels.h` / `engine_player.h` /
`engine_reads.h` as "also aggregators now" alongside `engine_offsets.h`.
This batch includes three of those four directly (`engine_area.h` in
`cycle_input.cpp`/`cycle_state.cpp`/`guidance_pathfind.cpp`,
`engine_panels.h` in `cycle_input.cpp`, `engine_player.h` in
`cycle_input.cpp`/`cycle_state.cpp`/`guidance_autowalk.cpp`). Checked the
directory for sibling narrow headers the way `engine_offsets.h` has
(`engine_offsets_types.h` etc.) — there are none
(`engine_area_types.h`-style files don't exist; `ls engine_area* engine_panels*
engine_player* engine_reads*` shows only the single `.h`/`.cpp` pair each,
plus a couple of unrelated `_internal.h`/`_state.cpp`/`_walls.cpp`/`_map.cpp`
files that are not include-narrowing splits). Only `engine_offsets.h`
currently has the four-way split described in the brief. There is nothing
narrower to migrate this batch's `engine_area.h`/`engine_panels.h`/
`engine_player.h` includes to yet.

## Files scanned with nothing to report

- guidance_autowalk.cpp
- guidance_autowalk.h (aside from the candidate-28 note above)
- guidance_beacon.h (aside from the candidate-28 note above)
- guidance_description.h (aside from the candidate-28 note above)
- guidance_approach.h (aside from the candidate-28 note above)
- cycle_input.h
- cycle_state.h (aside from the candidate-28 note above)
