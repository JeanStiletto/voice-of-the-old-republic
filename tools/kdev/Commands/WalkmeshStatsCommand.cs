using System.CommandLine;
using System.CommandLine.Invocation;
using System.Globalization;
using System.IO;
using System.Text;

using static Kdev.BwmFile;

namespace Kdev.Commands;

/// <summary>
/// Analyse extracted KOTOR walkmesh (.wok) files to surface the spatial
/// scales the accessibility wall-cue tuner needs answered: edge length
/// distribution, per-edge "corridor width" (perpendicular distance to
/// nearest parallel opposing wall in the same room), and smallest
/// navigable passages per area.
///
/// Why: tuning Pillar 1 wall cues (zone borders, raycast spacing, hysteresis)
/// requires knowing whether KOTOR has 0.5 m hidden gaps that a coarse
/// detector would miss, or whether minimum corridor width is closer to 1.5 m.
/// We have the raw walkmesh data — answer empirically rather than guess.
///
/// Pre-extract command (run once):
///   mkdir -p build/wok-extract && cd build/wok-extract && \
///     "C:/Tools/xoreos-tools-0.0.6/unkeybif.exe" e \
///       "&lt;steam&gt;/data/models.bif" "&lt;steam&gt;/chitin.key"
///
/// Then:  kdev walkmesh-stats
/// </summary>
public static class WalkmeshStatsCommand
{
    private const string DefaultSourceDir = @"build\wok-extract";

    // KOTOR surfacemat.2da walk column — walkable face_type ids.
    // Derived from the 2DA shipped in data/2da.bif (rows 0..30).
    // Non-walkable: NotDefined, Obscuring, Nonwalk, Transparent, Lava,
    // BottomlessPit, DeepWater, NonWalkGrass, CRAP. All others walk.

    public static Command Build()
    {
        var sourceOpt = new Option<string>(
            name: "--source",
            description: "Directory of extracted .wok files. Default: " + DefaultSourceDir,
            getDefaultValue: () => DefaultSourceDir);

        var moduleOpt = new Option<string?>(
            name: "--module",
            description: "Filter to a single area prefix (e.g. m01ab, m26aa). Omit for all areas.");

        var detailOpt = new Option<bool>(
            name: "--detail",
            description: "Print per-room breakdown for the selected area(s). Default off.");

        var minOnlyOpt = new Option<bool>(
            name: "--min-only",
            description: "Print only the global minimum passage width per area. One line each.");

        // Walkmesh has thin-strip artifacts (slivers along obstacle boundaries,
        // doorway rim triangles) that produce sub-decimetre "passages" the
        // player can never traverse — KOTOR's character PERSPACE is ~0.13 m
        // (radius), so any corridor < 2*PERSPACE = 0.26 m is impassable, and
        // a generous 0.5 m default trims the noise floor without hiding any
        // realistically-navigable doorway. Bump down to see raw artifacts.
        var minNavigableOpt = new Option<float>(
            name: "--min-navigable",
            description: "Filter out passages narrower than this (m); default 0.5.",
            getDefaultValue: () => 0.5f);

        var cmd = new Command("walkmesh-stats",
            "Analyse extracted KOTOR walkmesh files for edge / corridor-width statistics.")
        {
            sourceOpt, moduleOpt, detailOpt, minOnlyOpt, minNavigableOpt,
        };

        cmd.SetHandler((InvocationContext ctx) =>
        {
            var src = ctx.ParseResult.GetValueForOption(sourceOpt) ?? DefaultSourceDir;
            var mod = ctx.ParseResult.GetValueForOption(moduleOpt);
            var det = ctx.ParseResult.GetValueForOption(detailOpt);
            var minOnly = ctx.ParseResult.GetValueForOption(minOnlyOpt);
            var minNav = ctx.ParseResult.GetValueForOption(minNavigableOpt);
            ctx.ExitCode = Run(src, mod, det, minOnly, minNav);
        });
        return cmd;
    }

