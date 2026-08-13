using System.CommandLine;
using System.CommandLine.Invocation;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Kdev.Commands;

public static class BuildCommand
{
    // Compile/link flags MUST mirror create-patch.bat so the incremental DLL is
    // equivalent to the legacy full bat build. (bat: /LD /O2 /MD /W3 /EHsc
    // /std:c++17, link /DEF:exports.def /LIBPATH lib sqlite3.lib.)
    //
    // Deliberate divergence from the bat: /Z7 + /DEBUG produce accessibility.pdb
    // so a crash dump can name our functions instead of a bare module+offset.
    // Debug info does not change codegen, so the DLL stays equivalent — it only
    // gains a debug directory pointing at the PDB. The bat path has no symbols.
    //
    // /Z7 rather than /Zi is forced by this build's shape: /Zi has every cl
    // process write a shared vc140.pdb in its working directory, and we compile
    // ~110 TUs in parallel with the working directory set to the SOURCE folder —
    // so /Zi would both serialise on that file and litter patches/Accessibility/.
    // /Z7 embeds the debug info in each .obj instead, which the object cache
    // then carries for free and the linker collects at link time.
    private const string CompileFlags = "/nologo /c /O2 /MD /W3 /EHsc /std:c++17 /Z7";
    private const string LinkFlags    = "/nologo /LD /MD";

    public static Command Build()
    {
        var clean = new Option<bool>(
            name: "--clean",
            description: "Wipe the object cache and recompile every translation unit.");
        var viaBat = new Option<bool>(
            name: "--bat",
            description: "Use the legacy create-patch.bat full-rebuild path instead of the incremental compiler.");

        var cmd = new Command("build",
            "Compile the patch incrementally (cached objects, per-TU header deps) and package the .kpatch.");
        cmd.AddOption(clean);
        cmd.AddOption(viaBat);
        cmd.SetHandler((InvocationContext context) =>
        {
            var doBat = context.ParseResult.GetValueForOption(viaBat);
            var doClean = context.ParseResult.GetValueForOption(clean);
            context.ExitCode = doBat ? RunViaBat() : RunIncremental(doClean);
        });
        return cmd;
    }

    // Default entry point used by `kdev dev` and any other internal caller:
    // the incremental build without a forced clean.
    internal static int Run() => RunIncremental(clean: false);

    // ====================================================================
    //  Incremental build (default)
    // ====================================================================

    private readonly record struct CompileUnit(string Src, string Obj, string Deps);

