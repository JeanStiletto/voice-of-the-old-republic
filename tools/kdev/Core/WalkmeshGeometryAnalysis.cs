using System.Text;
using System.Globalization;

using static Kdev.BwmFile;

namespace Kdev;

// ----------------------------------------------------------------------
// BWM walkmesh geometry analysis.
//
// Split out of Commands/WalkmeshGeometryAuditCommand.cs by the Phase-1
// structure pass (refactoring candidate 16). The command half is CLI
// wiring plus report/appendix writing; this is the algorithm underneath
// it - parse a room's walkmesh, rasterise the walkable surface, measure
// area / smoothness / connectivity, and hand back a result record.
//
// The result types are top-level so the reporting half can keep naming
// them unqualified.
// ----------------------------------------------------------------------

internal struct V2 { public float X, Y; public V2(float x, float y) { X = x; Y = y; } }
internal struct Tri { public V2 A, B, C; }

internal class RoomResult
{
    public string RoomName = "";
    public List<Tri> Triangles = new();
    public List<(V2 A, V2 B)> Perimeter = new();
    public float WalkableArea;
    public float PerimeterLength;
    public double[] AngleBinsWorld = new double[18];
    public float RectilinearFractionWorld;
    public float LocalBestRotationDeg;
    public float RectilinearFractionLocal;
    public float RasterArea025, RasterArea050, RasterArea100;
    public int WalkableCellCount050;
    public int Reach4, Reach8;
    public int SkinnyTriangles;
    public List<ClusterStats> DiagLockedClusters = new();
    // Smoothness: per-cell longest consecutive walkable run in cells,
    // separated by cardinal vs diagonal. Captured as cumulative counts
    // (sum across cells) so areas/global can average cheaply.
    public long CardinalRunSumCells;       // sum of max-cardinal-run per walkable cell
    public long DiagonalRunSumCells;       // sum of max-diagonal-run per walkable cell
    public int SmoothCells;                // max cardinal run ≥ 5m
    public int FiddlyCells;                // cardinal max ≤ 2m AND diagonal max ≥ 5m → forced zigzag
    public int ChokeCells;                 // cardinal max ≤ 1m AND diagonal max ≤ 1m → single-cell pinch
    public int SmoothnessSampleCells;      // walkable cells that contributed to the smoothness pass
}

/// <summary>Cells reachable by 8-way BFS but not 4-cardinal — i.e. the
/// region cut off when diagonals are banned. Grouped into 8-connected
/// components so we can see whether the loss is one big chunk or many
/// small slivers, and where the chunks live in world coordinates.</summary>
internal record ClusterStats(int CellCount, float MinX, float MinY, float MaxX, float MaxY);

internal class AreaResult
{
    public string AreaPrefix = "";
    public List<RoomResult> Rooms = new();
    public int TriangleCount;
    public float WalkableArea;
    public int PerimeterEdgeCount;
    public float PerimeterLength;
    public double[] AngleBinsWorld = new double[18];
    public float RectilinearFractionWorld;
    public float RectilinearFractionLocalAvg;
    public float RasterArea025, RasterArea050, RasterArea100;
    public int WalkableCellCount050;
    public int Reach4, Reach8;
    public int SkinnyTriangles;
    public long CardinalRunSumCells;
    public long DiagonalRunSumCells;
    public int SmoothCells;
    public int FiddlyCells;
    public int ChokeCells;
    public int SmoothnessSampleCells;

    public void Aggregate()
    {
        TriangleCount = Rooms.Sum(r => r.Triangles.Count);
        WalkableArea = Rooms.Sum(r => r.WalkableArea);
        PerimeterEdgeCount = Rooms.Sum(r => r.Perimeter.Count);
        PerimeterLength = Rooms.Sum(r => r.PerimeterLength);
        for (int b = 0; b < 18; b++)
            AngleBinsWorld[b] = Rooms.Sum(r => r.AngleBinsWorld[b]);
        RectilinearFractionWorld = PerimeterLength > 0
            ? (float)((AngleBinsWorld[0] + AngleBinsWorld[17]) / PerimeterLength)
            : 0;
        RectilinearFractionLocalAvg = PerimeterLength > 0
            ? (float)(Rooms.Sum(r => r.RectilinearFractionLocal * r.PerimeterLength) / PerimeterLength)
            : 0;
        RasterArea025 = Rooms.Sum(r => r.RasterArea025);
        RasterArea050 = Rooms.Sum(r => r.RasterArea050);
        RasterArea100 = Rooms.Sum(r => r.RasterArea100);
        WalkableCellCount050 = Rooms.Sum(r => r.WalkableCellCount050);
        Reach4 = Rooms.Sum(r => r.Reach4);
        Reach8 = Rooms.Sum(r => r.Reach8);
        SkinnyTriangles = Rooms.Sum(r => r.SkinnyTriangles);
        CardinalRunSumCells = Rooms.Sum(r => r.CardinalRunSumCells);
        DiagonalRunSumCells = Rooms.Sum(r => r.DiagonalRunSumCells);
        SmoothCells = Rooms.Sum(r => r.SmoothCells);
        FiddlyCells = Rooms.Sum(r => r.FiddlyCells);
        ChokeCells = Rooms.Sum(r => r.ChokeCells);
        SmoothnessSampleCells = Rooms.Sum(r => r.SmoothnessSampleCells);
    }
}


