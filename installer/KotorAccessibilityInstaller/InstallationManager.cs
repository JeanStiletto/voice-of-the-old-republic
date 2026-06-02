using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using KPatchCore.Applicators;
using KPatchCore.Managers;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Drives the KOTOR-side installation: stages the bundled KPatchManager runtime
    /// (KotorPatcher.dll, sqlite3.dll, AddressDatabases/) into a temp directory,
    /// invokes KPatchCore.PatchApplicator against the downloaded .kpatch, then
    /// drops the bundled Prism speech runtime alongside accessibility.dll.
    /// </summary>
    public class InstallationManager
    {
        private readonly string _gameDir;

        // Names of resources embedded in the installer assembly.
        // The KPatchCore.PatchApplicator expects `AddressDatabases/` next to the
        // executing assembly at runtime, and `sqlite3.dll` next to the patcher
        // DLL it copies into the game folder.
        private static readonly string[] PatcherRuntimeFiles = { "KotorPatcher.dll", "sqlite3.dll" };
        private static readonly string[] AddressDbFiles = { "kotor1_0_3.db" };
        private const string PrismDllName = "prism.dll";
        private const string LoaderDllName = "dinput8.dll";

        // WAV samples shipped into <game>/Override/ for the engine's
        // ResLoader to pick up by bare resref. Mirrored as the
        // OverrideAssetNames list on PerformUninstall so removal stays
        // in sync.
        private static readonly string[] OverrideAssets = { "acc_boost.wav", "acc_turret_loop.wav", "acc_turret_lock.wav" };
        public static IReadOnlyList<string> OverrideAssetNames => OverrideAssets;

        public InstallationManager(string gameDir)
        {
            _gameDir = gameDir ?? throw new ArgumentNullException(nameof(gameDir));
        }

        /// <summary>
        /// Unpacks the bundled KPatchManager runtime into a temp staging directory
        /// in the layout KPatchCore expects:
        ///   &lt;stagingRoot&gt;/bin/KotorPatcher.dll
        ///   &lt;stagingRoot&gt;/bin/sqlite3.dll
        ///   &lt;stagingRoot&gt;/AddressDatabases/*.db
        ///   &lt;stagingRoot&gt;/patches/Accessibility.kpatch
        ///   &lt;stagingRoot&gt;/patches/Widescreen.kpatch
        ///
        /// Returns the staging root path. Caller should clean up later.
        /// </summary>
        public string StagePatcherRuntime(string kpatchSourcePath)
        {
            string stagingRoot = Path.Combine(Path.GetTempPath(), $"kotor_acc_install_{Guid.NewGuid():N}");
            Directory.CreateDirectory(stagingRoot);

            string binDir = Path.Combine(stagingRoot, "bin");
            string dbDir = Path.Combine(stagingRoot, "AddressDatabases");
            string patchesDir = Path.Combine(stagingRoot, "patches");
            Directory.CreateDirectory(binDir);
            Directory.CreateDirectory(dbDir);
            Directory.CreateDirectory(patchesDir);

            foreach (var name in PatcherRuntimeFiles)
                ExtractEmbeddedResource(name, Path.Combine(binDir, name));

            foreach (var name in AddressDbFiles)
                ExtractEmbeddedResource(name, Path.Combine(dbDir, name));

            // KPatchCore.PatchApplicator locates the address DB via
            // Assembly.GetExecutingAssembly().Location, which returns an empty
            // string for assemblies inside a single-file self-contained app.
            // That collapses to the relative path "AddressDatabases" — resolved
            // against CurrentDirectory at apply time. We swap CWD to stagingRoot
            // inside ApplyKPatch so that relative lookup hits the dir we just
            // populated above.

            // Stage the downloaded accessibility .kpatch alongside the bundled
            // widescreen .kpatch. PatchRepository picks both up; ApplyKPatch
            // installs both in one PatchApplicator call.
            string stagedKpatch = Path.Combine(patchesDir, Path.GetFileName(kpatchSourcePath));
            File.Copy(kpatchSourcePath, stagedKpatch, overwrite: true);

            ExtractEmbeddedResource(
                Config.WidescreenKPatchAssetName,
                Path.Combine(patchesDir, Config.WidescreenKPatchAssetName));

            Logger.Info($"Staged patcher runtime into: {stagingRoot}");
            return stagingRoot;
        }

        /// <summary>
        /// Runs KPatchCore.PatchApplicator against the staged runtime.
        /// Returns the install result so the caller can surface messages + errors.
        /// </summary>
        public PatchApplicator.InstallResult ApplyKPatch(string stagingRoot)
        {
            string gameExe = Path.Combine(_gameDir, "swkotor.exe");
            string patcherDll = Path.Combine(stagingRoot, "bin", "KotorPatcher.dll");
            string patchesDir = Path.Combine(stagingRoot, "patches");

            var repository = new PatchRepository(patchesDir);
            var scanResult = repository.ScanPatches();
            if (!scanResult.Success)
            {
                return new PatchApplicator.InstallResult
                {
                    Success = false,
                    Error = $"Failed to scan staged patches dir: {scanResult.Error}"
                };
            }

            var patchIds = new List<string> { Config.PatchId, Config.WidescreenPatchId };
            Logger.Info($"Installing [{string.Join(", ", patchIds)}] into {gameExe}...");
            var applicator = new PatchApplicator(repository);

            // Swap CWD so KPatchCore's relative "AddressDatabases" lookup hits
            // the directory we populated under stagingRoot. Restore on exit.
            string previousCwd = Directory.GetCurrentDirectory();
            try
            {
                Directory.SetCurrentDirectory(stagingRoot);
                return applicator.InstallPatches(new PatchApplicator.InstallOptions
                {
                    GameExePath = gameExe,
                    PatchIds = patchIds,
                    PatcherDllPath = patcherDll,
                    // No backup: KOTOR is always reacquirable via Steam ("Verify integrity
                    // of game files") or by reinstalling from GoG. Avoids cluttering the
                    // game folder with timestamped swkotor.exe.backup.* files.
                    CreateBackup = false
                });
            }
            finally
            {
                try { Directory.SetCurrentDirectory(previousCwd); } catch { /* best-effort */ }
            }
        }

        /// <summary>
        /// Drops the dinput8.dll proxy loader into the game root. swkotor.exe
        /// statically imports DINPUT8.dll, and the application directory wins
        /// the loader search over System32, so on next launch Windows maps our
        /// proxy. Its DllMain forwards the six dinput8 exports to the real
        /// system DLL and spawns a worker thread that LoadLibrary's
        /// KotorPatcher.dll once the engine has created its window — same
        /// timing signal kdev's KPatchLauncher uses for delayed Steam
        /// injection. Result: the user just hits Play in Steam (or double-
        /// clicks swkotor.exe) and the mod loads itself.
        /// </summary>
        public void InstallLoader()
        {
            string dest = Path.Combine(_gameDir, LoaderDllName);
            ExtractEmbeddedResource(LoaderDllName, dest);
            Logger.Info($"Installed dinput8.dll proxy loader to: {dest}");
        }

        /// <summary>
        /// Copies the bundled Prism speech runtime into &lt;game&gt;/patches/.
        /// Prism's NVDA bridge is statically linked, so no separate NVDA DLL is shipped —
        /// see docs/installer.md for the migration rationale.
        ///
        /// Also removes stale Tolk runtime files left over from earlier installer
        /// versions (Tolk.dll, nvdaControllerClient32.dll), matching the cleanup
        /// path in tools/kdev/Commands/ApplyCommand.cs.
        /// </summary>
        public void InstallPrismRuntime()
        {
            string patchesDir = Path.Combine(_gameDir, "patches");
            Directory.CreateDirectory(patchesDir);

            string prismDest = Path.Combine(patchesDir, PrismDllName);
            ExtractEmbeddedResource(PrismDllName, prismDest);
            Logger.Info($"Installed Prism speech runtime to: {prismDest}");

            string[] staleTolkFiles = { "Tolk.dll", "nvdaControllerClient32.dll" };
            foreach (var stale in staleTolkFiles)
            {
                string stalePath = Path.Combine(patchesDir, stale);
                if (File.Exists(stalePath))
                {
                    try
                    {
                        File.Delete(stalePath);
                        Logger.Info($"Removed stale {stale}");
                    }
                    catch (Exception ex)
                    {
                        Logger.Warning($"Could not remove stale {stale}: {ex.Message}");
                    }
                }
            }
        }

        /// <summary>
        /// Drops custom WAV samples (and any other Aurora-engine assets we
        /// ship) into &lt;game&gt;/Override/. The engine's ResLoader checks
        /// Override → BIF, so a bare resref like "acc_boost" resolves to
        /// our file. Used by the swoop-race accelerator-pad loop, where
        /// the vanilla mgs_basethrust03 sample's pitch-drop tail reads as
        /// the wrong direction; we ship a tail-trimmed variant under a
        /// new resref to avoid colliding with any vanilla use.
        /// </summary>
        public void InstallOverrideAssets()
        {
            string overrideDir = Path.Combine(_gameDir, "Override");
            Directory.CreateDirectory(overrideDir);

            foreach (var name in OverrideAssets)
            {
                string dest = Path.Combine(overrideDir, name);
                ExtractEmbeddedResource(name, dest);
            }
            Logger.Info($"Installed Override assets into: {overrideDir}");
        }

        /// <summary>
        /// Appends the mod's dedicated full-volume audio priority group to
        /// &lt;game&gt;/Override/prioritygroups.2da. Conflict-safe: reads the
        /// current Override file if present (preserving any other mod's rows),
        /// otherwise starts from the bundled vanilla copy, then appends our
        /// sentinel-tagged row. Idempotent — re-running an install does not
        /// add a second copy (PriorityGroup2da.AppendAccGroup detects the
        /// sentinel and no-ops).
        ///
        /// The DLL resolves our group at runtime by the sentinel FadeTime, so
        /// the row index doesn't matter. We deliberately do NOT remove this
        /// file on uninstall: it may carry a third-party mod's rows, and the
        /// lone extra row is inert once our DLL is gone.
        /// </summary>
        public void InstallPriorityGroup()
        {
            string overrideDir = Path.Combine(_gameDir, "Override");
            Directory.CreateDirectory(overrideDir);
            string target = Path.Combine(overrideDir, "prioritygroups.2da");

            bool fromExisting = File.Exists(target);
            byte[] source = fromExisting
                ? File.ReadAllBytes(target)
                : ReadEmbeddedResourceBytes("prioritygroups.2da");

            byte[] result = PriorityGroup2da.AppendAccGroup(source);
            if (ReferenceEquals(result, source))
            {
                Logger.Info("Priority group already present in prioritygroups.2da; skipping.");
                return;
            }

            File.WriteAllBytes(target, result);
            Logger.Info($"Installed accessibility priority group -> {target} " +
                        $"({source.Length} -> {result.Length} bytes, source: " +
                        $"{(fromExisting ? "existing Override file" : "bundled vanilla")}).");
        }

        /// <summary>
        /// Copies the running installer EXE into the game folder so Add/Remove
        /// Programs has a stable uninstaller path even if the user deletes the
        /// original download. Returns the destination path for registry use.
        /// </summary>
        public string CopyUninstaller()
        {
            string sourceExe = Environment.ProcessPath ?? string.Empty;
            string destExe = Path.Combine(_gameDir, Config.UninstallerExeName);

            if (string.IsNullOrEmpty(sourceExe) || !File.Exists(sourceExe))
            {
                Logger.Warning($"Could not locate running installer EXE; uninstaller copy skipped (source: '{sourceExe}')");
                return null;
            }

            try
            {
                File.Copy(sourceExe, destExe, overwrite: true);
                Logger.Info($"Copied uninstaller to: {destExe}");
                return destExe;
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not copy uninstaller to game folder: {ex.Message}");
                return null;
            }
        }

        public void CleanupStaging(string stagingRoot)
        {
            try
            {
                if (Directory.Exists(stagingRoot))
                {
                    Directory.Delete(stagingRoot, recursive: true);
                    Logger.Info($"Cleaned up staging dir: {stagingRoot}");
                }
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not clean up staging dir: {ex.Message}");
            }
        }

        private static byte[] ReadEmbeddedResourceBytes(string shortName)
        {
            var assembly = Assembly.GetExecutingAssembly();
            string fullName = FindResourceName(assembly, shortName);
            if (fullName == null)
                throw new FileNotFoundException($"Embedded resource not found: {shortName}");

            using var stream = assembly.GetManifestResourceStream(fullName);
            if (stream == null)
                throw new InvalidOperationException($"Could not open resource stream: {fullName}");
            using var ms = new MemoryStream();
            stream.CopyTo(ms);
            return ms.ToArray();
        }

        private static void ExtractEmbeddedResource(string shortName, string targetPath)
        {
            var assembly = Assembly.GetExecutingAssembly();
            string fullName = FindResourceName(assembly, shortName);
            if (fullName == null)
                throw new FileNotFoundException($"Embedded resource not found: {shortName}");

            using (var stream = assembly.GetManifestResourceStream(fullName))
            {
                if (stream == null)
                    throw new InvalidOperationException($"Could not open resource stream: {fullName}");

                using (var fileStream = new FileStream(targetPath, FileMode.Create, FileAccess.Write))
                {
                    stream.CopyTo(fileStream);
                }
            }
            Logger.Info($"Extracted: {shortName} -> {targetPath}");
        }

        private static string FindResourceName(Assembly assembly, string shortName)
        {
            foreach (var name in assembly.GetManifestResourceNames())
            {
                if (name.EndsWith(shortName, StringComparison.OrdinalIgnoreCase))
                    return name;
            }
            Logger.Warning($"Resource '{shortName}' not found. Available resources:");
            foreach (var name in assembly.GetManifestResourceNames())
                Logger.Warning($"  - {name}");
            return null;
        }
    }
}