    internal static int RunIncremental(bool clean)
    {
        var config = KdevConfig.LoadOrPrintErrors(out var exitCode);
        if (config is null) return exitCode;

        var patchDir = Path.GetFullPath(config.AccessibilitySource);
        var manifest = Path.Combine(patchDir, "manifest.toml");
        if (!File.Exists(manifest))
        {
            Console.Error.WriteLine($"ERROR: manifest.toml not found at {manifest}");
            return 4;
        }
        var hooksFiles = Directory.GetFiles(patchDir, "*hooks.toml");
        if (hooksFiles.Length == 0)
        {
            Console.Error.WriteLine($"ERROR: No *hooks.toml files found in {patchDir}");
            return 4;
        }
        var exportsDef = Path.Combine(patchDir, "exports.def");
        if (!File.Exists(exportsDef))
        {
            Console.Error.WriteLine($"ERROR: exports.def not found at {exportsDef}.");
            Console.Error.WriteLine("The incremental builder requires a checked-in exports.def. Run 'kdev build --bat' to auto-generate one.");
            return 4;
        }

        var upstream   = Path.GetFullPath(config.UpstreamClone);
        var commonDir  = Path.Combine(upstream, "Patches", "Common");
        var gameApiDir = Path.Combine(commonDir, "GameAPI");
        var libDir     = Path.Combine(upstream, "lib");

        // Resolve the MSVC toolchain once and capture its environment, so each
        // cl.exe invocation runs with INCLUDE/LIB/PATH set without paying the
        // vcvars cost per file.
        var vcvars = FindVcvars32();
        if (vcvars is null)
        {
            Console.Error.WriteLine("ERROR: Visual Studio C++ (vcvars32.bat) not found. Install VS Build Tools, or use 'kdev build --bat'.");
            return 4;
        }
        var env = CaptureEnv(vcvars);
        if (env is null)
        {
            Console.Error.WriteLine("ERROR: failed to capture the MSVC environment from vcvars32.bat.");
            return 4;
        }
        // Force English diagnostics so /showIncludes parsing + logs stay stable.
        env["VSLANG"] = "1033";
        var clExe = FindOnPath(env, "cl.exe");
        if (clExe is null)
        {
            Console.Error.WriteLine("ERROR: cl.exe not found on the MSVC PATH.");
            return 4;
        }

        var objCache = Path.Combine(config.BuildOutput, "objcache");
        var includeArgs = $"/I\"{patchDir}\" /I\"{commonDir}\" /I\"{libDir}\"";

        // Invalidate the whole cache if the toolchain or flags changed.
        var signature = string.Join("|", clExe, CompileFlags, includeArgs);
        var sigFile = Path.Combine(objCache, "build.sig");
        if (clean || !File.Exists(sigFile) || File.ReadAllText(sigFile) != signature)
        {
            if (Directory.Exists(objCache)) Directory.Delete(objCache, recursive: true);
        }
        Directory.CreateDirectory(objCache);
        File.WriteAllText(sigFile, signature);

        // Gather sources, grouped so obj basenames stay unique per origin dir.
        var units = new List<CompileUnit>();
        foreach (var (group, dir) in new[]
                 {
                     ("patch", patchDir),
                     ("common", commonDir),
                     ("gameapi", gameApiDir),
                 })
        {
            if (!Directory.Exists(dir)) continue;
            var objSub = Path.Combine(objCache, group);
            Directory.CreateDirectory(objSub);
            foreach (var cpp in Directory.GetFiles(dir, "*.cpp"))
            {
                var obj = Path.Combine(objSub, Path.GetFileNameWithoutExtension(cpp) + ".obj");
                units.Add(new CompileUnit(cpp, obj, obj + ".deps"));
            }
        }
        if (units.Count == 0)
        {
            Console.Error.WriteLine("ERROR: no .cpp source files found to compile.");
            return 4;
        }

        var toCompile = units.Where(NeedsRecompile).ToList();
        Console.WriteLine(
            $"Sources: {units.Count}   up-to-date: {units.Count - toCompile.Count}   to compile: {toCompile.Count}");

        if (toCompile.Count > 0)
        {
            var sw = Stopwatch.StartNew();
            var failures = CompileChanged(toCompile, clExe, env, includeArgs, upstream, config.ProjectRoot);
            sw.Stop();
            if (failures > 0)
            {
                Console.Error.WriteLine($"ERROR: {failures} translation unit(s) failed to compile.");
                return 4;
            }
            Console.WriteLine($"Compiled {toCompile.Count} file(s) in {sw.Elapsed.TotalSeconds:F1}s.");
        }

        // Link all current objects into the DLL.
        var dll = Path.Combine(objCache, "windows_x86.dll");
        if (File.Exists(dll)) File.Delete(dll);
        Console.WriteLine("Linking windows_x86.dll...");
        var objList = string.Join(" ", units.Select(u => $"\"{u.Obj}\""));
        // Named accessibility.pdb, not windows_x86.pdb: the DLL is installed as
        // patches/accessibility.dll, and a debugger looks for symbols under the
        // loaded module's name. The path is recorded inside the DLL, so the
        // matching PDB is found automatically when it sits alongside.
        var pdb = Path.Combine(objCache, "accessibility.pdb");
        // /OPT:REF,ICF are not optional extras here: /DEBUG flips the linker's
        // defaults to /OPT:NOREF,NOICF, so adding symbols alone kept every
        // unreferenced function and stopped identical-COMDAT folding — the DLL
        // went from ~2.6 MB to ~3.5 MB purely as a side effect. Restating them
        // puts the release-style link back; symbols are unaffected.
        var linkArgs =
            $"{LinkFlags} {objList} /Fe\"{dll}\" /link /DEF:\"{exportsDef}\" /LIBPATH:\"{libDir}\" sqlite3.lib " +
            $"/DEBUG /PDB:\"{pdb}\" /OPT:REF /OPT:ICF";
        var (linkExit, linkOut, linkErr) = RunCl(clExe, env, linkArgs, objCache);
        if (linkExit != 0 || !File.Exists(dll))
        {
            Console.Error.WriteLine(FilterNotes(linkOut, config.ProjectRoot));
            Console.Error.Write(linkErr);
            Console.Error.WriteLine("ERROR: link step failed.");
            return 4;
        }

        // Publish the PDB next to the .kpatch at a stable path. It is
        // deliberately NOT inside the .kpatch: users never need it and it would
        // multiply the download size. release.ps1 archives this copy per
        // version so an old tester's crash can still be symbolised.
        Directory.CreateDirectory(config.BuildOutput);
        if (File.Exists(pdb))
        {
            var publishedPdb = Path.Combine(config.BuildOutput, "accessibility.pdb");
            File.Copy(pdb, publishedPdb, overwrite: true);
            Console.WriteLine($"Symbols: {publishedPdb}");
        }
        else
        {
            Console.WriteLine("WARNING: link produced no PDB; crash dumps from this build cannot be symbolised.");
        }

        // Package the .kpatch (manifest + hooks + binaries/windows_x86.dll).
        var kpatch = Path.Combine(config.BuildOutput, "Accessibility.kpatch");
        if (File.Exists(kpatch)) File.Delete(kpatch);
        using (var zip = ZipFile.Open(kpatch, ZipArchiveMode.Create))
        {
            zip.CreateEntryFromFile(manifest, "manifest.toml");
            foreach (var h in hooksFiles)
                zip.CreateEntryFromFile(h, Path.GetFileName(h));
            zip.CreateEntryFromFile(dll, "binaries/windows_x86.dll");
        }
        Console.WriteLine($"Built: {kpatch}");

        // Build the dinput8.dll proxy loader (unchanged from the bat path).
        var loaderExit = BuildLoader(config);
        if (loaderExit != 0) return loaderExit;

        return 0;
    }