internal static class WalkmeshGeometryAnalysis
{

    internal static RoomResult? AnalyseRoom(string path, float cellSize)
    {
        var bytes = File.ReadAllBytes(path);
        if (!HasValidHeader(bytes)) return null;

        uint type        = ReadU32(bytes, OffType);
        uint vertexCount = ReadU32(bytes, OffVertexCount);
        uint vertexOff   = ReadU32(bytes, OffVertexOffset);
        uint faceCount   = ReadU32(bytes, OffFaceCount);
        uint faceOff     = ReadU32(bytes, OffFaceOffset);
        uint faceTypeOff = ReadU32(bytes, OffFaceTypeOffset);

        if (type != 1 || vertexCount == 0 || faceCount == 0) return null;

        var verts = new V2[vertexCount];
        for (int i = 0; i < vertexCount; i++)
        {
            int p = (int)vertexOff + i * 12;
            verts[i] = new V2(ReadF32(bytes, p), ReadF32(bytes, p + 4));
        }

        var result = new RoomResult { RoomName = Path.GetFileNameWithoutExtension(path) };
        var faceIdx = new List<(uint A, uint B, uint C)>();
        for (int i = 0; i < faceCount; i++)
        {
            uint a = ReadU32(bytes, (int)faceOff + i * 12);
            uint b = ReadU32(bytes, (int)faceOff + i * 12 + 4);
            uint c = ReadU32(bytes, (int)faceOff + i * 12 + 8);
            int mat = (int)ReadU32(bytes, (int)faceTypeOff + i * 4);
            if (!WalkableTypes.Contains(mat)) continue;
            if (a >= vertexCount || b >= vertexCount || c >= vertexCount) continue;
            faceIdx.Add((a, b, c));
            var tri = new Tri { A = verts[a], B = verts[b], C = verts[c] };
            result.Triangles.Add(tri);
            result.WalkableArea += TriArea(tri);
            if (MinInteriorAngleDeg(tri) < 10f) result.SkinnyTriangles++;
        }

        if (result.Triangles.Count == 0) return result;

        // Perimeter via edge-incidence count.
        var edgeCount = new Dictionary<(uint, uint), int>();
        foreach (var f in faceIdx)
        {
            BumpEdge(edgeCount, f.A, f.B);
            BumpEdge(edgeCount, f.B, f.C);
            BumpEdge(edgeCount, f.C, f.A);
        }
        foreach (var kv in edgeCount)
        {
            if (kv.Value == 1)
            {
                var a = verts[kv.Key.Item1];
                var b = verts[kv.Key.Item2];
                result.Perimeter.Add((a, b));
                float len = Dist(a, b);
                result.PerimeterLength += len;
                int bin = AngleBin(a, b, 0f);
                result.AngleBinsWorld[bin] += len;
            }
        }

        // Rectilinear under world axes: edges in 0-5° + 85-90° bins.
        if (result.PerimeterLength > 0)
        {
            result.RectilinearFractionWorld = (float)((result.AngleBinsWorld[0] + result.AngleBinsWorld[17])
                                                       / result.PerimeterLength);
        }

        // Best local rotation: scan θ in 1° steps over [0°, 90°), evaluate
        // rectilinear fraction under that rotation. Argmax wins. Coarse
        // 1° resolution is plenty for the "is the room a rotated rectangle"
        // question.
        float bestFrac = 0;
        float bestDeg = 0;
        for (int deg = 0; deg < 90; deg++)
        {
            double cardLen = 0;
            foreach (var (a, b) in result.Perimeter)
            {
                int bin = AngleBin(a, b, deg);
                if (bin == 0 || bin == 17)
                    cardLen += Dist(a, b);
            }
            float frac = (float)(cardLen / result.PerimeterLength);
            if (frac > bestFrac)
            {
                bestFrac = frac;
                bestDeg = deg;
            }
        }
        result.LocalBestRotationDeg = bestDeg;
        result.RectilinearFractionLocal = bestFrac;

        // Sparse rasterisation: bbox-independent. Some KOTOR rooms have
        // skybox/decoration vertices that stretch the AABB across kilometres
        // while the walkmesh itself is concentrated near the playable area.
        // A dense bool[w*h] would either bail or eat gigabytes. HashSet keyed
        // by packed (cx,cy) only allocates per actually-walkable cell.
        result.RasterArea025 = SparseRasterArea(result.Triangles, 0.25f);
        result.RasterArea050 = SparseRasterArea(result.Triangles, 0.50f);
        result.RasterArea100 = SparseRasterArea(result.Triangles, 1.00f);

        var cells = SparseRasterise(result.Triangles, cellSize);
        if (cells.Count > 0)
        {
            // Seed: centroid of largest walkable triangle. If that centroid
            // doesn't snap to a walkable cell (edge-of-triangle case) we
            // fall back to any walkable cell — same connected-region answer
            // unless the room is itself disconnected, which is rare.
            var largest = result.Triangles.OrderByDescending(TriArea).First();
            float cx = (largest.A.X + largest.B.X + largest.C.X) / 3f;
            float cy = (largest.A.Y + largest.B.Y + largest.C.Y) / 3f;
            int sx = (int)MathF.Floor(cx / cellSize);
            int sy = (int)MathF.Floor(cy / cellSize);
            long seedKey = PackCell(sx, sy);
            if (!cells.Contains(seedKey))
            {
                // Pick the first walkable cell — gives one connected
                // component's count, not necessarily the largest. Adequate
                // for the order-of-magnitude question this audit answers.
                seedKey = cells.First();
                UnpackCell(seedKey, out sx, out sy);
            }
            var reach4Set = SparseBfs(cells, sx, sy, diagonals: false);
            var reach8Set = SparseBfs(cells, sx, sy, diagonals: true);
            result.Reach4 = reach4Set.Count;
            result.Reach8 = reach8Set.Count;
            result.WalkableCellCount050 = cells.Count;

            // Smoothness pass: for each walkable cell, longest consecutive
            // walkable run in each of 8 directions. Cardinal vs diagonal max
            // separates "how far one key gets you" from "how far diagonal-key
            // would get you" — diff classifies smooth / fiddly / choke cells.
            ComputeSmoothness(cells, cellSize, result);

            // Diagonal-locked = cells the 8-way BFS reached but 4-card couldn't.
            // We exclude the unreachable disconnected island cells from this
            // set — those are cut off regardless of diagonals.
            var diagLocked = new HashSet<long>(reach8Set);
            diagLocked.ExceptWith(reach4Set);
            if (diagLocked.Count > 0)
            {
                foreach (var comp in ConnectedComponents8(diagLocked).OrderByDescending(c => c.Count))
                {
                    float clMinX = float.PositiveInfinity, clMinY = float.PositiveInfinity;
                    float clMaxX = float.NegativeInfinity, clMaxY = float.NegativeInfinity;
                    foreach (var key in comp)
                    {
                        UnpackCell(key, out int kx, out int ky);
                        float px = kx * cellSize, py = ky * cellSize;
                        if (px < clMinX) clMinX = px;
                        if (py < clMinY) clMinY = py;
                        if (px > clMaxX) clMaxX = px;
                        if (py > clMaxY) clMaxY = py;
                    }
                    result.DiagLockedClusters.Add(new ClusterStats(comp.Count, clMinX, clMinY, clMaxX + cellSize, clMaxY + cellSize));
                }
            }
        }

        return result;
    }

