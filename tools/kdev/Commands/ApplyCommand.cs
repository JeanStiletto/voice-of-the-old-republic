using System.CommandLine;
using System.CommandLine.Invocation;
using KPatchCore.Applicators;
using KPatchCore.Managers;

namespace Kdev.Commands;

public static class ApplyCommand
{
    public static Command Build()
    {
        var cmd = new Command("apply", "Install the built .kpatch into the game via KPatchCore.");
        var gameOption = new Option<string>(
            name: "--game",
            description: "Which game to install into: 'k1' (default) or 'k2'. " +
                         "'k2' requires game.install_k2 in kdev.toml.",
            getDefaultValue: () => "k1");
        cmd.AddOption(gameOption);
        cmd.SetHandler((InvocationContext context) =>
            context.ExitCode = Run(context.ParseResult.GetValueForOption(gameOption)!));
        return cmd;
    }

    internal static int Run(string game = "k1")
    {
        var config = KdevConfig.LoadOrPrintErrors(out var exitCode);
        if (config is null) return exitCode;

        var target = config.ResolveTarget(game);
        if (target is null) return 2;

        var kpatchPath = Path.Combine(config.BuildOutput, "Accessibility.kpatch");
        if (!File.Exists(kpatchPath))
        {
            Console.Error.WriteLine($"ERROR: built artifact not found: {kpatchPath}");
            Console.Error.WriteLine("Run 'kdev build' first.");
            return 5;
        }

        var patcherDll = Path.Combine(config.PatchManagerRelease, "bin", "KotorPatcher.dll");
        if (!File.Exists(patcherDll))
        {
            Console.Error.WriteLine($"ERROR: KotorPatcher.dll not found at {patcherDll}");
            return 5;
        }

        // The applicator updates files inside the game directory; if the game is running
        // it will fight us for file handles. Stop it first.
        Console.WriteLine("Stopping any running game...");
        var killSummary = GameProcess.KillAll(target.ProcessName);
        if (killSummary.NothingToKill)
        {
            Console.WriteLine("  No running game.");
        }
        else if (!killSummary.AllKilled)
        {
            Console.Error.WriteLine("ERROR: Could not stop the running game; cannot proceed.");
            return 1;
        }

        // Stage the .kpatch into a kdev-managed patches directory, separate from any directory
        // the GUI launcher might be using. PatchRepository scans this dir for .kpatch files.
        var runtimePatchesDir = Path.Combine(config.BuildOutput, "patches");
        Directory.CreateDirectory(runtimePatchesDir);
        foreach (var stale in Directory.GetFiles(runtimePatchesDir, "*.kpatch"))
        {
            File.Delete(stale);
        }
        var stagedKpatch = Path.Combine(runtimePatchesDir, Path.GetFileName(kpatchPath));
        File.Copy(kpatchPath, stagedKpatch, overwrite: true);
        Console.WriteLine();
        Console.WriteLine($"Staged: {stagedKpatch}");

        // Resolve and stage additional bundled patches by ID. We scan the prebuilt
        // patcher's patches dir, look up each requested ID, and copy that .kpatch
        // alongside ours. PatchApplicator will install everything in one call.
        var idsToInstall = new List<string> { config.PatchId };
        if (config.AdditionalPatchIds.Count > 0)
        {
            var bundleDir = Path.Combine(config.PatchManagerRelease, "patches");
            if (!Directory.Exists(bundleDir))
            {
                Console.Error.WriteLine($"ERROR: bundle patches dir not found: {bundleDir}");
                return 5;
            }
            var bundle = new PatchRepository(bundleDir);
            var bundleScan = bundle.ScanPatches();
            if (!bundleScan.Success)
            {
                Console.Error.WriteLine($"ERROR: failed to scan bundled patches: {bundleScan.Error}");
                return 5;
            }
            foreach (var addId in config.AdditionalPatchIds)
            {
                var lookup = bundle.GetPatch(addId);
                if (!lookup.Success || lookup.Data is null)
                {
                    Console.Error.WriteLine(
                        $"ERROR: additional_patches references id '{addId}', " +
                        $"but no .kpatch with that id was found in {bundleDir}.");
                    return 5;
                }
                var src = lookup.Data.KPatchPath;
                var dst = Path.Combine(runtimePatchesDir, Path.GetFileName(src));
                File.Copy(src, dst, overwrite: true);
                Console.WriteLine($"Staged: {dst}  (id={addId})");
                idsToInstall.Add(addId);
            }
        }

        // Scan the staging dir so the applicator knows about all the patches we're installing.
        var repository = new PatchRepository(runtimePatchesDir);
        var scanResult = repository.ScanPatches();
        if (!scanResult.Success)
        {
            Console.Error.WriteLine($"ERROR: failed to scan patches dir: {scanResult.Error}");
            return 5;
        }

        Console.WriteLine();
        Console.WriteLine(
            $"Installing {idsToInstall.Count} patch(es) [{string.Join(", ", idsToInstall)}] " +
            $"to {target.Exe}...");
        var applicator = new PatchApplicator(repository);
        var result = applicator.InstallPatches(new PatchApplicator.InstallOptions
        {
            GameExePath = target.Exe,
            PatchIds = idsToInstall,
            PatcherDllPath = patcherDll,
            CreateBackup = true,
        });

        foreach (var msg in result.Messages)
        {
            Console.WriteLine($"  {msg}");
        }

        if (!result.Success)
        {
            Console.Error.WriteLine();
            Console.Error.WriteLine($"ERROR: {result.Error}");
            return 5;
        }

        Console.WriteLine();
        Console.WriteLine($"Installed {result.InstalledPatches.Count} patch(es).");
        if (result.DetectedVersion is not null)
        {
            Console.WriteLine($"Detected version: {result.DetectedVersion.DisplayName}");
        }

        // Copy bundled Prism runtime DLL alongside our patch DLL. Prism's
        // bridge DLLs (orca, speech_dispatcher) do LoadLibrary with bare
        // names; prism.cpp points SetDllDirectory at this dir so they
        // resolve against bundled neighbours instead of WindowsApps.
        var copyExit = CopyPrismRuntimeDll(config, target);
        if (copyExit != 0) return copyExit;

        // Drop the dinput8.dll proxy next to the game exe. Without it the
        // OS never loads KotorPatcher on launch — every detour hook stays
        // unapplied and `kdev launch` would silently run an unmodded game.
        // KOTOR 2 imports dinput8.dll too, so the same proxy works there.
        var loaderExit = CopyLoaderDll(config, target);
        if (loaderExit != 0) return loaderExit;

        return 0;
    }

