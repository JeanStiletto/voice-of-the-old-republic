using Tomlyn;
using Tomlyn.Model;

namespace Kdev;

/// <summary>
/// Project configuration loaded from kdev.toml at the repo root.
/// </summary>
public sealed record KdevConfig(
    string ProjectRoot,
    string GameInstall,
    string GameExe,
    string PatchManagerRelease,
    string UpstreamClone,
    string AccessibilitySource,
    string BuildOutput,
    string PatchId,
    IReadOnlyList<string> AdditionalPatchIds,
    // Reverse-engineering assets, consumed by `kdev re`. All optional in
    // kdev.toml ([re] section); the defaults match docs/tools.md so the
    // command works out of the box on the canonical setup.
    string ReSarifPath,
    string ReJqPath,
    string ReGhidraHeadless,
    string ReGhidraProjectDir,
    string ReGhidraProjectName,
    string ReGhidraProgram,
    string ReGhidraScripts)
{
    public string LogsDir => Path.Combine(ProjectRoot, "logs");

    public const string FileName = "kdev.toml";

    /// <summary>
    /// Walks upward from the current directory looking for kdev.toml.
    /// Returns the absolute path to it, or null if not found.
    /// </summary>
    public static string? FindConfigFile(string? startDir = null)
    {
        var dir = new DirectoryInfo(startDir ?? Directory.GetCurrentDirectory());
        while (dir is not null)
        {
            var candidate = Path.Combine(dir.FullName, FileName);
            if (File.Exists(candidate)) return candidate;
            dir = dir.Parent;
        }
        return null;
    }

    /// <summary>
    /// Loads and validates kdev.toml. Throws KdevConfigException on any problem,
    /// with a message naming the missing or invalid key.
    /// </summary>
    public static KdevConfig Load(string? configPath = null)
    {
        configPath ??= FindConfigFile()
            ?? throw new KdevConfigException(
                $"Could not find {FileName} in the current directory or any parent directory.");

        var text = File.ReadAllText(configPath);
        var projectRoot = Path.GetDirectoryName(Path.GetFullPath(configPath))!;

        TomlTable table;
        try
        {
            table = Toml.ToModel(text);
        }
        catch (Exception ex)
        {
            throw new KdevConfigException($"Failed to parse {configPath}: {ex.Message}", ex);
        }

        var gameInstall = ResolvePath(projectRoot, RequireString(table, "game.install"));
        var gameExe = Path.Combine(gameInstall, "swkotor.exe");
        var release = ResolvePath(projectRoot, RequireString(table, "patch_manager.release"));
        var clone = ResolvePath(projectRoot, RequireString(table, "upstream.clone"));
        var source = ResolvePath(projectRoot, RequireString(table, "accessibility.source"));
        var buildOutput = ResolvePath(projectRoot, RequireString(table, "accessibility.build_output"));
        var patchId = RequireString(table, "accessibility.patch_id");
        var additionalPatchIds = OptionalStringArray(table, "accessibility.additional_patches");

        // [re] — reverse-engineering assets. Optional; defaults mirror docs/tools.md.
        var reSarif = ResolvePath(projectRoot, OptionalString(table, "re.sarif")
            ?? Path.Combine("docs", "llm-docs", "re", "k1_win_gog_swkotor.exe.sarif"));
        var reJq = OptionalString(table, "re.jq") ?? "jq";  // resolved via PATH if bare
        var reGhidraHeadless = OptionalString(table, "re.ghidra_headless")
            ?? @"C:\Tools\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat";
        var reGhidraProjectDir = OptionalString(table, "re.ghidra_project_dir")
            ?? @"C:\Tools\ghidra-projects";
        var reGhidraProjectName = OptionalString(table, "re.ghidra_project_name") ?? "kotor1";
        var reGhidraProgram = OptionalString(table, "re.ghidra_program") ?? "k1_win_gog_swkotor.exe";
        var reGhidraScripts = ResolvePath(projectRoot, OptionalString(table, "re.ghidra_scripts")
            ?? Path.Combine("tools", "ghidra-scripts"));

        return new KdevConfig(
            ProjectRoot: projectRoot,
            GameInstall: gameInstall,
            GameExe: gameExe,
            PatchManagerRelease: release,
            UpstreamClone: clone,
            AccessibilitySource: source,
            BuildOutput: buildOutput,
            PatchId: patchId,
            AdditionalPatchIds: additionalPatchIds,
            ReSarifPath: reSarif,
            ReJqPath: reJq,
            ReGhidraHeadless: reGhidraHeadless,
            ReGhidraProjectDir: reGhidraProjectDir,
            ReGhidraProjectName: reGhidraProjectName,
            ReGhidraProgram: reGhidraProgram,
            ReGhidraScripts: reGhidraScripts);
    }

    /// <summary>
    /// Convenience helper for commands that need a fully-validated config.
    /// On failure, prints an error to stderr and returns null with the exit code set in <paramref name="exitCode"/>.
    /// On success, returns the config and sets <paramref name="exitCode"/> to 0.
    /// </summary>
    public static KdevConfig? LoadOrPrintErrors(out int exitCode)
    {
        exitCode = 0;
        KdevConfig cfg;
        try
        {
            cfg = Load();
        }
        catch (KdevConfigException ex)
        {
            Console.Error.WriteLine($"ERROR: {ex.Message}");
            exitCode = 2;
            return null;
        }

        var problems = cfg.Validate();
        if (problems.Count > 0)
        {
            Console.Error.WriteLine(
                $"ERROR: configuration has {problems.Count} problem(s); run 'kdev status' for details.");
            exitCode = 3;
            return null;
        }

        return cfg;
    }

    /// <summary>
    /// Returns a list of validation problems (empty if config is fully usable on this machine).
    /// Distinct from Load() — Load enforces shape, Validate checks the contents resolve to real things.
    /// </summary>
    public IReadOnlyList<string> Validate()
    {
        var problems = new List<string>();

        if (!Directory.Exists(GameInstall))
            problems.Add($"Game install directory does not exist: {GameInstall}");
        else if (!File.Exists(GameExe))
            problems.Add($"swkotor.exe not found at: {GameExe}");

        if (!Directory.Exists(PatchManagerRelease))
            problems.Add($"Patch manager release directory does not exist: {PatchManagerRelease}");
        else
        {
            // KPatchLauncher.exe is no longer used: 'kdev launch' starts
            // swkotor.exe directly and the bundled dinput8.dll proxy auto-
            // loads KotorPatcher. Only the patcher DLL still needs to be
            // present so 'kdev apply' can stage it into the game folder.
            var patcherDll = Path.Combine(PatchManagerRelease, "bin", "KotorPatcher.dll");
            if (!File.Exists(patcherDll))
                problems.Add($"KotorPatcher.dll not found in release: {patcherDll}");
        }

        if (!Directory.Exists(UpstreamClone))
            problems.Add($"Upstream clone directory does not exist: {UpstreamClone}");

        if (!Directory.Exists(AccessibilitySource))
            problems.Add($"Patch source directory does not exist: {AccessibilitySource}");

        return problems;
    }

    private static IReadOnlyList<string> OptionalStringArray(TomlTable table, string dottedKey)
    {
        var value = NavigateTable(table, dottedKey);
        if (value is null) return Array.Empty<string>();
        if (value is not TomlArray arr)
            throw new KdevConfigException($"Key {dottedKey} in {FileName} must be an array of strings.");

        var result = new List<string>(arr.Count);
        for (int i = 0; i < arr.Count; i++)
        {
            if (arr[i] is not string s || string.IsNullOrWhiteSpace(s))
                throw new KdevConfigException($"Key {dottedKey}[{i}] in {FileName} must be a non-empty string.");
            result.Add(s);
        }
        return result;
    }

    private static string RequireString(TomlTable table, string dottedKey)
    {
        var value = NavigateTable(table, dottedKey);
        if (value is null)
            throw new KdevConfigException($"Missing required key in {FileName}: {dottedKey}");
        if (value is not string s || string.IsNullOrWhiteSpace(s))
            throw new KdevConfigException($"Key {dottedKey} in {FileName} must be a non-empty string.");
        return s;
    }

    private static string? OptionalString(TomlTable table, string dottedKey)
    {
        var value = NavigateTable(table, dottedKey);
        if (value is null) return null;
        if (value is not string s || string.IsNullOrWhiteSpace(s))
            throw new KdevConfigException($"Key {dottedKey} in {FileName} must be a non-empty string.");
        return s;
    }

    private static object? NavigateTable(TomlTable table, string dottedKey)
    {
        object? current = table;
        foreach (var segment in dottedKey.Split('.'))
        {
            if (current is not TomlTable t) return null;
            if (!t.TryGetValue(segment, out var next)) return null;
            current = next;
        }
        return current;
    }

    private static string ResolvePath(string projectRoot, string path)
    {
        return Path.IsPathRooted(path)
            ? Path.GetFullPath(path)
            : Path.GetFullPath(Path.Combine(projectRoot, path));
    }
}

public sealed class KdevConfigException : Exception
{
    public KdevConfigException(string message) : base(message) { }
    public KdevConfigException(string message, Exception inner) : base(message, inner) { }
}
