using System.CommandLine;
using System.CommandLine.Invocation;
using System.Globalization;
using System.IO;
using System.Text;

namespace Kdev.Commands;

/// <summary>
/// Geometry-shape audit of extracted KOTOR area walkmeshes. Answers the
/// "can we simplify navigation to a 4-cardinal grid" design question by
/// quantifying how axis-aligned the world actually is and how much
/// walkable area a grid-rasterisation would shed.
///
/// Per room / per area / global, we compute:
///   1. Edge-angle histogram (perimeter edges, length-weighted, folded to
///      [0°, 90°)).
///   2. Rectilinear fraction under world axes — fraction of perimeter
///      edge length within ±5° of {0°, 90°}.
///   3. Best local rotation — angle θ in [0°, 90°) that maximises the
///      rectilinear fraction after rotating the room by θ. Tells us
///      whether rooms are rectilinear-but-rotated (axis-aligned in a
///      local frame).
///   4. Grid rasterisation at cell sizes 0.25, 0.5, 1.0 m: walkable
///      area lost when we snap to a centre-walkable grid.
///   5. 4-cardinal vs 8-way reachability at 0.5 m: BFS from the centroid
///      of the largest walkable triangle; cells reachable under each
///      adjacency model. Difference = area cut off by the diagonal ban.
///   6. Skinny-triangle counts: triangles whose smallest interior angle
///      is &lt; 10° (slivers a grid silently trashes).
///
/// Only area room walkmeshes are analysed — filenames matching mNNxxx
/// (e.g. m01aa_05a.wok). Creature / placeable / door walkmeshes share the
/// .wok extension but are not navigable surfaces and would skew the
/// numbers.
/// </summary>
public static class WalkmeshGeometryAuditCommand
{
    private const string DefaultSourceDir = @"build\wok-extract";

    public static Command Build()
    {
        var sourceOpt = new Option<string>(
            name: "--source",
            description: "Directory of extracted .wok files. Default: " + DefaultSourceDir,
            getDefaultValue: () => DefaultSourceDir);

        var reportOpt = new Option<string?>(
            name: "--report",
            description: "Path to write a markdown summary. Console output is unaffected.");

        var moduleOpt = new Option<string?>(
            name: "--module",
            description: "Filter to a single area prefix (e.g. m01aa). Omit for all areas.");

        var cellOpt = new Option<float>(
            name: "--cell",
            description: "Grid cell size in metres for reachability analysis. Default 0.5.",
            getDefaultValue: () => 0.5f);

        var cmd = new Command("walkmesh-geometry-audit",
            "Quantify how axis-aligned the world is and how much area a 4-cardinal grid would lose.")
        {
            sourceOpt, reportOpt, moduleOpt, cellOpt,
        };

        cmd.SetHandler((InvocationContext ctx) =>
        {
            var src = ctx.ParseResult.GetValueForOption(sourceOpt) ?? DefaultSourceDir;
            var report = ctx.ParseResult.GetValueForOption(reportOpt);
            var mod = ctx.ParseResult.GetValueForOption(moduleOpt);
            var cell = ctx.ParseResult.GetValueForOption(cellOpt);
            ctx.ExitCode = Run(src, report, mod, cell);
        });
        return cmd;
    }

