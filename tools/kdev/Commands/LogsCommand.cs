using System.CommandLine;
using System.CommandLine.Invocation;

namespace Kdev.Commands;

public static class LogsCommand
{
    public static Command Build()
    {
        var follow = new Option<bool>(
            name: "--follow",
            description: "Keep streaming as new lines are written. Ctrl+C to stop.");

        var cmd = new Command("logs", "Tail the most recent log under logs/.");
        cmd.AddOption(follow);
        cmd.SetHandler((InvocationContext context) =>
        {
            var followValue = context.ParseResult.GetValueForOption(follow);
            context.ExitCode = Run(followValue);
        });
        return cmd;
    }

    internal static int Run(bool follow)
    {
        var config = KdevConfig.LoadOrPrintErrors(out var exitCode);
        if (config is null) return exitCode;

        if (!Directory.Exists(config.LogsDir))
        {
            Console.WriteLine($"No logs directory yet: {config.LogsDir}");
            return 0;
        }

        var logFiles = Directory.GetFiles(config.LogsDir, "*.log");
        if (logFiles.Length == 0)
        {
            Console.WriteLine($"No log files in {config.LogsDir}.");
            return 0;
        }

        var newest = logFiles
            .Select(f => new FileInfo(f))
            .OrderByDescending(f => f.LastWriteTimeUtc)
            .First()
            .FullName;

        Console.WriteLine($"Tailing: {newest}");
        Console.WriteLine();

        // FileShare.ReadWrite so a concurrent writer (e.g. an active build) doesn't lock us out.
        using var stream = new FileStream(newest, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        using var reader = new StreamReader(stream);

        string? line;
        while ((line = reader.ReadLine()) is not null)
        {
            Console.WriteLine(line);
        }

        if (!follow) return 0;

        Console.WriteLine();
        Console.WriteLine("--- following (Ctrl+C to stop) ---");

        var stop = false;
        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            stop = true;
        };

        while (!stop)
        {
            line = reader.ReadLine();
            if (line is null)
            {
                Thread.Sleep(250);
            }
            else
            {
                Console.WriteLine(line);
            }
        }

        return 0;
    }
}
