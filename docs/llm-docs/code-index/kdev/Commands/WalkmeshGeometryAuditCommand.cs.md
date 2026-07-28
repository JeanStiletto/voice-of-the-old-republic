# WalkmeshGeometryAuditCommand.cs (999 lines)

`kdev walkmesh-geometry-audit` — quantifies how axis-aligned KOTOR's world is and how much walkable area a 4-cardinal grid movement model would lose, to inform an accessibility navigation design decision. Per room/area/global: edge-angle histogram (length-weighted, folded to [0,90)), rectilinear fraction under world axes vs. best local rotation (1°-step scan), sparse grid rasterisation loss at 0.25/0.5/1.0m cell sizes (HashSet-keyed, bbox-independent to tolerate skybox-sentinel vertices), 4-cardinal-vs-8-way BFS reachability from the largest triangle's centroid, per-cell "smoothness" classification (smooth/fiddly/choke, via 8-direction run-length DP), skinny-triangle counts, and diagonal-locked cluster identification (8-connected components of cells reachable only via diagonal moves, with world-coord bounding boxes). Parses BWM v1.0 directly. Optionally writes a markdown report via `--report`, whose tail is a fixed "Design decisions" appendix (`AppendDesignDecisions`) documenting the 4-cardinal-viable / 0.5m-default-with-0.25m-pinch-exceptions conclusion — regenerated each run but the decision text lives in this source file, not the generated doc. No dependencies on other kdev classes.

## Declarations (in source order)

- L37 — `static class WalkmeshGeometryAuditCommand`
- L39 — `const string DefaultSourceDir = @"build\wok-extract"`
- L44 — `WalkableTypes` — same surfacemat set as `WalkmeshStatsCommand` (kept duplicated intentionally; hoist only if a 3rd consumer appears)
- L50 — `Command Build()` — `--source`, `--report`, `--module`, `--cell` (default 0.5m)
- L87 — `int Run(...)` — groups `.wok` by area prefix, analyses each room, aggregates per-area and global, prints + optionally writes report
- L173 — `void PrintGlobal(...)` — linear console summary: triangle/area/perimeter counts, rectilinear %, angle histogram, raster loss, BFS reachability, smoothness, skinny triangles, worst-10 lists
- L247 — `void WriteReport(...)` — markdown report mirroring the console output, per-area breakdown with top diagonal-locked clusters
- L358 — `void AppendDesignDecisions(StringBuilder sb)` — the fixed appendix: 4-cardinal viability conclusion, 0.5m/0.25m cell-size decision, named pinch-point areas (m14ab/m14ac/m44ad/m47ac/m28aa), rejected alternatives
- L410-497 — `struct V2`, `struct Tri`, `class RoomResult`, `record ClusterStats`, `class AreaResult` (with `Aggregate()`)
- L498 — `RoomResult? AnalyseRoom(string path, float cellSize)` — parses one `.wok`, computes walkable area/perimeter/angle bins, best local rotation, sparse rasterisation at 3 cell sizes, BFS reach4/reach8, smoothness, diagonal-locked clusters
- L663 — `void BumpEdge(...)`
- L669 — `int AngleBin(V2 a, V2 b, float rotateDeg)` — folds edge angle into an 18-bin [0°,90°) histogram, optionally under a rotated frame
- L689 — `float TriArea(Tri t)`
- L695 — `float MinInteriorAngleDeg(Tri t)` — law-of-cosines per-triangle min angle (skinny-triangle detector)
- L713 — `float Dist(V2 a, V2 b)`
- L719 — `bool PointInTri(...)` — barycentric sign test
- L737 — `long PackCell(int cx, int cy)` / L740 `UnpackCell` — sparse grid key packing tolerant of negative coords
- L746 — `float SparseRasterArea(List<Tri> tris, float cell)`
- L752 — `HashSet<long> SparseRasterise(...)` — bbox-bounded, per-triangle cell-centre point-in-triangle test; bails on degenerate (skybox) bboxes >10M cells
- L781 — `HashSet<long> SparseBfs(...)` — 4- or 8-connected BFS over the sparse cell set
- L804 — `void ComputeSmoothness(...)` — dense-grid (bbox-of-walkable-cells-only) 8-direction run-length DP classifying each cell smooth/fiddly/choke
  note: thresholds are cell-size-relative (5m/2m/1m converted to cell counts), so results stay consistent across `--cell` values
- L957 — `List<HashSet<long>> ConnectedComponents8(HashSet<long> cells)` — 8-connectivity component labelling
- L989/994 — `ReadU32`/`ReadF32`