    private static void BumpEdge(Dictionary<(uint, uint), int> map, uint a, uint b)
    {
        var key = a < b ? (a, b) : (b, a);
        map[key] = map.TryGetValue(key, out var n) ? n + 1 : 1;
    }

    private static int AngleBin(V2 a, V2 b, float rotateDeg)
    {
        float dx = b.X - a.X, dy = b.Y - a.Y;
        if (rotateDeg != 0)
        {
            float r = rotateDeg * MathF.PI / 180f;
            float c = MathF.Cos(r), s = MathF.Sin(r);
            float nx = c * dx - s * dy;
            float ny = s * dx + c * dy;
            dx = nx; dy = ny;
        }
        float ang = MathF.Atan2(dy, dx);          // (-π, π]
        ang = MathF.Abs(ang);                      // [0, π]
        if (ang > MathF.PI / 2f) ang = MathF.PI - ang;  // fold to [0, π/2]
        float deg = ang * 180f / MathF.PI;
        int bin = (int)(deg / 5f);
        if (bin > 17) bin = 17;
        return bin;
    }

    private static float TriArea(Tri t)
    {
        return 0.5f * MathF.Abs((t.B.X - t.A.X) * (t.C.Y - t.A.Y)
                              - (t.C.X - t.A.X) * (t.B.Y - t.A.Y));
    }