    private static int Run(string source, string? moduleFilter, bool detail, bool minOnly, float minNavigable)
    {
        if (!Directory.Exists(source))
        {
            Console.Error.WriteLine($"Source dir not found: {source}");
            Console.Error.WriteLine(
                "Extract first via: \"C:/Tools/xoreos-tools-0.0.6/unkeybif.exe\" e " +
                "\"<steam>/data/models.bif\" \"<steam>/chitin.key\"");
            return 1;
        }

        var allFiles = Directory.GetFiles(source, "*.wok");
        Array.Sort(allFiles, StringComparer.OrdinalIgnoreCase);

        // Group by area prefix (filename up to the last underscore).
        var byArea = new SortedDictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        foreach (var path in allFiles)
        {
            var name = Path.GetFileNameWithoutExtension(path);
            int us = name.LastIndexOf('_');
            if (us <= 0) continue;
            var prefix = name.Substring(0, us);
            if (moduleFilter != null && !prefix.Equals(moduleFilter, StringComparison.OrdinalIgnoreCase))
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
            Console.Error.WriteLine("No .wok files matched.");
            return 1;
        }

        // Aggregate stats across all areas for the global summary.
        var allEdgeLengths = new List<float>();
        var allCorridorWidths = new List<float>();
        var areaSummaries = new List<AreaSummary>();

        foreach (var (area, files) in byArea)
        {
            var summary = new AreaSummary { AreaPrefix = area };
            foreach (var path in files)
            {
                try
                {
                    var room = ParseAndAnalyse(path, minNavigable);
                    summary.Rooms.Add(room);
                    summary.TotalPerimeterEdges += room.PerimeterEdgeCount;
                    summary.EdgeLengths.AddRange(room.EdgeLengths);
                    summary.CorridorWidths.AddRange(room.CorridorWidths);
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine($"  WARN: {Path.GetFileName(path)}: {ex.Message}");
                }
            }
            areaSummaries.Add(summary);
            allEdgeLengths.AddRange(summary.EdgeLengths);
            allCorridorWidths.AddRange(summary.CorridorWidths);
        }

        if (minOnly)
        {
            foreach (var a in areaSummaries.OrderBy(x => MinOrInf(x.CorridorWidths)))
            {
                var minW = MinOrInf(a.CorridorWidths);
                var minStr = float.IsInfinity(minW) ? "(no parallel walls)" : $"{minW:F2} m";
                Console.WriteLine($"{a.AreaPrefix,-10}  {a.Rooms.Count,3} rooms  " +
                    $"{a.TotalPerimeterEdges,5} walls  min passage {minStr}");
            }
            PrintGlobal(allEdgeLengths, allCorridorWidths, areaSummaries);
            return 0;
        }

        foreach (var a in areaSummaries)
        {
            PrintArea(a, detail);
        }

        PrintGlobal(allEdgeLengths, allCorridorWidths, areaSummaries);
        return 0;
    }

    private static void PrintArea(AreaSummary a, bool detail)
    {
        Console.WriteLine();
        Console.WriteLine($"Area: {a.AreaPrefix}");
        Console.WriteLine($"  Rooms: {a.Rooms.Count}");
        Console.WriteLine($"  Perimeter edges: {a.TotalPerimeterEdges}");
        if (a.EdgeLengths.Count > 0)
        {
            Console.WriteLine($"  Edge length: min {Min(a.EdgeLengths):F2} m, p10 {Percentile(a.EdgeLengths, 0.10f):F2} m, " +
                $"p50 {Percentile(a.EdgeLengths, 0.50f):F2} m, p90 {Percentile(a.EdgeLengths, 0.90f):F2} m, " +
                $"max {Max(a.EdgeLengths):F2} m");
        }
        if (a.CorridorWidths.Count > 0)
        {
            Console.WriteLine($"  Corridor width samples: {a.CorridorWidths.Count}");
            Console.WriteLine($"    min {Min(a.CorridorWidths):F2} m, p10 {Percentile(a.CorridorWidths, 0.10f):F2} m, " +
                $"p50 {Percentile(a.CorridorWidths, 0.50f):F2} m, p90 {Percentile(a.CorridorWidths, 0.90f):F2} m");
        }
        else
        {
            Console.WriteLine("  Corridor width samples: 0 (no parallel-wall pairs found)");
        }
        if (detail)
        {
            foreach (var r in a.Rooms.OrderBy(x => x.RoomName))
            {
                var minW = MinOrInf(r.CorridorWidths);
                var minStr = float.IsInfinity(minW) ? "—" : $"{minW:F2} m";
                Console.WriteLine($"    {r.RoomName,-20}  walkable_faces={r.WalkableFaceCount,4}  " +
                    $"perim={r.PerimeterEdgeCount,4}  min_passage={minStr}");
            }
        }
    }

