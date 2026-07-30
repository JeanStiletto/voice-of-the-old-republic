using System.CommandLine;
using System.CommandLine.Invocation;

namespace Kdev.Commands;

public static class KillCommand
{
    public static Command Build()
    {
        var cmd = new Command("kill", "Terminate any running swkotor.exe processes.");
        cmd.SetHandler((InvocationContext context) => context.ExitCode = Run());
        return cmd;
    }

    internal static int Run()
    {
        var summary = GameProcess.KillAll();
        if (summary.NothingToKill)
        {
            Console.WriteLine($"No running {GameProcess.ExeName} processes found.");
            return 0;
        }

        Console.WriteLine();
        Console.WriteLine($"Found {summary.FoundCount}; killed {summary.KilledCount}; failed {summary.FailedCount}.");
        return summary.AllKilled ? 0 : 1;
    }
}