    private static int Run(string source, string? reportPath, string? moduleFilter, float cellSize)
    {
        if (!Directory.Exists(source))
        {
            Console.Error.WriteLine($"Source dir not found: {source}");
            return 1;
        }

        var allFiles = Directory.GetFiles(source, "*.wok");
        Array.Sort(allFiles, StringComparer.OrdinalIgnoreCase);

        // Match area room walkmeshes: filename starts with m##.
        var byArea = new SortedDictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        foreach (var path in allFiles)
        {
            var name = Path.GetFileNameWithoutExtension(path);
            if (name.Length < 4 || name[0] != 'm') continue;
            if (!char.IsDigit(name[1]) || !char.IsDigit(name[2])) continue;
            int us = name.LastIndexOf('_');
            if (us <= 0) continue;
            var prefix = name.Substring(0, us);
            // Prefix match: --module m14 picks m14aa/m14ab/m14ac/...; --module m14ab
            // restricts to exactly that area's rooms. Exact match falls out of the
            // startswith for full prefixes.
            if (moduleFilter != null && !prefix.StartsWith(moduleFilter, StringComparison.OrdinalIgnoreCase))
                continue;
            if (!byArea.TryGetValue(prefix, out var list))
            {
                list = new List<string>();
                byArea[prefix] = list;
            }
            list.Add(path);
        }

        if (byArea.Count == 0)
        {
            Console.Error.WriteLine("No area .wok files matched.");
            return 1;
        }

        var areas = new List<AreaResult>();
        int totalRooms = 0;
        foreach (var (prefix, files) in byArea)
        {
            var ar = new AreaResult { AreaPrefix = prefix };
            foreach (var path in files)
            {
                try
                {
                    var room = WalkmeshGeometryAnalysis.AnalyseRoom(path, cellSize);
                    if (room != null)
                    {
                        ar.Rooms.Add(room);
                        totalRooms++;
                    }
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine($"  WARN: {Path.GetFileName(path)}: {ex.Message}");
                }
            }
            ar.Aggregate();
            areas.Add(ar);
        }

        Console.WriteLine($"Analysed {areas.Count} areas, {totalRooms} rooms.");
        Console.WriteLine();

        // Global aggregate.
        var global = new AreaResult { AreaPrefix = "(global)" };
        foreach (var a in areas)
            global.Rooms.AddRange(a.Rooms);
        global.Aggregate();

        PrintGlobal(global, areas, cellSize);

        if (reportPath != null)
        {
            WriteReport(reportPath, global, areas, cellSize);
            Console.WriteLine();
            Console.WriteLine($"Report written to {reportPath}");
        }

        return 0;
    }