    private static int CopyLoaderDll(KdevConfig config, KdevConfig.GameTarget target)
    {
        var srcDll = Path.Combine(config.ProjectRoot, "loader", "dinput8.dll");
        if (!File.Exists(srcDll))
        {
            Console.Error.WriteLine($"ERROR: loader DLL not found at {srcDll}");
            Console.Error.WriteLine("       Run 'kdev build' first to produce it.");
            return 5;
        }

        Console.WriteLine();
        Console.WriteLine("Copying dinput8.dll proxy loader...");
        var dst = Path.Combine(target.Install, "dinput8.dll");
        File.Copy(srcDll, dst, overwrite: true);
        Console.WriteLine("  ✓ dinput8.dll");
        return 0;
    }

    private static int CopyPrismRuntimeDll(KdevConfig config, KdevConfig.GameTarget target)
    {
        var prismSrcDir = Path.Combine(config.ProjectRoot, "third_party", "prism-dist", "x86");
        if (!Directory.Exists(prismSrcDir))
        {
            Console.Error.WriteLine($"ERROR: Prism dist dir not found: {prismSrcDir}");
            Console.Error.WriteLine("       Expected vendored binaries from upstream prism/x86.");
            return 5;
        }

        var prismDll = Path.Combine(prismSrcDir, "prism.dll");
        if (!File.Exists(prismDll))
        {
            Console.Error.WriteLine($"ERROR: prism.dll not found at {prismDll}");
            return 5;
        }

        var destDir = Path.Combine(target.Install, "patches");
        Directory.CreateDirectory(destDir);

        Console.WriteLine();
        Console.WriteLine("Copying Prism runtime DLL...");
        var dst = Path.Combine(destDir, "prism.dll");
        File.Copy(prismDll, dst, overwrite: true);
        Console.WriteLine("  ✓ prism.dll");

        // Tolk migration cleanup: remove stale Tolk runtime files from prior
        // installs. Safe — these are files this command itself used to copy.
        string[] staleTolkFiles = { "Tolk.dll", "nvdaControllerClient32.dll" };
        foreach (var stale in staleTolkFiles)
        {
            var stalePath = Path.Combine(destDir, stale);
            if (File.Exists(stalePath))
            {
                try
                {
                    File.Delete(stalePath);
                    Console.WriteLine($"  ✓ removed stale {stale}");
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine($"  WARN: could not remove stale {stale}: {ex.Message}");
                }
            }
        }

        return 0;
    }
}