    private static void PrintGlobal(List<float> edges, List<float> corridors, List<AreaSummary> areas)
    {
        Console.WriteLine();
        Console.WriteLine("=== Global summary ===");
        Console.WriteLine($"Areas analysed: {areas.Count}");
        Console.WriteLine($"Total perimeter edges: {areas.Sum(a => a.TotalPerimeterEdges)}");
        if (edges.Count > 0)
        {
            Console.WriteLine($"Edge length: min {Min(edges):F2} m, p10 {Percentile(edges, 0.10f):F2} m, " +
                $"p50 {Percentile(edges, 0.50f):F2} m, p90 {Percentile(edges, 0.90f):F2} m, max {Max(edges):F2} m");
        }
        if (corridors.Count > 0)
        {
            Console.WriteLine($"Corridor widths: {corridors.Count} samples");
            Console.WriteLine($"  min {Min(corridors):F2} m, p1 {Percentile(corridors, 0.01f):F2} m, " +
                $"p10 {Percentile(corridors, 0.10f):F2} m, p50 {Percentile(corridors, 0.50f):F2} m");

            // Smallest passages — top 10 by min corridor width across all areas.
            var ranked = areas
                .Where(a => a.CorridorWidths.Count > 0)
                .OrderBy(a => MinOrInf(a.CorridorWidths))
                .Take(10);
            Console.WriteLine();
            Console.WriteLine("Tightest 10 areas by smallest passage:");
            foreach (var a in ranked)
            {
                Console.WriteLine($"  {a.AreaPrefix,-10}  min passage {MinOrInf(a.CorridorWidths):F2} m");
            }
        }
    }

    // --- Geometry / parser ---

    private struct V3 { public float X, Y, Z; }

    private record RoomStats(string RoomName, int WalkableFaceCount, int PerimeterEdgeCount,
        List<float> EdgeLengths, List<float> CorridorWidths);

    private class AreaSummary
    {
        public string AreaPrefix = "";
        public List<RoomStats> Rooms = new();
        public int TotalPerimeterEdges;
        public List<float> EdgeLengths = new();
        public List<float> CorridorWidths = new();
    }

