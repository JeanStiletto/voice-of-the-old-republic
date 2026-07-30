using System.CommandLine;
using System.CommandLine.Invocation;
using System.IO;

using static Kdev.BwmFile;

namespace Kdev.Commands;

/// <summary>
/// Diagnostic — histogram face_type values in walkmeshes. Single file mode
/// for sanity-checking one .wok, or directory mode (--dir) for a
/// game-wide aggregation with human-readable surfacemat labels. Used to
/// answer questions like "what's the material distribution under the
/// player's feet" and "is there a path-vs-wild distinction baked into
/// the walkmesh".
/// </summary>
public static class WalkmeshFaceTypesCommand
{
    // surfacemat.2da row id → label. Source: build/2da-extracted/surfacemat.2da.
    private static readonly string[] MaterialNames =
    {
        "NotDefined",     //  0
        "Dirt",           //  1
        "Obscuring",      //  2
        "Grass",          //  3
        "Stone",          //  4
        "Wood",           //  5
        "Water",          //  6
        "Nonwalk",        //  7
        "Transparent",    //  8
        "Carpet",         //  9
        "Metal",          // 10
        "Puddles",        // 11
        "Swamp",          // 12
        "Mud",            // 13
        "Leaves",         // 14
        "Lava",           // 15
        "BottomlessPit",  // 16
        "DeepWater",      // 17
        "Door",           // 18
        "NonWalkGrass",   // 19
        "CRAP",           // 20
        "Trigger",        // 21
    };

    public static Command Build()
    {
        var pathArg = new Argument<string?>(name: "path", description: ".wok file path (single-file mode)")
        {
            Arity = ArgumentArity.ZeroOrOne,
        };
        var dirOpt = new Option<string?>(
            name: "--dir",
            description: "Aggregate face_type histogram across all m##*.wok in this directory.");
        var allOpt = new Option<bool>(
            name: "--all",
            description: "With --dir: include placeable/creature/door walkmeshes too (default: areas only).");
        var cmd = new Command("walkmesh-facetypes",
            "Histogram face_type values in a .wok or across a directory of walkmeshes.")
        {
            pathArg, dirOpt, allOpt,
        };
        cmd.SetHandler((InvocationContext ctx) =>
        {
            var p = ctx.ParseResult.GetValueForArgument(pathArg);
            var d = ctx.ParseResult.GetValueForOption(dirOpt);
            var all = ctx.ParseResult.GetValueForOption(allOpt);
            ctx.ExitCode = d != null ? RunDir(d, all) : Run(p ?? "");
        });
        return cmd;
    }

    private static int Run(string path)
    {
        if (string.IsNullOrEmpty(path))
        {
            Console.Error.WriteLine("Provide a .wok path or use --dir for directory aggregation.");
            return 1;
        }
        var bytes = File.ReadAllBytes(path);
        // Use BwmFile.HasValidHeader, which checks LENGTH before reading the
        // magic. The old inline check read bytes 0..8 unconditionally, so a
        // short or non-BWM file died with a raw ArgumentOutOfRangeException
        // instead of this command's own error - while the --dir path a few
        // dozen lines below has always had the length check.
        if (!HasValidHeader(bytes))
        { Console.Error.WriteLine("not BWM v1.0"); return 1; }
        uint type = ReadU32(bytes, OffType);
        if (type != 1) { Console.WriteLine($"type={type} (not walkmesh)"); return 0; }
        uint faceCount = ReadU32(bytes, OffFaceCount);
        uint faceTypeOff = ReadU32(bytes, OffFaceTypeOffset);

        var hist = new SortedDictionary<int, int>();
        for (int i = 0; i < faceCount; i++)
        {
            int t = (int)ReadU32(bytes, (int)faceTypeOff + i * 4);
            hist[t] = hist.TryGetValue(t, out var n) ? n + 1 : 1;
        }
        Console.WriteLine($"{Path.GetFileName(path)} faces={faceCount}");
        foreach (var kv in hist)
            Console.WriteLine($"  type {kv.Key,3} ({LabelFor(kv.Key)}): {kv.Value,4} faces");
        return 0;
    }

    private static int RunDir(string dir, bool includeAll)
    {
        if (!Directory.Exists(dir))
        {
            Console.Error.WriteLine($"Directory not found: {dir}");
            return 1;
        }
        var files = Directory.GetFiles(dir, "*.wok");
        var hist = new SortedDictionary<int, long>();
        int areaRoomFiles = 0;
        int faceTotal = 0;
        foreach (var path in files)
        {
            var name = Path.GetFileNameWithoutExtension(path);
            if (!includeAll)
            {
                if (name.Length < 3 || name[0] != 'm') continue;
                if (!char.IsDigit(name[1]) || !char.IsDigit(name[2])) continue;
            }
            areaRoomFiles++;
            byte[] bytes;
            try { bytes = File.ReadAllBytes(path); } catch { continue; }
            if (!HasValidHeader(bytes)) continue;
            uint t = ReadU32(bytes, OffType);
            if (t != 1) continue;
            uint faceCount = ReadU32(bytes, OffFaceCount);
            uint faceTypeOff = ReadU32(bytes, OffFaceTypeOffset);
            for (int i = 0; i < faceCount; i++)
            {
                int ty = (int)ReadU32(bytes, (int)faceTypeOff + i * 4);
                hist[ty] = hist.TryGetValue(ty, out var n) ? n + 1 : 1;
                faceTotal++;
            }
        }
        Console.WriteLine($"Scanned {areaRoomFiles} area walkmeshes; {faceTotal} total faces.");
        Console.WriteLine();
        Console.WriteLine($"{"id",3}  {"label",-15}  {"faces",10}  {"%",6}");
        foreach (var kv in hist.OrderByDescending(x => x.Value))
        {
            double pct = faceTotal > 0 ? kv.Value * 100.0 / faceTotal : 0;
            Console.WriteLine($"{kv.Key,3}  {LabelFor(kv.Key),-15}  {kv.Value,10}  {pct,5:F1}%");
        }
        return 0;
    }

    private static string LabelFor(int id) =>
        (id >= 0 && id < MaterialNames.Length) ? MaterialNames[id] : "?";

}