    private static float MinInteriorAngleDeg(Tri t)
    {
        float a = Dist(t.B, t.C);
        float b = Dist(t.A, t.C);
        float c = Dist(t.A, t.B);
        if (a < 1e-6f || b < 1e-6f || c < 1e-6f) return 0f;
        float ang(float opp, float adj1, float adj2)
        {
            float cosX = (adj1 * adj1 + adj2 * adj2 - opp * opp) / (2 * adj1 * adj2);
            cosX = Math.Clamp(cosX, -1f, 1f);
            return MathF.Acos(cosX) * 180f / MathF.PI;
        }
        float A = ang(a, b, c);
        float B = ang(b, a, c);
        float C = ang(c, a, b);
        return MathF.Min(A, MathF.Min(B, C));
    }

    private static float Dist(V2 a, V2 b)
    {
        float dx = b.X - a.X, dy = b.Y - a.Y;
        return MathF.Sqrt(dx * dx + dy * dy);
    }

    private static bool PointInTri(float px, float py, Tri t)
    {
        // Barycentric sign test.
        float d1 = (px - t.B.X) * (t.A.Y - t.B.Y) - (t.A.X - t.B.X) * (py - t.B.Y);
        float d2 = (px - t.C.X) * (t.B.Y - t.C.Y) - (t.B.X - t.C.X) * (py - t.C.Y);
        float d3 = (px - t.A.X) * (t.C.Y - t.A.Y) - (t.C.X - t.A.X) * (py - t.A.Y);
        bool neg = d1 < 0 || d2 < 0 || d3 < 0;
        bool pos = d1 > 0 || d2 > 0 || d3 > 0;
        return !(neg && pos);
    }

    // --- Sparse rasterisation ---
    //
    // Cell coordinates are absolute world-grid indices (floor(world / cell)).
    // Packed into a long for HashSet keys: (cx << 32) | (uint)cy. Negative
    // cx/cy work because we cast to uint before OR (sign-extension would
    // collide otherwise).

    private static long PackCell(int cx, int cy)
        => ((long)cx << 32) | (uint)cy;

    private static void UnpackCell(long key, out int cx, out int cy)
    {
        cy = (int)(uint)(key & 0xFFFFFFFFL);
        cx = (int)(key >> 32);
    }

    private static float SparseRasterArea(List<Tri> tris, float cell)
    {
        var cells = SparseRasterise(tris, cell);
        return cells.Count * cell * cell;
    }

    private static HashSet<long> SparseRasterise(List<Tri> tris, float cell)
    {
        var cells = new HashSet<long>();
        foreach (var t in tris)
        {
            float tMinX = MathF.Min(t.A.X, MathF.Min(t.B.X, t.C.X));
            float tMaxX = MathF.Max(t.A.X, MathF.Max(t.B.X, t.C.X));
            float tMinY = MathF.Min(t.A.Y, MathF.Min(t.B.Y, t.C.Y));
            float tMaxY = MathF.Max(t.A.Y, MathF.Max(t.B.Y, t.C.Y));
            int cx0 = (int)MathF.Floor(tMinX / cell);
            int cx1 = (int)MathF.Floor(tMaxX / cell);
            int cy0 = (int)MathF.Floor(tMinY / cell);
            int cy1 = (int)MathF.Floor(tMaxY / cell);
            // Sanity bail — a triangle with bbox > ~10km is degenerate
            // (skybox sentinel), don't rasterise it.
            if ((long)(cx1 - cx0 + 1) * (cy1 - cy0 + 1) > 10_000_000) continue;
            for (int cy = cy0; cy <= cy1; cy++)
            {
                float py = (cy + 0.5f) * cell;
                for (int cx = cx0; cx <= cx1; cx++)
                {
                    float px = (cx + 0.5f) * cell;
                    if (PointInTri(px, py, t)) cells.Add(PackCell(cx, cy));
                }
            }
        }
        return cells;
    }