    private static RoomStats ParseAndAnalyse(string path, float minNavigable)
    {
        var bytes = File.ReadAllBytes(path);
        if (bytes.Length < 0x88) throw new InvalidDataException("file too short for BWM header");
        if (Encoding.ASCII.GetString(bytes, 0, 8) != "BWM V1.0")
            throw new InvalidDataException("not a BWM v1.0 file");

        // Header layout (KotOR walkmesh, type=1):
        //   0x00  magic "BWM V1.0"
        //   0x08  type uint32 (1=walkmesh, 0=AABB-only door, 2=area AABB)
        //   0x0C  position float[3]
        //   0x18  reserved 48 bytes (12 of which are an unidentified Vec3)
        //   0x48  vertexCount uint32
        //   0x4C  vertexOffset uint32  → vertexCount * Vec3
        //   0x50  faceCount uint32
        //   0x54  faceOffset uint32    → faceCount * 3 uint32 (vertex indices)
        //   0x58  faceTypeOffset uint32 → faceCount * uint32 (surfacemat row id)
        //   ... (normals, distances, AABB nodes, adjacency, outer edges,
        //        perimeters — not needed: we recompute perimeter from face/vertex)
        uint type        = ReadU32(bytes, 0x08);
        uint vertexCount = ReadU32(bytes, 0x48);
        uint vertexOff   = ReadU32(bytes, 0x4C);
        uint faceCount   = ReadU32(bytes, 0x50);
        uint faceOff     = ReadU32(bytes, 0x54);
        uint faceTypeOff = ReadU32(bytes, 0x58);

        var roomName = Path.GetFileNameWithoutExtension(path);
        if (type != 1)
        {
            // type 0 (door AABB-only) or type 2 (area AABB): no walkable surface.
            return new RoomStats(roomName, 0, 0, new(), new());
        }

        if (vertexCount == 0 || faceCount == 0)
            return new RoomStats(roomName, 0, 0, new(), new());

        // Read vertices.
        var verts = new V3[vertexCount];
        for (int i = 0; i < vertexCount; i++)
        {
            int p = (int)vertexOff + i * 12;
            verts[i] = new V3
            {
                X = ReadF32(bytes, p),
                Y = ReadF32(bytes, p + 4),
                Z = ReadF32(bytes, p + 8),
            };
        }

        // Read faces + face types; keep only walkable faces.
        var walkableFaceVerts = new List<(uint A, uint B, uint C)>();
        for (int i = 0; i < faceCount; i++)
        {
            uint a = ReadU32(bytes, (int)faceOff + i * 12);
            uint b = ReadU32(bytes, (int)faceOff + i * 12 + 4);
            uint c = ReadU32(bytes, (int)faceOff + i * 12 + 8);
            int matType = (int)ReadU32(bytes, (int)faceTypeOff + i * 4);
            if (!WalkableTypes.Contains(matType)) continue;
            if (a >= vertexCount || b >= vertexCount || c >= vertexCount) continue;
            walkableFaceVerts.Add((a, b, c));
        }

        // Edge perimeter: an undirected edge appearing in only one walkable face
        // is on the walkable surface boundary == "wall" for navigation purposes.
        // Track the third vertex of each edge's face so we can derive a
        // walkable-side "inward" direction later — two walls form a navigable
        // passage only if both face *toward* the gap between them, otherwise
        // they're an obstacle perimeter or two unrelated walls.
        var edgeCount = new Dictionary<(uint, uint), int>();
        var edgeThirdVert = new Dictionary<(uint, uint), uint>();
        foreach (var f in walkableFaceVerts)
        {
            BumpEdgeWithThird(edgeCount, edgeThirdVert, f.A, f.B, f.C);
            BumpEdgeWithThird(edgeCount, edgeThirdVert, f.B, f.C, f.A);
            BumpEdgeWithThird(edgeCount, edgeThirdVert, f.C, f.A, f.B);
        }

        // perimeter[i] = (a, b, third). third is the in-face third vertex,
        // used to compute the walkable-side normal direction.
        var perimeter = new List<(uint A, uint B, uint Third)>();
        foreach (var kv in edgeCount)
        {
            if (kv.Value == 1)
                perimeter.Add((kv.Key.Item1, kv.Key.Item2, edgeThirdVert[kv.Key]));
        }

        // Edge length distribution (XY plane — KOTOR rooms are layout-flat enough
        // that 2D matches the player's perspective).
        var edgeLengths = new List<float>(perimeter.Count);
        foreach (var (ai, bi, _) in perimeter)
        {
            edgeLengths.Add(Length2D(verts[ai], verts[bi]));
        }

        // Per-edge "inward" direction (XY) — from edge midpoint toward the
        // third vertex of the edge's walkable face. This is the half-plane
        // containing walkable area; the wall is on the opposite side.
        var inwardX = new float[perimeter.Count];
        var inwardY = new float[perimeter.Count];
        var midX = new float[perimeter.Count];
        var midY = new float[perimeter.Count];
        for (int i = 0; i < perimeter.Count; i++)
        {
            var (ai, bi, ci) = perimeter[i];
            var a = verts[ai]; var b = verts[bi]; var c = verts[ci];
            midX[i] = (a.X + b.X) * 0.5f;
            midY[i] = (a.Y + b.Y) * 0.5f;
            float ix = c.X - midX[i];
            float iy = c.Y - midY[i];
            float il = MathF.Sqrt(ix * ix + iy * iy);
            inwardX[i] = il > 1e-6f ? ix / il : 0f;
            inwardY[i] = il > 1e-6f ? iy / il : 0f;
        }

        // Corridor width: for each perimeter edge, find the closest parallel
        // edge that doesn't share an endpoint, isn't collinear-and-touching,
        // and FACES this edge (their walkable sides point toward each other).
        // The face-toward filter rejects perimeters of obstacle islands and
        // unrelated parallel walls in different parts of the room — both are
        // common false positives without it.
        const float kCosParallel = 0.996f;       // ≤ ~5°
        const float kMinPerpDist = 0.10f;        // collinear filter
        var corridorWidths = new List<float>();
        for (int i = 0; i < perimeter.Count; i++)
        {
            var e = perimeter[i];
            var ea = verts[e.A]; var eb = verts[e.B];
            float ex = eb.X - ea.X, ey = eb.Y - ea.Y;
            float elen = MathF.Sqrt(ex * ex + ey * ey);
            if (elen < 1e-4f) continue;
            float edx = ex / elen, edy = ey / elen;

            float bestPerp = float.PositiveInfinity;
            for (int j = 0; j < perimeter.Count; j++)
            {
                if (j == i) continue;
                var f = perimeter[j];
                if (f.A == e.A || f.A == e.B || f.B == e.A || f.B == e.B) continue;
                var fa = verts[f.A]; var fb = verts[f.B];
                float fx = fb.X - fa.X, fy = fb.Y - fa.Y;
                float flen = MathF.Sqrt(fx * fx + fy * fy);
                if (flen < 1e-4f) continue;
                float fdx = fx / flen, fdy = fy / flen;
                float cosAng = MathF.Abs(edx * fdx + edy * fdy);
                if (cosAng < kCosParallel) continue;

                // Vector from E's mid to F's mid — the candidate passage axis.
                float toFx = midX[j] - midX[i];
                float toFy = midY[j] - midY[i];

                // Facing test: E's walkable side must point toward F (positive
                // projection of E.inward onto E→F), AND F's walkable side must
                // point toward E (positive projection of F.inward onto F→E).
                // Use a small slack so near-perpendicular walkable normals
                // (which can happen at thin walls) still qualify.
                float dotEf = inwardX[i] * toFx + inwardY[i] * toFy;
                float dotFe = inwardX[j] * (-toFx) + inwardY[j] * (-toFy);
                if (dotEf <= 0 || dotFe <= 0) continue;

                // Perpendicular distance from E's midpoint to F's infinite line.
                float vx = midX[i] - fa.X, vy = midY[i] - fa.Y;
                float perp = MathF.Abs(vx * fdy - vy * fdx);
                if (perp < kMinPerpDist) continue;

                // Sub-navigable artifact: thin walkable slivers, doorway-rim
                // triangles, etc. Skip without claiming the "best" slot — a
                // larger but real corridor on a different F is still the
                // answer for this edge.
                if (perp < minNavigable) continue;

                // Foot of the perpendicular must land roughly within F's
                // extent — otherwise F is "across the room", not "across the
                // corridor".
                float t = vx * fdx + vy * fdy;
                if (t < -0.5f || t > flen + 0.5f) continue;

                if (perp < bestPerp) bestPerp = perp;
            }

            if (!float.IsInfinity(bestPerp))
                corridorWidths.Add(bestPerp);
        }

        return new RoomStats(roomName, walkableFaceVerts.Count, perimeter.Count,
            edgeLengths, corridorWidths);
    }