    // Recompile a TU if its obj is missing/stale, the source changed, the deps
    // sidecar is missing, or any tracked header is newer than the obj.
    private static bool NeedsRecompile(CompileUnit u)
    {
        if (!File.Exists(u.Obj)) return true;
        var objTime = File.GetLastWriteTimeUtc(u.Obj);
        if (File.GetLastWriteTimeUtc(u.Src) > objTime) return true;
        if (!File.Exists(u.Deps)) return true;
        foreach (var dep in File.ReadLines(u.Deps))
        {
            if (dep.Length == 0) continue;
            if (!File.Exists(dep)) return true;                   // header moved/deleted
            if (File.GetLastWriteTimeUtc(dep) > objTime) return true;
        }
        return false;
    }

    private static int CompileChanged(
        List<CompileUnit> toCompile, string clExe, IDictionary<string, string> env,
        string includeArgs, string upstream, string projectRoot)
    {
        var maxParallel = Math.Max(1, Environment.ProcessorCount);
        using var sem = new SemaphoreSlim(maxParallel);
        var consoleLock = new object();
        int failures = 0;

        var tasks = toCompile.Select(u => Task.Run(() =>
        {
            sem.Wait();
            try
            {
                var args =
                    $"{CompileFlags} /showIncludes {includeArgs} \"{u.Src}\" /Fo\"{u.Obj}\"";
                var (exit, stdout, stderr) = RunCl(clExe, env, args, Path.GetDirectoryName(u.Src)!);
                if (exit == 0)
                {
                    WriteDeps(u, stdout, projectRoot, upstream);
                    lock (consoleLock) Console.WriteLine($"  cc {Path.GetFileName(u.Src)}");
                }
                else
                {
                    Interlocked.Increment(ref failures);
                    lock (consoleLock)
                    {
                        Console.Error.WriteLine($"  FAILED {Path.GetFileName(u.Src)}");
                        Console.Error.Write(FilterNotes(stdout, projectRoot));
                        Console.Error.Write(stderr);
                    }
                }
            }
            finally { sem.Release(); }
        })).ToArray();

        Task.WaitAll(tasks);
        return failures;
    }