    private static HashSet<long> SparseBfs(HashSet<long> cells, int sx, int sy, bool diagonals)
    {
        var visited = new HashSet<long>();
        if (!cells.Contains(PackCell(sx, sy))) return visited;
        visited.Add(PackCell(sx, sy));
        var q = new Queue<(int x, int y)>();
        q.Enqueue((sx, sy));
        int[] dx = diagonals ? new[] { 1, -1, 0, 0, 1, 1, -1, -1 } : new[] { 1, -1, 0, 0 };
        int[] dy = diagonals ? new[] { 0, 0, 1, -1, 1, -1, 1, -1 } : new[] { 0, 0, 1, -1 };
        while (q.Count > 0)
        {
            var (x, y) = q.Dequeue();
            for (int k = 0; k < dx.Length; k++)
            {
                int nx = x + dx[k], ny = y + dy[k];
                long key = PackCell(nx, ny);
                if (!cells.Contains(key) || !visited.Add(key)) continue;
                q.Enqueue((nx, ny));
            }
        }
        return visited;
    }

    /// <summary>For each walkable cell, compute the longest consecutive
    /// walkable run starting at that cell in each of 8 directions, then
    /// classify by max-cardinal vs max-diagonal:
    ///   smooth  = max-cardinal ≥ 10 cells (≥ 5 m at 0.5 m grid): one
    ///             keypress carries you ≥ 5 m in some cardinal direction.
    ///   fiddly  = max-cardinal ≤ 4 cells AND max-diagonal ≥ 10 cells:
    ///             you're in a corridor where diagonal travel is long but
    ///             cardinal-only forces zigzag every couple of cells.
    ///   choke   = max-cardinal ≤ 2 cells AND max-diagonal ≤ 2 cells:
    ///             single-cell pinch, no smooth movement possible at all.
    ///
    /// Builds a dense bool grid sized to the walkable-cell bbox (not the
    /// world-vertex bbox, which can include skybox sentinels). DP per row
    /// then per column gives O(N) run-length in each cardinal direction;
    /// O(N) per diagonal axis. Bails on rooms whose walkable bbox exceeds
    /// 4M cells — none in KOTOR do at 0.5 m.</summary>
    /// <remarks>Caller-guarded: the sole call site is inside
    /// <c>if (cells.Count > 0)</c>. Do not call with an empty set — the bbox
    /// scan below would leave its min/max sentinels untouched.</remarks>
    private static void ComputeSmoothness(HashSet<long> cells, float cellSize, RoomResult result)
    {
        int minCx = int.MaxValue, minCy = int.MaxValue, maxCx = int.MinValue, maxCy = int.MinValue;
        foreach (var key in cells)
        {
            UnpackCell(key, out int cx, out int cy);
            if (cx < minCx) minCx = cx;
            if (cy < minCy) minCy = cy;
            if (cx > maxCx) maxCx = cx;
            if (cy > maxCy) maxCy = cy;
        }
        int w = maxCx - minCx + 1;
        int h = maxCy - minCy + 1;
        if ((long)w * h > 4_000_000) return;

        var grid = new bool[w * h];
        foreach (var key in cells)
        {
            UnpackCell(key, out int cx, out int cy);
            grid[(cy - minCy) * w + (cx - minCx)] = true;
        }

        // 8-direction run length, including the cell itself. Computed via
        // backwards DP along each direction's axis: run[c, dir] = walkable(c)
        // ? 1 + run[c + dir, dir] : 0.
        var runPosX = new int[w * h];
        var runNegX = new int[w * h];
        var runPosY = new int[w * h];
        var runNegY = new int[w * h];
        var runNE   = new int[w * h];
        var runNW   = new int[w * h];
        var runSE   = new int[w * h];
        var runSW   = new int[w * h];

        // +X (sweep right→left so we know the run from the right neighbour).
        for (int y = 0; y < h; y++)
        {
            for (int x = w - 1; x >= 0; x--)
            {
                int idx = y * w + x;
                if (!grid[idx]) continue;
                runPosX[idx] = 1 + (x + 1 < w ? runPosX[idx + 1] : 0);
            }
        }
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                int idx = y * w + x;
                if (!grid[idx]) continue;
                runNegX[idx] = 1 + (x - 1 >= 0 ? runNegX[idx - 1] : 0);
            }
        }
        for (int x = 0; x < w; x++)
        {
            for (int y = h - 1; y >= 0; y--)
            {
                int idx = y * w + x;
                if (!grid[idx]) continue;
                runPosY[idx] = 1 + (y + 1 < h ? runPosY[idx + w] : 0);
            }
        }
        for (int x = 0; x < w; x++)
        {
            for (int y = 0; y < h; y++)
            {
                int idx = y * w + x;
                if (!grid[idx]) continue;
                runNegY[idx] = 1 + (y - 1 >= 0 ? runNegY[idx - w] : 0);
            }
        }
        // NE = +x +y; sweep top-right corner backwards.
        for (int y = h - 1; y >= 0; y--)
        {
            for (int x = w - 1; x >= 0; x--)
            {
                int idx = y * w + x;
                if (!grid[idx]) continue;
                runNE[idx] = 1 + ((x + 1 < w && y + 1 < h) ? runNE[idx + w + 1] : 0);
            }
        }
        // NW = -x +y.
        for (int y = h - 1; y >= 0; y--)
        {
            for (int x = 0; x < w; x++)
            {
                int idx = y * w + x;
                if (!grid[idx]) continue;
                runNW[idx] = 1 + ((x - 1 >= 0 && y + 1 < h) ? runNW[idx + w - 1] : 0);
            }
        }
        // SE = +x -y.
        for (int y = 0; y < h; y++)
        {
            for (int x = w - 1; x >= 0; x--)
            {
                int idx = y * w + x;
                if (!grid[idx]) continue;
                runSE[idx] = 1 + ((x + 1 < w && y - 1 >= 0) ? runSE[idx - w + 1] : 0);
            }
        }
        // SW = -x -y.
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                int idx = y * w + x;
                if (!grid[idx]) continue;
                runSW[idx] = 1 + ((x - 1 >= 0 && y - 1 >= 0) ? runSW[idx - w - 1] : 0);
            }
        }

        // Thresholds in cells, derived from cell size. 5 m = 10 cells at
        // 0.5 m, 2 m = 4 cells, 1 m = 2 cells. Reused later for the
        // summary so make them mutually consistent.
        int smoothCardinalCells = (int)MathF.Round(5f / cellSize);   // ≥
        int fiddlyCardinalMaxCells = (int)MathF.Round(2f / cellSize); // ≤
        int fiddlyDiagonalMinCells = (int)MathF.Round(5f / cellSize); // ≥
        int chokeCells = (int)MathF.Round(1f / cellSize);             // ≤

        for (int i = 0; i < grid.Length; i++)
        {
            if (!grid[i]) continue;
            int maxC = Math.Max(Math.Max(runPosX[i], runNegX[i]), Math.Max(runPosY[i], runNegY[i]));
            int maxD = Math.Max(Math.Max(runNE[i], runNW[i]), Math.Max(runSE[i], runSW[i]));
            result.CardinalRunSumCells += maxC;
            result.DiagonalRunSumCells += maxD;
            result.SmoothnessSampleCells++;
            if (maxC >= smoothCardinalCells) result.SmoothCells++;
            if (maxC <= fiddlyCardinalMaxCells && maxD >= fiddlyDiagonalMinCells) result.FiddlyCells++;
            if (maxC <= chokeCells && maxD <= chokeCells) result.ChokeCells++;
        }
    }

    /// <summary>Connected-component labelling on a HashSet of cells using
    /// 8-connectivity. Returns each component's cells.</summary>
    private static List<HashSet<long>> ConnectedComponents8(HashSet<long> cells)
    {
        var result = new List<HashSet<long>>();
        var unvisited = new HashSet<long>(cells);
        int[] dx = { 1, -1, 0, 0, 1, 1, -1, -1 };
        int[] dy = { 0, 0, 1, -1, 1, -1, 1, -1 };
        while (unvisited.Count > 0)
        {
            long seed = unvisited.First();
            UnpackCell(seed, out int sx, out int sy);
            var comp = new HashSet<long> { seed };
            unvisited.Remove(seed);
            var q = new Queue<(int x, int y)>();
            q.Enqueue((sx, sy));
            while (q.Count > 0)
            {
                var (x, y) = q.Dequeue();
                for (int k = 0; k < 8; k++)
                {
                    int nx = x + dx[k], ny = y + dy[k];
                    long key = PackCell(nx, ny);
                    if (!unvisited.Contains(key)) continue;
                    unvisited.Remove(key);
                    comp.Add(key);
                    q.Enqueue((nx, ny));
                }
            }
            result.Add(comp);
        }
        return result;
    }

}