    private static void BumpEdgeWithThird(Dictionary<(uint, uint), int> map,
        Dictionary<(uint, uint), uint> third, uint a, uint b, uint c)
    {
        var key = a < b ? (a, b) : (b, a);
        map[key] = map.TryGetValue(key, out var n) ? n + 1 : 1;
        // Only useful for perimeter edges (count==1), so latest wins is fine.
        third[key] = c;
    }

    private static float Length2D(V3 a, V3 b)
    {
        float dx = b.X - a.X, dy = b.Y - a.Y;
        return MathF.Sqrt(dx * dx + dy * dy);
    }

    // --- Stats helpers ---

    private static float Min(List<float> xs) => xs.Min();
    private static float Max(List<float> xs) => xs.Max();

    private static float MinOrInf(List<float> xs) =>
        xs.Count == 0 ? float.PositiveInfinity : xs.Min();

    private static float Percentile(List<float> xs, float p)
    {
        if (xs.Count == 0) return float.NaN;
        var sorted = xs.ToArray();
        Array.Sort(sorted);
        float rank = p * (sorted.Length - 1);
        int lo = (int)MathF.Floor(rank), hi = (int)MathF.Ceiling(rank);
        float frac = rank - lo;
        return sorted[lo] * (1 - frac) + sorted[hi] * frac;
    }
}