    private static void PrintGlobal(AreaResult global, List<AreaResult> areas, float cell)
    {
        Console.WriteLine("=== Global ===");
        Console.WriteLine($"Walkable triangles: {global.TriangleCount}");
        Console.WriteLine($"Walkable area: {global.WalkableArea:F1} m²");
        Console.WriteLine($"Perimeter edges: {global.PerimeterEdgeCount}  total length {global.PerimeterLength:F1} m");
        Console.WriteLine();
        Console.WriteLine($"Rectilinear (±5° of cardinal): {global.RectilinearFractionWorld * 100:F1}%  (world axes)");
        Console.WriteLine($"Rectilinear (±5° of cardinal): {global.RectilinearFractionLocalAvg * 100:F1}%  (per-room best rotation, length-weighted mean)");
        Console.WriteLine();
        Console.WriteLine("Edge-angle histogram (length-weighted, world axes, folded [0°, 90°), 5° bins):");
        for (int b = 0; b < 18; b++)
        {
            double frac = global.PerimeterLength > 0
                ? global.AngleBinsWorld[b] / global.PerimeterLength
                : 0;
            int bars = (int)Math.Round(frac * 100);
            Console.WriteLine($"  {b * 5,2}-{(b + 1) * 5,2}°  {frac * 100,5:F1}%  {new string('#', Math.Min(bars, 80))}");
        }
        Console.WriteLine();
        Console.WriteLine($"Grid rasterisation (cell-centre walkable rule):");
        Console.WriteLine($"  cell 0.25 m: {global.RasterArea025 / global.WalkableArea * 100:F1}% of true walkable area");
        Console.WriteLine($"  cell 0.50 m: {global.RasterArea050 / global.WalkableArea * 100:F1}% of true walkable area");
        Console.WriteLine($"  cell 1.00 m: {global.RasterArea100 / global.WalkableArea * 100:F1}% of true walkable area");
        Console.WriteLine();
        Console.WriteLine($"BFS reachability at cell {cell:F2} m (sum across rooms, seed = centroid of largest walkable triangle):");
        Console.WriteLine($"  total walkable cells     : {global.WalkableCellCount050}  ({global.WalkableCellCount050 * cell * cell:F1} m²)");
        Console.WriteLine($"  8-way reachable from seed: {global.Reach8} cells ({global.Reach8 * cell * cell:F1} m²)");
        Console.WriteLine($"  4-cardinal from seed     : {global.Reach4} cells ({global.Reach4 * cell * cell:F1} m²)");
        double lossDiag = global.Reach8 > 0
            ? (1.0 - (double)global.Reach4 / global.Reach8) * 100
            : 0;
        double cov4 = global.WalkableCellCount050 > 0
            ? (double)global.Reach4 / global.WalkableCellCount050 * 100
            : 0;
        Console.WriteLine($"  4-cardinal coverage of walkable: {cov4:F1}%   (diagonal-ban loss vs 8-way: {lossDiag:F1}%)");
        Console.WriteLine();
        Console.WriteLine($"Smoothness (per-cell longest run, sampled over {global.SmoothnessSampleCells} walkable cells):");
        if (global.SmoothnessSampleCells > 0)
        {
            double meanCardCells = (double)global.CardinalRunSumCells / global.SmoothnessSampleCells;
            double meanDiagCells = (double)global.DiagonalRunSumCells / global.SmoothnessSampleCells;
            Console.WriteLine($"  mean max-cardinal run: {meanCardCells * cell:F2} m   mean max-diagonal run: {meanDiagCells * cell:F2} m");
            Console.WriteLine($"  smooth (≥5 m cardinal): {global.SmoothCells * 100.0 / global.SmoothnessSampleCells:F1}%");
            Console.WriteLine($"  fiddly (≤2 m cardinal, ≥5 m diagonal): {global.FiddlyCells * 100.0 / global.SmoothnessSampleCells:F1}%");
            Console.WriteLine($"  choke (≤1 m cardinal & ≤1 m diagonal): {global.ChokeCells * 100.0 / global.SmoothnessSampleCells:F1}%");
        }
        Console.WriteLine();
        Console.WriteLine($"Skinny triangles (min interior angle < 10°): {global.SkinnyTriangles}  ({(global.TriangleCount > 0 ? global.SkinnyTriangles * 100.0 / global.TriangleCount : 0):F1}% of walkable)");
        Console.WriteLine();
        Console.WriteLine("Most fiddly 10 areas (highest % of cells in diagonal corridors):");
        foreach (var a in areas.Where(x => x.SmoothnessSampleCells > 0)
                              .OrderByDescending(x => (double)x.FiddlyCells / x.SmoothnessSampleCells)
                              .Take(10))
        {
            double pct = (double)a.FiddlyCells / a.SmoothnessSampleCells * 100;
            double smoothPct = (double)a.SmoothCells / a.SmoothnessSampleCells * 100;
            Console.WriteLine($"  {a.AreaPrefix,-10}  fiddly {pct,5:F1}%   smooth {smoothPct,5:F1}%");
        }
        Console.WriteLine();
        Console.WriteLine("Least-rectilinear 10 areas (world axes):");
        foreach (var a in areas.OrderBy(x => x.RectilinearFractionWorld).Take(10))
            Console.WriteLine($"  {a.AreaPrefix,-10}  world {a.RectilinearFractionWorld * 100,5:F1}%  local {a.RectilinearFractionLocalAvg * 100,5:F1}%");
        Console.WriteLine();
        Console.WriteLine("Worst 10 areas by 4-cardinal reachability loss:");
        foreach (var a in areas.Where(x => x.Reach8 > 0)
                              .OrderByDescending(x => 1.0 - (double)x.Reach4 / x.Reach8)
                              .Take(10))
        {
            double pct = (1.0 - (double)a.Reach4 / a.Reach8) * 100;
            Console.WriteLine($"  {a.AreaPrefix,-10}  4-card {a.Reach4}/{a.Reach8}  loss {pct:F1}%");
        }
    }

