using System.CommandLine;
using System.CommandLine.Invocation;
using System.IO;
using KotorAccessibilityInstaller;

namespace Kdev.Commands;

/// <summary>
/// Strip a minidump down to the memory our diagnostics use (swkotor.exe + our
/// DLLs, all thread stacks, crash-referenced heap, contexts, module list) and
/// drop stock system/driver module code+data. Validation harness for the same
/// MinidumpStripper the installer runs before bundling a crash dump.
/// </summary>
public static class StripDumpCommand
{
    public static Command Build()
    {
        var inArg = new Argument<string>("dump", "Path to the input .dmp file.");
        var outArg = new Argument<string>("out", "Path to write the stripped .dmp.");

        var cmd = new Command("strip-dump",
            "Strip a minidump to the memory our triage needs; report size before/after.");
        cmd.AddArgument(inArg);
        cmd.AddArgument(outArg);

        cmd.SetHandler((InvocationContext ctx) =>
        {
            string input = ctx.ParseResult.GetValueForArgument(inArg);
            string output = ctx.ParseResult.GetValueForArgument(outArg);
            if (!File.Exists(input))
            {
                System.Console.Error.WriteLine($"ERROR: dump not found: {input}");
                ctx.ExitCode = 2;
                return;
            }
            try
            {
                var stats = MinidumpStripper.StripFile(input, output);
                double Mb(long b) => b / (1024.0 * 1024.0);
                System.Console.WriteLine($"Input:    {Mb(stats.OriginalBytes):F1} MB");
                System.Console.WriteLine($"Output:   {Mb(stats.StrippedBytes):F1} MB  ({output})");
                System.Console.WriteLine($"Reduction: {100.0 * (1 - (double)stats.StrippedBytes / stats.OriginalBytes):F1}%");
                System.Console.WriteLine();
                System.Console.WriteLine($"Kept ranges:    {stats.KeptRanges,6}  ({Mb(stats.KeptMemoryBytes):F1} MB)");
                System.Console.WriteLine($"Dropped ranges: {stats.DroppedRanges,6}  ({Mb(stats.DroppedMemoryBytes):F1} MB)");
                ctx.ExitCode = 0;
            }
            catch (System.NotSupportedException ex)
            {
                System.Console.Error.WriteLine($"NOT STRIPPED: {ex.Message}");
                ctx.ExitCode = 3;
            }
        });

        return cmd;
    }
}
