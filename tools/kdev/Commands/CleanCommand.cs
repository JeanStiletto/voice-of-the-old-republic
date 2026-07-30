using System.CommandLine;
using System.CommandLine.Invocation;
using KPatchCore.Applicators;

namespace Kdev.Commands;

public static class CleanCommand
{
    public static Command Build()
    {
        var cmd = new Command("clean", "Kill running game and uninstall all patches from the game install.");
        cmd.SetHandler((InvocationContext context) => context.ExitCode = Run());
        return cmd;
    }

    internal static int Run()
    {
        var config = KdevConfig.LoadOrPrintErrors(out var exitCode);
        if (config is null) return exitCode;

        Console.WriteLine("Stopping any running game...");
        var killSummary = GameProcess.KillAll();
        if (killSummary.NothingToKill)
        {
            Console.WriteLine("  No running game.");
        }
        else if (!killSummary.AllKilled)
        {
            Console.Error.WriteLine("ERROR: Could not stop the running game; cannot proceed.");
            return 1;
        }

        Console.WriteLine();
        Console.WriteLine("Uninstalling patches...");
        var result = PatchRemover.RemoveAllPatches(config.GameExe);

        foreach (var msg in result.Messages)
        {
            Console.WriteLine($"  {msg}");
        }

        if (!result.Success)
        {
            Console.Error.WriteLine();
            Console.Error.WriteLine($"ERROR: {result.Error}");
            return 1;
        }

        Console.WriteLine();
        if (result.RemovedFiles.Count == 0)
        {
            Console.WriteLine("Nothing to clean - game install is already vanilla.");
        }
        else
        {
            Console.WriteLine(
                $"Cleaned. Removed {result.RemovedFiles.Count} file(s); backup restored: {result.BackupRestored}.");
        }

        return 0;
    }
}