    private static void WriteReport(string path, AreaResult global, List<AreaResult> areas, float cell)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var sb = new StringBuilder();
        sb.AppendLine("# Walkmesh geometry audit");
        sb.AppendLine();
        sb.AppendLine($"Generated: {DateTime.UtcNow:yyyy-MM-dd HH:mm} UTC");
        sb.AppendLine($"Source: build/wok-extract/*.wok — area room walkmeshes only.");
        sb.AppendLine($"Cell size for reachability: {cell:F2} m.");
        sb.AppendLine();
        sb.AppendLine("## Headline numbers");
        sb.AppendLine();
        sb.AppendLine($"- Areas analysed: {areas.Count}");
        sb.AppendLine($"- Rooms analysed: {global.Rooms.Count}");
        sb.AppendLine($"- Walkable triangles: {global.TriangleCount}");
        sb.AppendLine($"- Total walkable area: {global.WalkableArea:F1} m²");
        sb.AppendLine($"- Total perimeter length: {global.PerimeterLength:F1} m");
        sb.AppendLine($"- Rectilinear fraction, world axes: {global.RectilinearFractionWorld * 100:F1}%");
        sb.AppendLine($"- Rectilinear fraction, per-room best rotation: {global.RectilinearFractionLocalAvg * 100:F1}%");
        sb.AppendLine($"- Total walkable cells at {cell:F2} m: {global.WalkableCellCount050} ({global.WalkableCellCount050 * cell * cell:F0} m²)");
        sb.AppendLine($"- 8-way reachable from seed: {global.Reach8} ({global.Reach8 * cell * cell:F0} m²)");
        sb.AppendLine($"- 4-cardinal reachable from seed: {global.Reach4} ({global.Reach4 * cell * cell:F0} m²)");
        double globLoss = global.Reach8 > 0 ? (1.0 - (double)global.Reach4 / global.Reach8) * 100 : 0;
        double globCov4 = global.WalkableCellCount050 > 0 ? (double)global.Reach4 / global.WalkableCellCount050 * 100 : 0;
        sb.AppendLine($"- 4-cardinal coverage of walkable cells: {globCov4:F1}%");
        sb.AppendLine($"- Cells uniquely unlocked by diagonals: {globLoss:F1}% (4-card vs 8-way from same seed)");
        if (global.SmoothnessSampleCells > 0)
        {
            double meanCardCells = (double)global.CardinalRunSumCells / global.SmoothnessSampleCells;
            double meanDiagCells = (double)global.DiagonalRunSumCells / global.SmoothnessSampleCells;
            sb.AppendLine($"- Mean longest cardinal run per cell: {meanCardCells * cell:F2} m");
            sb.AppendLine($"- Mean longest diagonal run per cell: {meanDiagCells * cell:F2} m");
            sb.AppendLine($"- Smooth cells (≥5 m cardinal run): {global.SmoothCells * 100.0 / global.SmoothnessSampleCells:F1}%");
            sb.AppendLine($"- Fiddly cells (cardinal ≤2 m, diagonal ≥5 m → forced zigzag in 4-card): {global.FiddlyCells * 100.0 / global.SmoothnessSampleCells:F1}%");
            sb.AppendLine($"- Choke cells (cardinal ≤1 m & diagonal ≤1 m → single-cell pinch): {global.ChokeCells * 100.0 / global.SmoothnessSampleCells:F1}%");
        }
        sb.AppendLine($"- Skinny triangles (min angle < 10°): {global.SkinnyTriangles} of {global.TriangleCount} ({(global.TriangleCount > 0 ? global.SkinnyTriangles * 100.0 / global.TriangleCount : 0):F1}%)");
        sb.AppendLine();
        sb.AppendLine("## Edge-angle histogram (world axes)");
        sb.AppendLine();
        sb.AppendLine("Perimeter-edge length per 5° bin, folded into [0°, 90°). Cardinal bins (0-5° and 85-90°) measure axis-alignment under the world frame.");
        sb.AppendLine();
        for (int b = 0; b < 18; b++)
        {
            double frac = global.PerimeterLength > 0
                ? global.AngleBinsWorld[b] / global.PerimeterLength
                : 0;
            int bars = (int)Math.Round(frac * 100);
            sb.AppendLine($"- {b * 5,2}-{(b + 1) * 5,2}°: {frac * 100:F1}%  `{new string('#', Math.Min(bars, 60))}`");
        }
        sb.AppendLine();
        sb.AppendLine("## Grid rasterisation loss (centre-walkable rule)");
        sb.AppendLine();
        sb.AppendLine($"- 0.25 m cells: {global.RasterArea025 / global.WalkableArea * 100:F1}% of walkable area kept");
        sb.AppendLine($"- 0.50 m cells: {global.RasterArea050 / global.WalkableArea * 100:F1}% of walkable area kept");
        sb.AppendLine($"- 1.00 m cells: {global.RasterArea100 / global.WalkableArea * 100:F1}% of walkable area kept");
        sb.AppendLine();
        sb.AppendLine($"## Per-area breakdown");
        sb.AppendLine();
        sb.AppendLine("Sorted by 4-cardinal reachability loss (worst first).");
        sb.AppendLine();
        var sorted = areas
            .OrderByDescending(a => a.Reach8 > 0 ? 1.0 - (double)a.Reach4 / a.Reach8 : 0)
            .ToList();
        foreach (var a in sorted)
        {
            double loss = a.Reach8 > 0 ? (1.0 - (double)a.Reach4 / a.Reach8) * 100 : 0;
            sb.AppendLine($"### {a.AreaPrefix}");
            sb.AppendLine();
            sb.AppendLine($"- Rooms: {a.Rooms.Count}");
            sb.AppendLine($"- Walkable area: {a.WalkableArea:F1} m²");
            sb.AppendLine($"- Rectilinear (world): {a.RectilinearFractionWorld * 100:F1}%");
            sb.AppendLine($"- Rectilinear (local best rotation, length-weighted mean): {a.RectilinearFractionLocalAvg * 100:F1}%");
            sb.AppendLine($"- Per-room best rotations (deg): {string.Join(", ", a.Rooms.OrderBy(r => r.RoomName).Select(r => r.LocalBestRotationDeg.ToString("F0", CultureInfo.InvariantCulture)))}");
            sb.AppendLine($"- Grid kept @0.5 m: {(a.WalkableArea > 0 ? a.RasterArea050 / a.WalkableArea * 100 : 0):F1}%");
            double cov = a.WalkableCellCount050 > 0 ? (double)a.Reach4 / a.WalkableCellCount050 * 100 : 0;
            sb.AppendLine($"- Walkable cells: {a.WalkableCellCount050}; 8-way reach: {a.Reach8}; 4-card reach: {a.Reach4}; 4-card coverage: {cov:F1}%; diagonal-ban loss: {loss:F1}%");
            if (a.SmoothnessSampleCells > 0)
            {
                double aMeanCardCells = (double)a.CardinalRunSumCells / a.SmoothnessSampleCells;
                sb.AppendLine($"- Smoothness: mean cardinal run {aMeanCardCells * cell:F2} m; smooth cells {a.SmoothCells * 100.0 / a.SmoothnessSampleCells:F1}%; fiddly cells {a.FiddlyCells * 100.0 / a.SmoothnessSampleCells:F1}%; choke cells {a.ChokeCells * 100.0 / a.SmoothnessSampleCells:F1}%");
            }
            sb.AppendLine($"- Skinny triangles (<10°): {a.SkinnyTriangles} of {a.TriangleCount}");

            // Pool all rooms' clusters, rank, and show the top 8. Bbox is in
            // world coordinates (metres) — drop into the in-game console with
            // `coords` to teleport to the loss region for inspection.
            var clusters = a.Rooms
                .SelectMany(r => r.DiagLockedClusters.Select(c => (room: r.RoomName, cluster: c)))
                .OrderByDescending(x => x.cluster.CellCount)
                .Take(8)
                .ToList();
            if (clusters.Count > 0)
            {
                sb.AppendLine($"- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):");
                foreach (var (room, cl) in clusters)
                {
                    sb.AppendLine($"    - {room}: {cl.CellCount} cells, x={cl.MinX:F1}..{cl.MaxX:F1}, y={cl.MinY:F1}..{cl.MaxY:F1} (size {cl.MaxX - cl.MinX:F1} × {cl.MaxY - cl.MinY:F1})");
                }
            }
            sb.AppendLine();
        }

