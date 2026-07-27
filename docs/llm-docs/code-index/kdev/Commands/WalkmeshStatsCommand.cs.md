# WalkmeshStatsCommand.cs (503 lines)

`kdev walkmesh-stats` — analyses extracted KOTOR `.wok` walkmeshes for edge-length and corridor-width distributions, to empirically tune accessibility wall-cue detection (Pillar 1: zone borders, raycast spacing, hysteresis) instead of guessing minimum passage widths. For each perimeter edge, finds the closest parallel (within ~5°), non-collinear, mutually-facing opposing edge (using each edge's "inward" direction toward its walkable-face third vertex) to measure corridor width, filtering sub-navigable artifacts below `--min-navigable` (default 0.5m, since KOTOR's character radius is ~0.13m). Parses BWM v1.0 directly. `--min-only` prints one line per area (min passage width); `--detail` adds a per-room breakdown. No dependencies on other kdev classes.

## Declarations (in source order)

- L28 — `static class WalkmeshStatsCommand`
- L30 — `const string DefaultSourceDir = @"build\wok-extract"`
- L36 — `WalkableTypes` — surfacemat.2da walkable row ids (Dirt, Grass, Stone, Wood, Water, Carpet, Metal, Puddles, Swamp, Mud, Leaves, Door, Trigger, + extended 22-30)
- L55 — `Command Build()` — `--source`, `--module`, `--detail`, `--min-only`, `--min-navigable` (default 0.5)
- L103 — `int Run(...)` — groups files by area prefix, analyses each room, aggregates, prints
- L191 — `void PrintArea(AreaSummary a, bool detail)`
- L225 — `void PrintGlobal(...)` — global edge/corridor percentiles + tightest-10-areas list
- L258-270 — `struct V3`, `record RoomStats(...)`, `class AreaSummary`
- L272 — `RoomStats ParseAndAnalyse(string path, float minNavigable)` — parses BWM header, filters walkable faces, builds perimeter edges (count==1 in the edge-incidence map) with each edge's "inward" direction (midpoint→third-vertex of its face), then for each edge searches all others for the nearest parallel + facing-toward + within-extent opposing edge to measure corridor width
  note: the "facing test" (`dotEf`/`dotFe` both positive) is what rejects false positives from obstacle-island perimeters and unrelated parallel walls elsewhere in the room
- L459 — `void BumpEdgeWithThird(...)` — edge-incidence counter also recording the third vertex, for inward-direction computation
- L468 — `float Length2D(V3 a, V3 b)`
- L474/479 — `ReadU32`/`ReadF32`
- L486-501 — stats helpers: `Min`, `Max`, `MinOrInf`, `Percentile` (linear-interpolated)