    // Parse cl /showIncludes output into a deps sidecar. Locale-independent:
    // the /showIncludes prefix is localized ("Note: including file:" /
    // "Hinweis: Einlesen der Datei:" / ...), so instead of matching it we look
    // for the repo-root path that every tracked header line ends with. Only
    // project + upstream headers (both live under the repo root) are recorded;
    // system/SDK headers don't change between builds, so excluding them keeps
    // the sidecar small and the staleness check fast.
    private static void WriteDeps(CompileUnit u, string clStdout, string projectRoot, string upstream)
    {
        var root = Path.GetFullPath(projectRoot);
        var deps = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var raw in clStdout.Split('\n'))
        {
            var line = raw.TrimEnd('\r');
            var i = line.IndexOf(root, StringComparison.OrdinalIgnoreCase);
            if (i < 0) continue;                       // not a project/upstream header
            var path = line.Substring(i).Trim();       // /showIncludes puts the path at line end
            // cl writes warnings/errors to stdout too (e.g. "...foo.cpp(309): warning C4996"),
            // and those contain the repo root — keep only candidates that are real files so a
            // diagnostic line can't masquerade as a (missing) dependency and force a perpetual rebuild.
            if (path.Length > 0 && File.Exists(path)) deps.Add(path);
        }
        File.WriteAllLines(u.Deps, deps);
    }

    // Declutter a captured compile failure by dropping /showIncludes note lines
    // (any locale). A note line carries a header path under the repo root and
    // is NOT a diagnostic; error/warning lines also carry the source path, so
    // they're kept by the marker check.
    private static string FilterNotes(string s, string projectRoot)
    {
        var root = Path.GetFullPath(projectRoot);
        var sb = new StringBuilder();
        foreach (var line in s.Split('\n'))
        {
            var l = line.TrimEnd('\r');
            bool isNote = l.IndexOf(root, StringComparison.OrdinalIgnoreCase) >= 0
                          && !l.Contains("error") && !l.Contains("warning")
                          && !l.Contains("Fehler") && !l.Contains("Warnung");
            if (isNote) continue;
            sb.AppendLine(l);
        }
        return sb.ToString();
    }

    private static (int exit, string stdout, string stderr) RunCl(
        string exe, IDictionary<string, string> env, string args, string workingDir)
    {
        var psi = new ProcessStartInfo
        {
            FileName = exe,
            Arguments = args,
            WorkingDirectory = workingDir,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        psi.Environment.Clear();
        foreach (var kv in env) psi.Environment[kv.Key] = kv.Value;

        var sbOut = new StringBuilder();
        var sbErr = new StringBuilder();
        using var proc = Process.Start(psi)
            ?? throw new InvalidOperationException($"Failed to start {exe}");
        proc.OutputDataReceived += (_, e) => { if (e.Data is not null) sbOut.AppendLine(e.Data); };
        proc.ErrorDataReceived  += (_, e) => { if (e.Data is not null) sbErr.AppendLine(e.Data); };
        proc.BeginOutputReadLine();
        proc.BeginErrorReadLine();
        proc.WaitForExit();
        return (proc.ExitCode, sbOut.ToString(), sbErr.ToString());
    }

    private static string? FindVcvars32()
    {
        var pf86 = Environment.GetEnvironmentVariable("ProgramFiles(x86)")
                   ?? @"C:\Program Files (x86)";
        var vswhere = Path.Combine(pf86, "Microsoft Visual Studio", "Installer", "vswhere.exe");
        if (File.Exists(vswhere))
        {
            var psi = new ProcessStartInfo
            {
                FileName = vswhere,
                Arguments = "-latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath",
                RedirectStandardOutput = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            using var proc = Process.Start(psi);
            if (proc is not null)
            {
                var outp = proc.StandardOutput.ReadToEnd();
                proc.WaitForExit();
                var vsPath = outp.Split('\n').Select(l => l.Trim()).FirstOrDefault(l => l.Length > 0);
                if (!string.IsNullOrEmpty(vsPath))
                {
                    var candidate = Path.Combine(vsPath, "VC", "Auxiliary", "Build", "vcvars32.bat");
                    if (File.Exists(candidate)) return candidate;
                }
            }
        }
        var fallback = @"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat";
        return File.Exists(fallback) ? fallback : null;
    }

    private static IDictionary<string, string>? CaptureEnv(string vcvars)
    {
        var psi = new ProcessStartInfo
        {
            FileName = "cmd.exe",
            Arguments = $"/c \"\"{vcvars}\" >nul 2>&1 && set\"",
            RedirectStandardOutput = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        using var proc = Process.Start(psi);
        if (proc is null) return null;
        var outp = proc.StandardOutput.ReadToEnd();
        proc.WaitForExit();
        if (proc.ExitCode != 0) return null;

        var env = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var line in outp.Split('\n'))
        {
            var eq = line.IndexOf('=');
            if (eq <= 0) continue;
            var name = line.Substring(0, eq).Trim();
            var value = line.Substring(eq + 1).TrimEnd('\r', '\n');
            if (name.Length > 0) env[name] = value;
        }
        return env.Count > 0 ? env : null;
    }

    private static string? FindOnPath(IDictionary<string, string> env, string exeName)
    {
        if (!env.TryGetValue("Path", out var path) && !env.TryGetValue("PATH", out path))
            return null;
        foreach (var dir in path.Split(';'))
        {
            if (dir.Length == 0) continue;
            string candidate;
            try { candidate = Path.Combine(dir.Trim(), exeName); }
            catch { continue; }
            if (File.Exists(candidate)) return candidate;
        }
        return null;
    }

    // ====================================================================
    //  Legacy bat path (--bat) — full rebuild via create-patch.bat
    // ====================================================================

    internal static int RunViaBat()
    {
        var config = KdevConfig.LoadOrPrintErrors(out var exitCode);
        if (config is null) return exitCode;

        // Validate that the patch source has the minimum required files.
        var manifest = Path.Combine(config.AccessibilitySource, "manifest.toml");
        if (!File.Exists(manifest))
        {
            Console.Error.WriteLine($"ERROR: manifest.toml not found at {manifest}");
            Console.Error.WriteLine("Cannot build a patch without a manifest. Create one and try again.");
            return 4;
        }

        var hooksFiles = Directory.GetFiles(config.AccessibilitySource, "*hooks.toml");
        if (hooksFiles.Length == 0)
        {
            Console.Error.WriteLine($"ERROR: No *hooks.toml files found in {config.AccessibilitySource}");
            Console.Error.WriteLine("Cannot build a patch without at least one hooks file.");
            return 4;
        }

        // Locate upstream files we depend on.
        var upstreamPatches = Path.Combine(config.UpstreamClone, "Patches");
        var upstreamCommon = Path.Combine(upstreamPatches, "Common");
        var upstreamLib = Path.Combine(config.UpstreamClone, "lib");
        var upstreamBat = Path.Combine(upstreamPatches, "create-patch.bat");

        if (!File.Exists(upstreamBat))
        {
            Console.Error.WriteLine($"ERROR: upstream create-patch.bat not found at {upstreamBat}");
            return 4;
        }

        // Set up staging directory: a fresh build/staging that mimics the layout the bat expects.
        var stagingRoot = Path.Combine(config.BuildOutput, "staging");
        if (Directory.Exists(stagingRoot))
        {
            Directory.Delete(stagingRoot, recursive: true);
        }
        Directory.CreateDirectory(stagingRoot);

        var stagedPatchesDir = Path.Combine(stagingRoot, "Patches");
        var stagedPatchDir = Path.Combine(stagedPatchesDir, "Accessibility");
        var stagedCommonDir = Path.Combine(stagedPatchesDir, "Common");
        var stagedLibDir = Path.Combine(stagingRoot, "lib");
        var stagedBat = Path.Combine(stagedPatchesDir, "create-patch.bat");

        Console.WriteLine("Staging build inputs...");
        CopyDirectory(config.AccessibilitySource, stagedPatchDir);
        Console.WriteLine($"  source        -> {stagedPatchDir}");

        if (Directory.Exists(upstreamCommon))
        {
            CopyDirectory(upstreamCommon, stagedCommonDir);
            Console.WriteLine($"  Common        -> {stagedCommonDir}");
        }

        if (Directory.Exists(upstreamLib))
        {
            CopyDirectory(upstreamLib, stagedLibDir);
            Console.WriteLine($"  lib           -> {stagedLibDir}");
        }

        File.Copy(upstreamBat, stagedBat, overwrite: true);
        Console.WriteLine($"  build script  -> {stagedBat}");

        // Prepare log file.
        Directory.CreateDirectory(config.LogsDir);
        var logPath = Path.Combine(
            config.LogsDir,
            $"build-{DateTime.UtcNow:yyyyMMdd-HHmmss}.log");

        Console.WriteLine();
        Console.WriteLine($"Running create-patch.bat in {stagedPatchDir}");
        Console.WriteLine($"Log: {logPath}");
        Console.WriteLine();

        var psi = new ProcessStartInfo
        {
            FileName = "cmd.exe",
            // /c runs the command and exits. We pipe < NUL into the inner bat so any unconditional
            // 'pause' calls in the bat's error paths see EOF and return immediately instead of hanging.
            Arguments = "/c \"..\\create-patch.bat < NUL\"",
            WorkingDirectory = stagedPatchDir,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        // Tells the bat to skip its terminal 'pause' on success path.
        psi.Environment["SKIP_PAUSE"] = "1";

        int batExit;
        using (var logWriter = new StreamWriter(logPath, append: false))
        {
            using var proc = Process.Start(psi)
                ?? throw new InvalidOperationException("Failed to start cmd.exe");

            proc.OutputDataReceived += (_, e) =>
            {
                if (e.Data is null) return;
                Console.WriteLine(e.Data);
                logWriter.WriteLine(e.Data);
            };
            proc.ErrorDataReceived += (_, e) =>
            {
                if (e.Data is null) return;
                Console.Error.WriteLine(e.Data);
                logWriter.WriteLine($"STDERR: {e.Data}");
            };

            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();
            proc.WaitForExit();
            batExit = proc.ExitCode;
        }

        if (batExit != 0)
        {
            Console.Error.WriteLine();
            Console.Error.WriteLine($"ERROR: create-patch.bat failed with exit code {batExit}.");
            Console.Error.WriteLine($"Full log: {logPath}");
            return 4;
        }

        var producedKpatch = Path.Combine(stagedPatchDir, "Accessibility.kpatch");
        if (!File.Exists(producedKpatch))
        {
            Console.Error.WriteLine();
            Console.Error.WriteLine(
                $"ERROR: bat reported success but {producedKpatch} not found. See {logPath}.");
            return 4;
        }

        Directory.CreateDirectory(config.BuildOutput);
        var finalKpatch = Path.Combine(config.BuildOutput, "Accessibility.kpatch");
        if (File.Exists(finalKpatch)) File.Delete(finalKpatch);
        File.Move(producedKpatch, finalKpatch);

        // Tear down staging now that we have the artifact.
        Directory.Delete(stagingRoot, recursive: true);

        Console.WriteLine();
        Console.WriteLine($"Built: {finalKpatch}");

        // Build the dinput8.dll proxy loader. Tiny 32-bit DLL that sits next
        // to swkotor.exe and auto-loads KotorPatcher when the game starts —
        // without it, `kdev launch` would launch an unmodded game.
        var loaderExit = BuildLoader(config);
        if (loaderExit != 0) return loaderExit;

        return 0;
    }

    private static int BuildLoader(KdevConfig config)
    {
        var loaderDir = Path.Combine(config.ProjectRoot, "loader");
        var buildBat = Path.Combine(loaderDir, "build.bat");
        if (!File.Exists(buildBat))
        {
            Console.Error.WriteLine($"ERROR: loader build script not found at {buildBat}");
            return 4;
        }

        Console.WriteLine();
        Console.WriteLine("Building loader (dinput8.dll proxy)...");

        var psi = new ProcessStartInfo
        {
            FileName = "cmd.exe",
            Arguments = $"/c \"\"{buildBat}\"\"",
            WorkingDirectory = loaderDir,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };

        using var proc = Process.Start(psi)
            ?? throw new InvalidOperationException("Failed to start cmd.exe for loader build");

        proc.OutputDataReceived += (_, e) => { if (e.Data is not null) Console.WriteLine(e.Data); };
        proc.ErrorDataReceived  += (_, e) => { if (e.Data is not null) Console.Error.WriteLine(e.Data); };
        proc.BeginOutputReadLine();
        proc.BeginErrorReadLine();
        proc.WaitForExit();

        if (proc.ExitCode != 0)
        {
            Console.Error.WriteLine($"ERROR: loader build failed (exit code {proc.ExitCode}).");
            return 4;
        }

        var producedDll = Path.Combine(loaderDir, "dinput8.dll");
        if (!File.Exists(producedDll))
        {
            Console.Error.WriteLine($"ERROR: loader build reported success but {producedDll} not found.");
            return 4;
        }
        Console.WriteLine($"Built: {producedDll}");
        return 0;
    }

    private static void CopyDirectory(string source, string destination)
    {
        Directory.CreateDirectory(destination);

        foreach (var file in Directory.GetFiles(source))
        {
            var name = Path.GetFileName(file);
            File.Copy(file, Path.Combine(destination, name), overwrite: true);
        }

        foreach (var dir in Directory.GetDirectories(source))
        {
            var name = Path.GetFileName(dir);
            CopyDirectory(dir, Path.Combine(destination, name));
        }
    }
}
