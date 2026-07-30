# Phase 3 scan — room-topology / wall-probing batch

Scope:
- `patches/Accessibility/room_topology.cpp` (3372 lines)
- `patches/Accessibility/room_topology.h` (116 lines)
- `patches/Accessibility/wall_probe.cpp` (139 lines)
- `patches/Accessibility/wall_probe.h` (44 lines)

Method: full sequential read of all four files (no section skipped),
followed by targeted greps to verify every finding below: a function-map
grep of `room_topology.cpp` to get exact line ranges for every top-level
function; per-counter-name greps across the whole file to confirm which
locals cross a proposed extraction boundary and which stay pass-local;
a `g_graph\.` grep restricted to the `BuildForArea` pass-1/1b/2 line range
(2436-2884) to prove those passes touch no shared mutable state beyond
plain parameters; a cross-file check of `_stricmp` usage in
`map_note_renames.cpp` to confirm `<windows.h>` isn't needed for it; and a
read of `strings.h`'s `Get(Id)` contract plus its `strings.cpp` and
`strings_en.cpp` implementations to confirm the never-null guarantee used
in finding B1. Read-only throughout — no source file touched.

Context carried in from `docs/refactoring/STATE.md` (not re-reported):
the file-level splits of this file are closed (Phase 1's candidate 12).
While reading `SnapshotDoors`/`MaybeRefreshDoors` I confirmed the C3
landmarkName-ownership item from the Phase-2 coupling scan is now fixed —
`SnapshotDoors` (room_topology.cpp:601-701) owns the save/restore itself
and `MaybeRefreshDoors`'s comment (room_topology.cpp:2310-2312) says so
explicitly; no position-matching workaround remains in the caller. C4
(the `Landmark::doorMatched` ordering contract between transitions.cpp
and this file) is still open but its concrete fix — a precondition
comment on `IterateLandmarks`/`MarkLandmarkClaimedByDoor` — lives in
`transitions.h`, outside this batch; `room_topology.h`'s own doc comment
on `AttachLandmarksToDoors` (lines 69-74) already states the "runs inside
BuildForArea; also invoked after a landmark-cache rebuild" contract from
this side, so nothing further is needed here.

## Section A — general low-level cleanup

### A1 — `ClassifyCluster` (room_topology.cpp:1378-1881, 504 lines): function-level decomposition

This is one of the two functions the brief names explicitly. Read in
full. Its structure is already self-documenting — a dispatch on
`areaHint`/`externalCount` into four independent rendering paths, each of
which touches only its own parameters and a handful of already-file-scope
free functions (`OctantBit`, `OctantFromVector`, `Degree`,
`WalkmeshAgreesDeadEnd`, `RenderDoorDirection`, `DirEntry`,
`AppendListEntry`, `ComputeCentroidAxis`, `AxisOctantMask`,
`RenderCorridorAxis`, `acc::strings::Get`). None of the four paths reads
or writes an anonymous-namespace variable — verified by inspection, not
just a function-name scan (the trap that burned candidates 13/24). That
makes this a clean function-level split, not a repeat of the rejected
file-level one: every new helper stays in the same anonymous namespace,
in the same .cpp, callable unqualified exactly the way `ClassifyCluster`
already calls `RenderDoorDirection` etc.

Proposed extraction (all as file-local `static` — or just left inside the
existing anonymous namespace, which is what every other helper in this
file already does):

- `RenderClusterAsArea` — replaces the `if (areaHint != 0) { … return; }`
  block, room_topology.cpp:1410-1503 (94 lines). Signature: `(const
  NavGraphSnapshot& g, const Vector& centroid, const int* externalNbs,
  const int* externalSrcs, const int* externalDoorIdx, int externalCount,
  const std::string& trigEntries, int areaHint, float centroidFloorZ,
  bool isLargeArea, std::string& outLabel, int& outKind, int& outSig)`.
  Needs nothing beyond its parameters.
- `RenderClusterAsDeadEnd` — replaces the `externalCount == 1` body,
  room_topology.cpp:1508-1600 (93 lines, minus the `nb` bounds check
  which stays as the caller's early-return guard). Signature: `(const
  NavGraphSnapshot& g, const Vector& centroid, int nb, int doorIdx, const
  std::string& trigEntries, int trigCount, std::string& outLabel, int&
  outKind, int& outSig, bool& outFiltered)`.
- `FinishCorridorLabel` — the shared render tail used by both corridor
  paths (see A4 below): `(int bitA, int bitB, int doorIdx, const
  std::string& trigEntries, std::string& outLabel, int& outKind, int&
  outSig)`. Replaces room_topology.cpp:1625-1637 (direct
  `externalCount == 2` path) and 1814-1821 (demoted `realExitCount == 2`
  path) — bitA/bitB/doorIdx computation, which differs between the two
  call sites, stays in the caller.
- `AggregateJunctionOctants` — replaces the octant-array init +
  aggregation loop, room_topology.cpp:1649-1742 (94 lines). Signature:
  `(const NavGraphSnapshot& g, const Vector& centroid, const int*
  externalNbs, const int* externalSrcs, const int* externalDoorIdx, int
  externalCount, bool outOctantHasExit[8], bool outOctantAllDeadEnd[8],
  int outOctantDoorIdx[8])`.
- `TryDemoteJunction` — replaces the demote-gate computation plus both
  demote branches, room_topology.cpp:1744-1828 (85 lines). Returns
  `bool` (true = rendered, caller returns immediately; false = fall
  through to the full junction render). Calls `FinishCorridorLabel`
  internally for its `realExitCount == 2` branch. Signature: `(const
  bool octantHasExit[8], const bool octantAllDeadEnd[8], const int
  octantDoorIdx[8], const Vector& centroid, const std::string&
  trigEntries, std::string& outLabel, int& outKind, int& outSig)`.
- `RenderJunctionLabel` — replaces the final junction-emit block,
  room_topology.cpp:1830-1880 (51 lines). Signature: `(const bool
  octantHasExit[8], const bool octantAllDeadEnd[8], const int
  octantDoorIdx[8], const std::string& trigEntries, std::string&
  outLabel, int& outKind, int& outSig)`.

After extraction `ClassifyCluster` itself is a ~45-line dispatcher: clear
outputs, build `trigEntries` (kept inline, it's 5 lines and feeds every
path), then one `if`/call/`return` per case. Every acclog::Write call
inside a moved block moves with it — no logging changes.

Risk: low. Pure code motion, no logic changes; every helper's inputs are
exactly what the branch already receives today (verified, not assumed).
This is nav-narration code the user hears on every dead end, corridor,
junction, room and door — **needs an in-game test before commit**:
walk through at least one of each label type (a dead end, a straight
corridor, a junction, a merged "Bereich", and a door-adjacent cluster)
and confirm the spoken/logged labels are byte-identical to a pre-change
run. The `WallTopo` per-node dump (`DumpGraphToLog`) is a convenient
diff target since it prints label/kind/sig per node.

Estimated line delta: roughly neutral in total (code moves, doesn't
shrink), but `ClassifyCluster` itself drops from 504 to ~45 lines.

### A2 — `BuildForArea` (room_topology.cpp:2344-3147, 804 lines): function-level decomposition

The second function the brief names explicitly. Its body is already
labelled by the author into five phases (`===== Pass 1: core merge
=====`, `Pass 1b: corner fold`, `Pass 2: straggler absorb` (a)+(b),
`Pass 3: per-cluster classification`) — the seams are the ones the
comments already draw, not a new cut.

Verified boundary (this is the check that burned candidates 13/24 — I
ran it, not just a function-name scan): a `g_graph\.` grep restricted to
lines 2436-2884 (Pass 1 through Pass 2b) returns **zero matches**. Those
four passes touch only local variables (`g`, `n`, `nodeOpen`, `nodeRoom`,
`nodeFloorZ`, `chainStraight`, `chainStraightCos`, the wall-cache triple
`haveGW`/`gw`/`gwCount`) plus the existing global `s_uf_parent` via
`UFFind`/`UFUnite` (already file-scope functions, callable from anywhere
in the file). Only Pass 3 (2885-3095) and the setup/tail portions read or
write `g_graph`. This means the extraction is pure parameter-passing,
not a repeat of the shared-mutable-state trap.

I also traced every per-pass counter to see which ones cross into
`BuildForArea`'s single consolidated summary log (room_topology.cpp:
3107-3115) versus which stay inside their own pass's local summary line
— this is the "shared constants/variables a function-call scan misses"
check. Result:
- Cross the boundary (must be out-params): `coreSpace`, `coreCorridor`
  (Pass 1), `absorbAdj` (Pass 2a), `bboxAbsorbed` (Pass 2b), `clusters`,
  `multiNodeClusters`, `deadEnds`, `corridors`, `junctions`, `openAreas`
  (Pass 3).
- Stay pass-local (no out-param needed): `coreVetoDoor`, `coreVetoWall`
  (Pass 1), `cornerFolds` (Pass 1b), `absorbAdjVetoDoor`,
  `absorbAdjVetoWall` (Pass 2a), `bboxVetoedByWall`, `bboxVetoedByDoor`,
  `bboxNoZMatch`, `bboxAmbiguous` (Pass 2b) — each is only read by its
  own pass's own `acclog::Write` line, verified by grep.

Proposed extraction, all file-local (anonymous-namespace) helpers:

- `ComputeChainStraightness(g, n, chainStraight[], chainStraightCos[])`
  — room_topology.cpp:2465-2491. Computed once because Pass 1, Pass 1b
  and Pass 2a all read the result (the comment at 2465-2469 already says
  so).
- `RunCoreMergePass(g, n, nodeOpen[], nodeRoom[], nodeFloorZ[],
  chainStraight[], haveGW, gw, gwCount, int& outCoreSpace, int&
  outCoreCorridor)` — room_topology.cpp:2463(UF reset)-2566, ~105 lines.
- `RunCornerFoldPass(g, n, chainStraight[], chainStraightCos[], haveGW,
  gw, gwCount)` — room_topology.cpp:2585-2623, ~40 lines. No out-param;
  `cornerFolds` stays local to the function's own log line.
- `RunStragglerAbsorbAdjacency(g, n, chainStraight[], nodeFloorZ[],
  haveGW, gw, gwCount, int& outAbsorbAdj)` — room_topology.cpp:2651-2728,
  ~78 lines.
- `RunStragglerAbsorbBbox(area, g, n, int& outBboxAbsorbed)` —
  room_topology.cpp:2735-2883, ~150 lines. Takes `area` (for
  `ClassifyEdge`'s cross-room diagnostic) but **not** the wall-cache
  triple — this pass calls `ClassifyEdge`, which fetches its own wall
  cache internally, so `haveGW`/`gw`/`gwCount` are not needed here
  (verified by reading the call site at room_topology.cpp:2841).
- `ClassifyAndLabelClusters(area, g, n, nodeOpen[], nodeRoom[],
  nodeFloorZ[], int& outClusters, int& outMultiNodeClusters, int&
  outDeadEnds, int& outCorridors, int& outJunctions, int& outOpenAreas)`
  — room_topology.cpp:2892-3095, ~205 lines (Pass 3). Writes
  `g_graph.node_label/kind/sig/filtered/extent` directly — `g_graph` is
  this file's existing anonymous-namespace global, so no pointer needs to
  be threaded through; the function accesses it exactly the way
  `SnapshotDoors`/`ClassifyEdge`/etc. already do.

After extraction, `BuildForArea` itself is roughly: the existing
guard/retry/reset/snapshot setup (2344-2434, unchanged, ~90 lines) + a
`ComputeNodeShapeFeatures` call (unchanged) + the wall-cache fetch
(unchanged, 5 lines) + six one-line pass calls + the freeze-cluster-ids
loop (unchanged, 3 lines) + the final summary logs and diagnostic-dump
calls (unchanged, ~45 lines) — roughly 170-190 lines total, down from
804, with every phase boundary matching a boundary the code's own
comments already declare.

Risk: low, same reasoning as A1 (pure parameter-passing, no anonymous
namespace state crosses undeclared). This is the entire nav-graph build
path, so it is higher-stakes than A1 — **needs an in-game test before
commit**: re-enter several previously-visited areas of different
topology (a corridor-heavy interior, an open plaza, a small room cluster,
an area with door-attached landmarks) and diff the `BuildForArea`
summary log line (cluster/kind counts) plus a couple of spoken labels
against a pre-change baseline. Recommend doing A1 and A2 as one build/
test cycle rather than two, since they touch the same call path.

Estimated line delta: roughly neutral (code moves), `BuildForArea`
itself drops from ~804 to ~180 lines.

### A3 — Unused `#include <windows.h>` (room_topology.cpp:21)

Nothing in the file uses a Win32 type, macro or API. The only
plausible reason for the include — `_stricmp` at room_topology.cpp:1130
— doesn't need it: `_stricmp` is an MSVC CRT function declared via
`<cstring>`/`<string.h>`, and `map_note_renames.cpp` in this same
codebase calls `_stricmp` with only `<cstdio>`/`<cstring>` included (no
`<windows.h>`), confirming the toolchain doesn't require it here. Grepped
the whole file for `min(`/`max(`/`NULL`/`TRUE`/`FALSE`/`BYTE`/`HANDLE`/
etc. — zero hits, so there's no hidden macro dependency either.

Proposed change: delete room_topology.cpp:21.
Risk: mechanical (compiler-checked — if something did need it, the next
build fails immediately and the fix is a one-line revert).
Estimated line delta: -1.

### A4 — Duplicated corridor-render tail (room_topology.cpp:1625-1637 and 1814-1821)

Two blocks compute the identical sequence — call `RenderCorridorAxis`,
append `trigEntries`, set `outKind = kKindCorridor`, and pack `outSig` —
once for the direct `externalCount == 2` path and again for the demoted
`realExitCount == 2` path inside the junction branch. The sig-packing is
written two different ways for the same result: the first site uses two
named locals (`sigBitLo`/`sigBitHi`); the second inlines the same
ternary twice (`sigBit` plus a repeated `(bitA < bitB) ? bitB : bitA`).
Only the `bitA`/`bitB`/`doorIdx` derivation differs between the two call
sites (raw neighbour octants vs. the post-aggregation `realExitBits`).

Proposed change: extract `FinishCorridorLabel(int bitA, int bitB, int
doorIdx, const std::string& trigEntries, std::string& outLabel, int&
outKind, int& outSig)` and call it from both sites (this is the same
helper named in A1 — listing it here too since it stands on its own even
if the larger A1 decomposition isn't taken).
Risk: mechanical (the two blocks are provably equivalent — same
operations, same outputs, only the sig-packing math is written two
different but equal ways).
Estimated line delta: -10 to -12.

### A5 — Stale "EXPERIMENTAL" framing in the file's opening comment (room_topology.cpp:1-17) — low confidence

The file opens with "EXPERIMENTAL — alternative-direction-calculation-
system branch" and frames the algorithm as "Path 3 (2026-05-13)". The
file is the shipped, single-source-of-truth nav-narration system per its
own header's opening line (room_topology.h:1-4: "Single source of truth
for 'spoken label for the region at world position P'. Consumed by
transitions (player), view_mode (cursor), map_ui_cursor..."), with two
months of subsequent tuning history, in-game bug citations, and shipped
version numbers throughout the rest of the file. This isn't a hard
contradiction — the "Path 3" naming and the historical framing are still
technically accurate as *how the algorithm arrived here* — but a new
reader hitting "EXPERIMENTAL" at the top of a 3372-line production file
that gates something spoken on every player footstep could reasonably
misjudge its maturity.
Risk: comment-only, zero behaviour change either way. Flagging as a
judgment call, not a firm finding — leave it out if the historical
framing is intentionally kept as project history.

## Section B — AI-pattern findings

### B1 — Redundant null-checks on `acc::strings::Get()` results (~30 sites)

`strings.h`:1986-1989 documents the contract explicitly: "Resolve a
string ID to its language-specific bytes. **Never returns nullptr** —
out-of-range / Count_ ids resolve to \"\" so callers can snprintf without
null-checking." Confirmed at both call layers: `strings.cpp`'s dispatcher
falls back to `return ""` if the language switch somehow falls through
(strings.cpp:24), and each per-language table (checked `strings_en.cpp`)
ends its switch with an explicit `case Id::Count_: return "";` plus a
trailing `return "";` outside the switch (strings_en.cpp:839-841) — two
independent belts-and-braces against ever returning `nullptr`.

Despite that guarantee, room_topology.cpp guards almost every `Get()`
result with both a null check and an empty-string check, e.g.:
`if (!dirWord || !dirWord[0]) return std::string();` (870, 1106, 1178,
1282, 1199), `if (fmt && fmt[0]) …` (879, 1135, 1139, 1142, 1181, 1205,
1286, 1488, 1492, 1496, 1592, 1794, 1869), and similar at 877, 1116,
1118, 1129, 1133, 1137, 1442, 1463, 1486, 1494, 1523, 1580, 1776, 1850,
1873, 3363 — roughly 30 sites in this file alone. The null half of every
one of these is provably dead: `Get()` cannot return `nullptr`, so only
the `X[0]` (empty-string) half ever does anything — it's the real guard,
covering an untranslated/missing table entry.

Proposed change: drop the `!X ||` / `X &&` null half at each site,
keeping only the `X[0]` check. Behaviour-preserving by the function's own
documented contract — the branch taken is identical either way, since
the null branch can never be entered.
Risk: mechanical (provably behaviour-preserving given the cited
contract).
Estimated line delta: cosmetic only (each site shrinks by a few
characters, no line removed).

Note: I only verified and counted this pattern in room_topology.cpp — it
is very likely present in most/all other files that call
`acc::strings::Get()`, since it's a consistent style choice across this
codebase, not something unique to this file. Doing it piecemeal per
Phase-3 batch means every batch will independently re-find the same
~30-site pattern in its own files; a single codebase-wide pass once all
batches report in would be more efficient than fixing it file-by-file.

## Findings (possible bugs — user decides)

None found. Read the merge-pass arithmetic, the door/edge classification,
and the union-find bookkeeping closely (these are the parts most likely
to hide an off-by-one or an inverted condition) and didn't find anything
that looked like a behavioural bug — the tie-break choices in
`FindDoorNearPoint`/`FindDoorOnEdge` (`<=` vs `<` against a running-best)
are stylistic, not incorrect: they still converge to the true nearest
candidate.

## Candidate 28 — narrow-header include opportunities

- `room_topology.h:21` includes the full `engine_offsets.h` aggregator
  but only uses `Vector` (declared in `engine_offsets_types.h`, confirmed
  by reading the aggregator's own header comment listing the four-way
  split). Since `room_topology.h` is included by `transitions.cpp`,
  `view_mode.cpp` and `map_ui_cursor.cpp` per its own opening comment,
  narrowing this one include would shrink the transitive header footprint
  for all of them, not just this file — worth doing together with those
  files' own Phase-3 passes rather than in isolation. Note:
  `room_topology.cpp` itself gets the full aggregator regardless, via
  `engine_area.h`/`engine_navgraph.h`/`engine_reads.h` (all of which
  already include the full `engine_offsets.h`), so narrowing
  `room_topology.h` doesn't reduce this .cpp's own dependency footprint —
  only its consumers'.
- `wall_probe.h` is already minimal: it forward-declares `struct Vector;`
  instead of including any offsets header at all. Nothing to improve.

## Files scanned with nothing to report

- `wall_probe.cpp` (139 lines) — read in full. Self-contained: reads the
  perimeter-wall cache directly, holds no state of its own, and stays
  entirely on the "wall probing" side of the wall-probe/room-topology
  split (single- and multi-ray casts only — no cluster, graph or label
  concept anywhere in it). No dead code, no oversized functions, no
  AI-pattern smells.
- `wall_probe.h` (44 lines) — read in full. Matches its own doc comment's
  description of the split exactly; the three published entry points
  (`ProbeDistance`, `IsAlcoveAlongAxis`, `ProbeClearance8`) are all
  called from `room_topology.cpp` (verified via the `using
  acc::wall_probe::…` declarations at room_topology.cpp:43-45) and
  nothing on the room-shape/cluster side has leaked into this header.
- `room_topology.h` (116 lines) — read in full. Declarations only, well
  documented, no issues beyond the candidate-28 note above.
