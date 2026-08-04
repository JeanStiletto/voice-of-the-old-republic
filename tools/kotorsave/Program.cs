using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using KotOR_IO;

// Read-only inspector for KOTOR savegames.
//
// Written to answer one question that keeps coming up in beta-test triage:
// "what state is this object actually in inside the player's save?" — the
// patch log can only show what the engine reports at runtime, which tells us a
// field is wrong but not how it got that way. The save carries the persisted
// truth.
//
// Save layout: <save dir>/SAVEGAME.sav (ERF)
//                -> <module>.sav (ERF)
//                   -> <area>.git (GFF)  <- live object state, incl. doors
// savenfo.res sits beside SAVEGAME.sav and holds the save's own metadata.
//
// Nothing here writes. Repairing a save is a separate job and should stay that
// way, so a diagnostic run can never damage the file we were sent.
internal static class Program
{
    private const string GitResourceType = "git";

    private static int Main(string[] args)
    {
        // Same reason kdev does this: on a German host the default culture
        // prints coordinates as "340,732", which reads as a thousands
        // separator when the output is pasted into a bug report.
        System.Globalization.CultureInfo.DefaultThreadCurrentCulture =
            System.Globalization.CultureInfo.InvariantCulture;

        if (args.Length == 0 || args[0] is "-h" or "--help")
        {
            Usage();
            return args.Length == 0 ? 2 : 0;
        }

        string savePath = args[0];
        string module = null;
        string tag = null;
        string listName = "Door List";

        for (int i = 1; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--module": module = Next(args, ref i); break;
                case "--tag": tag = Next(args, ref i); break;
                case "--list": listName = Next(args, ref i); break;
                default:
                    Console.Error.WriteLine($"unknown argument: {args[i]}");
                    Usage();
                    return 2;
            }
        }