        AppendDesignDecisions(sb);

        File.WriteAllText(path, sb.ToString());
    }

    /// <summary>Appendix preserved across regenerations of this report so
    /// the design intent and the audit data stay co-located. Update here
    /// rather than editing the .md file in place.</summary>
    private static void AppendDesignDecisions(StringBuilder sb)
    {
        sb.AppendLine("## Design decisions (proposed)");
        sb.AppendLine();
        sb.AppendLine("Outcome of this audit for the accessibility-mod navigation model. Captured here as the canonical reference for future implementation.");
        sb.AppendLine();
        sb.AppendLine("### Movement model: 4-cardinal viable game-wide");
        sb.AppendLine();
        sb.AppendLine("- 4-cardinal input (N/S/E/W only) reaches 96.4% of walkable cells from a typical seed. The 3.6% gap is mostly disconnected walkmesh islands that are also unreachable on foot in vanilla.");
        sb.AppendLine("- 99.7% of walkable cells have a straight cardinal run of ≥5 m available. The world is built on cardinal corridors with curved decorative perimeters — exactly the opposite shape of what would hurt 4-card play.");
        sb.AppendLine("- 0.0% fiddly cells globally (no diagonal corridors that force zigzag). 0.0% choke cells.");
        sb.AppendLine("- Diagonal input gives a 0.3% reachability bonus over 4-card. Not worth a dedicated input affordance for the general case.");
        sb.AppendLine();
        sb.AppendLine("### Cell size: 0.5 m default, 0.25 m in pinch areas");
        sb.AppendLine();
        sb.AppendLine("The cell size is a **model resolution**, not a step size. The KOTOR character keeps moving continuously at the engine's normal ~5 m/s — the grid is the screen-reader's mental map for narration, distance announcements, and connectivity checks. Player perception of speed and inertia is unchanged.");
        sb.AppendLine();
        sb.AppendLine("- **Default 0.5 m** globally. Cheap, well-matched to KOTOR's typical 0.5-1 m doorways, and the character's 0.13 m collision radius is already finer than this so we lose no perceptible precision.");
        sb.AppendLine("- **0.25 m** in the five flagged pinch-point areas (see list below). At this resolution the narrow diagonal pinches that block 4-cardinal BFS at 0.5 m become 2-3 cells wide and traverse normally. 4× memory and CPU per area, negligible globally.");
        sb.AppendLine("- **Never use cell size as step size.** A 0.25 m step at fixed per-press cadence would feel sluggish (~10× slower than current run speed). The grid is for *describing* the world, not for *driving* the character.");
        sb.AppendLine();
        sb.AppendLine("### Pinch-point areas (use 0.25 m grid)");
        sb.AppendLine();
        sb.AppendLine("From the per-area breakdown above, ordered by diagonal-ban loss at 0.5 m. These are the only areas where the model resolution matters for navigation correctness.");
        sb.AppendLine();
        sb.AppendLine("- **m14ab** — Dantooine. 21.5% loss, one 9300 m² cluster behind a single pinch at world coords x=282..394, y=36..171 inside room m14ab_02a.");
        sb.AppendLine("- **m14ac** — Dantooine. 13.4% loss, one 1500 m² cluster at x=310..365, y=337..398 inside m14ac_10d.");
        sb.AppendLine("- **m44ad** — Star Forge. 10.1% loss in a single 888 m² room.");
        sb.AppendLine("- **m47ac** — Lehon (Unknown World). 6.4% loss.");
        sb.AppendLine("- **m28aa** — Dantooine. 4.4% loss.");
        sb.AppendLine();
        sb.AppendLine("All other areas show < 1.1% diagonal-ban loss at 0.5 m and can stay at default resolution.");
        sb.AppendLine();
        sb.AppendLine("### Alternative for pinch handling: slip-diagonal rule");
        sb.AppendLine();
        sb.AppendLine("If per-area cell-size variation proves awkward (e.g. crossing area boundaries needs grid re-binning), an equivalent fix at uniform 0.5 m: when a 4-cardinal press would land on a non-walkable cell but the adjacent diagonal cell *is* walkable, slip the character one diagonal step. Transparent to the player, never exposed as input, fires only at the handful of pinch points where the engine would otherwise block. Choose this over per-area resolution if a single, uniform model is preferable.");
        sb.AppendLine();
        sb.AppendLine("### Things this audit explicitly does NOT recommend");
        sb.AppendLine();
        sb.AppendLine("- **Stepped/snap movement (one press = one cell).** Would map cell size to step size and feel tedious at any resolution under 1 m. Keep engine locomotion continuous.");
        sb.AppendLine("- **Locally-rotated grids per area.** The per-room best-rotation column in the report shows most areas are within 5° of world axes; the +5pp gain from rotating per room (65.5% → 70.5% rectilinear globally) does not justify the speech-and-mental-model complexity of running multiple frames concurrently.");
        sb.AppendLine("- **Diagonal-only input affordance (Q/E etc.) as a default.** The 0.3% reachability bonus and 0.0% smoothness loss don't motivate it. Could still be useful as a per-pinch escape hatch if the slip-diagonal rule proves insufficient.");
        sb.AppendLine();
        sb.AppendLine("### Regenerating this report");
        sb.AppendLine();
        sb.AppendLine("    kdev walkmesh-geometry-audit --source build/wok-extract --report docs/walkmesh-geometry-audit.md");
        sb.AppendLine();
        sb.AppendLine("This appendix is emitted by the audit command itself; edits to the data sections above will be overwritten on next run, but the design decisions stay in source at `tools/kdev/Commands/WalkmeshGeometryAuditCommand.cs:AppendDesignDecisions`.");
    }

}
