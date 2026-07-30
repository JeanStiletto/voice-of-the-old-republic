using System.CommandLine;
using System.CommandLine.Invocation;

namespace Kdev.Commands;

/// <summary>
/// The daily-driver command. Chains clean → build → apply → launch --monitor,
/// failing fast with the failing step's exit code.
/// </summary>
public static class DevCommand
{
    public static Command Build()
    {
        var cmd = new Command("dev", "Daily-driver loop: clean, build, apply, launch --monitor.");
        cmd.SetHandler((InvocationContext context) => context.ExitCode = Run());
        return cmd;
    }

    private static int Run()
    {
        var steps = new (string Name, Func<int> Run)[]
        {
            ("clean", CleanCommand.Run),
            ("build", BuildCommand.Run),
            ("apply", ApplyCommand.Run),
            ("launch (monitored)", () => LaunchCommand.Run(monitor: true)),
        };

        for (var i = 0; i < steps.Length; i++)
        {
            var step = steps[i];
            Console.WriteLine();
            Console.WriteLine($"=== kdev dev: step {i + 1}/{steps.Length} - {step.Name} ===");
            Console.WriteLine();

            var rc = step.Run();
            if (rc != 0)
            {
                Console.Error.WriteLine();
                Console.Error.WriteLine($"=== kdev dev: aborted at step '{step.Name}' (exit {rc}) ===");
                return rc;
            }
        }

        Console.WriteLine();
        Console.WriteLine("=== kdev dev: all steps completed ===");
        return 0;
    }
}