        try
        {
            return Run(savePath, module, tag, listName);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"error: {ex.Message}");
            return 1;
        }
    }

    private static string Next(string[] args, ref int i)
    {
        if (i + 1 >= args.Length) throw new Exception($"{args[i]} needs a value");
        return args[++i];
    }

    private static void Usage()
    {
        Console.WriteLine("kotorsave — read-only KOTOR savegame inspector");
        Console.WriteLine();
        Console.WriteLine("  kotorsave <save>");
        Console.WriteLine("      Print the save's metadata and the modules it carries state for.");
        Console.WriteLine();
        Console.WriteLine("  kotorsave <save> --module <resref> [--list <GFF list>]");
        Console.WriteLine("      Summarise every object in that module's list (default \"Door List\").");
        Console.WriteLine();
        Console.WriteLine("  kotorsave <save> --module <resref> --tag <tag>");
        Console.WriteLine("      Dump every field of the matching object, nested lists expanded.");
        Console.WriteLine();
        Console.WriteLine("<save> is either the save directory or the SAVEGAME.sav inside it.");
        Console.WriteLine();
        Console.WriteLine("Example — the Upper Shadowlands force field:");
        Console.WriteLine("  kotorsave \"...\\saves\\000036 - Game35\" --module kas_m24aa --tag kas24_forcefield");
    }

    private static int Run(string savePath, string module, string tag, string listName)
    {
        string saveFile = ResolveSaveFile(savePath, out string saveDir);
        Console.WriteLine($"Save file: {saveFile}");
        PrintSaveInfo(saveDir);
        Console.WriteLine();

        var outer = new ERF(saveFile);

        if (module == null)
        {
            // Per-module state is stored as a nested ERF. Its resource type is
            // SAV (2057), which Res_Types renders as "mod" — match on the type
            // code via ERFResTypes rather than on that misleading string.
            var modules = outer.Key_List
                .Where(k => Reference_Tables.ERFResTypes.Contains(k.ResType))
                .Select(k => k.ResRef)
                .OrderBy(r => r, StringComparer.OrdinalIgnoreCase)
                .ToList();
            Console.WriteLine($"Modules with saved state: {modules.Count}");
            foreach (string m in modules) Console.WriteLine($"  {m}");
            Console.WriteLine();
            Console.WriteLine("Re-run with --module <resref> to inspect one.");
            return 0;
        }

        var moduleKey = outer.Key_List.FirstOrDefault(
            k => string.Equals(k.ResRef, module, StringComparison.OrdinalIgnoreCase));
        if (moduleKey == null)
        {
            Console.Error.WriteLine($"error: this save carries no state for module '{module}'.");
            Console.Error.WriteLine("Run without --module to list the modules it does have.");
            return 1;
        }

        var inner = new ERF(outer.Resource_List[moduleKey.ResID].Resource_data);
        var gitKey = inner.Key_List.FirstOrDefault(
            k => string.Equals(k.Type_string, GitResourceType, StringComparison.OrdinalIgnoreCase));
        if (gitKey == null)
            throw new Exception($"module '{module}' contains no .git resource");

        Console.WriteLine($"Module: {moduleKey.ResRef}  ->  {gitKey.ResRef}.git");

        var git = new GFF(inner.Resource_List[gitKey.ResID].Resource_data);
        var list = git.Top_Level.Fields.FirstOrDefault(f => f.Label == listName) as GFF.LIST;
        if (list == null)
        {
            Console.Error.WriteLine($"error: no list named \"{listName}\" in this module's .git.");
            Console.Error.WriteLine("Lists present: " + string.Join(", ",
                git.Top_Level.Fields.OfType<GFF.LIST>().Select(l => $"\"{l.Label}\"")));
            return 1;
        }

        var matches = list.Structs
            .Where(s => tag == null || string.Equals(TagOf(s), tag, StringComparison.OrdinalIgnoreCase))
            .ToList();

        if (tag != null && matches.Count == 0)
        {
            Console.Error.WriteLine($"error: no entry in \"{listName}\" has tag '{tag}'.");
            Console.Error.WriteLine("Tags present: " + string.Join(", ",
                list.Structs.Select(TagOf).Where(t => t != null)));
            return 1;
        }

        Console.WriteLine($"\"{listName}\": {list.Structs.Count} entries"
            + (tag != null ? $", {matches.Count} matching tag '{tag}'" : ""));

        for (int i = 0; i < matches.Count; i++)
        {
            Console.WriteLine();
            Console.WriteLine($"Entry {i + 1} of {matches.Count}");
            if (tag != null) DumpAll(matches[i], "  ");
            else Summarise(matches[i], "  ");

            string verdict = DoorVerdict(matches[i]);
            if (verdict != null)
            {
                Console.WriteLine($"  Verdict:        {verdict}");
            }
        }

        return 0;
    }

    private static string ResolveSaveFile(string path, out string saveDir)
    {
        if (Directory.Exists(path))
        {
            saveDir = path;
            string f = Path.Combine(path, "SAVEGAME.sav");
            if (!File.Exists(f)) throw new Exception($"no SAVEGAME.sav in {path}");
            return f;
        }
        if (!File.Exists(path)) throw new Exception($"no such save: {path}");
        saveDir = Path.GetDirectoryName(Path.GetFullPath(path));
        return path;
    }

    // savenfo.res is a plain GFF holding the strings the load menu shows. Worth
    // echoing so a log pasted into a bug report identifies which save it is.
    private static void PrintSaveInfo(string saveDir)
    {
        string nfo = Path.Combine(saveDir ?? ".", "savenfo.res");
        if (!File.Exists(nfo)) return;

        GFF gff;
        try { gff = new GFF(File.ReadAllBytes(nfo)); }
        catch (Exception ex) { Console.WriteLine($"  (savenfo.res unreadable: {ex.Message})"); return; }

        var top = gff.Top_Level;
        Console.WriteLine($"  Name:        {Str(top, "SAVEGAMENAME")}");
        Console.WriteLine($"  Area:        {Str(top, "AREANAME")}");
        Console.WriteLine($"  Last module: {Str(top, "LASTMODULE")}");

        if (top.Fields.FirstOrDefault(f => f.Label == "TIMEPLAYED") is GFF.DWORD t)
        {
            uint secs = t.Value;
            Console.WriteLine($"  Time played: {secs / 3600}h {secs % 3600 / 60}m");
        }
    }

    private static string Str(GFF.STRUCT s, string label)
    {
        var f = s.Fields.FirstOrDefault(x => x.Label == label);
        return f is GFF.CExoString cs ? cs.CEString : "(none)";
    }

    private static string TagOf(GFF.STRUCT s)
    {
        return s.Fields.FirstOrDefault(f => f.Label == "Tag") is GFF.CExoString cs ? cs.CEString : null;
    }

    // The fields that decide whether the engine will offer an action on a door
    // and whether its OnFailToOpen can still fire, plus enough identity to be
    // sure we are looking at the right object.
    private static readonly string[] SummaryFields =
    {
        "Tag", "ObjectId", "LocName", "Locked", "OpenState", "CurrentHP", "HP",
        "Plot", "Min1HP", "Static", "Faction", "KeyRequired", "KeyName",
        "OpenLockDC", "Conversation", "OnFailToOpen", "OnOpen", "OnDeath",
        "X", "Y", "Z",
    };

    private static void Summarise(GFF.STRUCT s, string indent)
    {
        foreach (string label in SummaryFields)
        {
            var f = s.Fields.FirstOrDefault(x => x.Label == label);
            if (f == null) continue;
            Console.WriteLine($"{indent}{label,-15} {Value(f)}");
        }
        foreach (var l in s.Fields.OfType<GFF.LIST>())
        {
            Console.WriteLine($"{indent}{l.Label,-15} {l.Structs.Count} entries");
        }
    }

    private static void DumpAll(GFF.STRUCT s, string indent)
    {
        foreach (var f in s.Fields)
        {
            switch (f)
            {
                case GFF.LIST l:
                    Console.WriteLine($"{indent}{l.Label} ({l.Structs.Count} entries)");
                    for (int i = 0; i < l.Structs.Count; i++)
                    {
                        Console.WriteLine($"{indent}  [{i}]");
                        DumpAll(l.Structs[i], indent + "    ");
                    }
                    break;
                case GFF.STRUCT st:
                    Console.WriteLine($"{indent}{st.Label} (struct)");
                    DumpAll(st, indent + "  ");
                    break;
                default:
                    Console.WriteLine($"{indent}{f.Label,-18} {Value(f)}");
                    break;
            }
        }
    }

    private static string Value(GFF.FIELD f)
    {
        switch (f)
        {
            case GFF.BYTE v: return v.Value.ToString();
            case GFF.CHAR v: return v.Value.ToString();
            case GFF.WORD v: return v.Value.ToString();
            case GFF.SHORT v: return v.Value.ToString();
            case GFF.DWORD v: return v.Value.ToString();
            case GFF.INT v: return v.Value.ToString();
            case GFF.DWORD64 v: return v.Value.ToString();
            case GFF.INT64 v: return v.Value.ToString();
            case GFF.FLOAT v: return v.Value.ToString("0.####");
            case GFF.DOUBLE v: return v.Value.ToString("0.####");
            case GFF.CExoString v: return v.CEString.Length == 0 ? "(empty)" : v.CEString;
            case GFF.ResRef v: return string.IsNullOrEmpty(v.Reference) ? "(empty)" : v.Reference;
            case GFF.CExoLocString v:
                return v.StringRef == -1 || v.StringRef == unchecked((int)0xFFFFFFFF)
                    ? "(no strref)"
                    : $"strref {v.StringRef}";
            case GFF.VOID v: return $"{v.Data.Count} raw bytes";
            case GFF.Vector v: return $"({v.X:0.##}, {v.Y:0.##}, {v.Z:0.##})";
            case GFF.Orientation v: return $"({v.Value1:0.##}, {v.Value2:0.##}, {v.Value3:0.##}, {v.Value4:0.##})";
            case GFF.StrRef v: return $"strref {v.Reference}";
            default: return f.ToString();
        }
    }

    // Plain-language read of the two flags that gate a door's interaction, so
    // the answer does not depend on remembering which way round they run.
    // Only meaningful for Door List entries; returns null for anything else.
    private static string DoorVerdict(GFF.STRUCT s)
    {
        var lockedField = s.Fields.FirstOrDefault(f => f.Label == "Locked") as GFF.BYTE;
        var openField = s.Fields.FirstOrDefault(f => f.Label == "OpenState") as GFF.BYTE;
        if (lockedField == null || openField == null) return null;

        bool locked = lockedField.Value != 0;
        bool open = openField.Value != 0;

        var hp = s.Fields.FirstOrDefault(f => f.Label == "CurrentHP") as GFF.SHORT;
        if (hp != null && hp.Value <= 0)
            return "destroyed (CurrentHP <= 0) — final state, no actions offered";

        if (locked && !open)
            return "closed and locked — engine offers Open, and OnFailToOpen fires on the attempt";
        if (locked)
            return "locked but flagged open — inconsistent, worth reporting";
        if (open)
            return "unlocked and open — engine offers no Open action, so OnFailToOpen can never fire";
        return "closed and unlocked — engine offers Open, and it will succeed (OnFailToOpen will NOT fire)";
    }
}
