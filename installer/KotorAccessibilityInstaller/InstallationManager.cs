using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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
        private readonly GameTarget _target;

        // Names of resources embedded in the installer assembly.
        // The KPatchCore.PatchApplicator expects `AddressDatabases/` next to the
        // executing assembly at runtime, and `sqlite3.dll` next to the patcher
        // DLL it copies into the game folder.
        private static readonly string[] PatcherRuntimeFiles = { "KotorPatcher.dll", "sqlite3.dll" };
        private static readonly string[] AddressDbFiles = { "kotor1_0_3.db" };
        private const string PrismDllName = "prism.dll";
        private const string LoaderDllName = "dinput8.dll";

        // WAV samples shipped into <game>/Override/ for the engine's
        // ResLoader to pick up by bare resref. Uninstall removes
        // AllOverrideAssetNamesEverInstalled, not this list — see below.
        private static readonly string[] OverrideAssets = { "acc_boost.wav", "acc_turret_loop.wav", "acc_turret_lock.wav", "acc_turret_tick.wav", "acc_steer_ok.wav" };
        public static IReadOnlyList<string> OverrideAssetNames => OverrideAssets;

        // Override assets earlier versions shipped and current ones do not.
        // acc_steer_l / acc_steer_r were the discrete directional steering ticks,
        // retired when the steering magnet replaced that guide (see the csproj).
        //
        // They matter because removal is driven by the list of what we install
        // TODAY, so anything we ever shipped and then stopped shipping becomes
        // permanent litter: no uninstall will ever mention it again. Both files
        // were still sitting in a real KOTOR 1 and KOTOR 2 Override folder after
        // a clean uninstall. Same shape as the renamed uninstaller exe and the
        // pre-dsoal ini backup in UninstallFlow — when something is dropped from
        // the install set, it has to be added here in the same change.
        private static readonly string[] LegacyOverrideAssets = { "acc_steer_l.wav", "acc_steer_r.wav" };

        /// <summary>Every Override file we have ever installed — what uninstall must remove.</summary>
        public static IReadOnlyList<string> AllOverrideAssetNamesEverInstalled =>
            new List<string>(OverrideAssets).Concat(LegacyOverrideAssets).ToList();

        public InstallationManager(GameTarget target, string gameDir)
        {
            _target = target ?? throw new ArgumentNullException(nameof(target));
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

            // Stage the downloaded accessibility .kpatch alongside this game's
            // bundled third-party patches. PatchRepository picks them all up;
            // ApplyKPatch installs the eligible ones in one PatchApplicator call.
            string stagedKpatch = Path.Combine(patchesDir, Path.GetFileName(kpatchSourcePath));
            File.Copy(kpatchSourcePath, stagedKpatch, overwrite: true);

            foreach (var (assetName, _) in _target.BundledPatches)
                ExtractEmbeddedResource(assetName, Path.Combine(patchesDir, assetName));

            Logger.Info($"Staged patcher runtime into: {stagingRoot}");
            return stagingRoot;
        }

        /// <summary>
        /// Runs KPatchCore.PatchApplicator against the staged runtime.
        /// Returns the install result so the caller can surface messages + errors.
        /// </summary>
        public PatchApplicator.InstallResult ApplyKPatch(string stagingRoot)
        {
            string gameExe = Path.Combine(_gameDir, _target.ExeName);
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

            var patchIds = new List<string> { Config.PatchId };

            // The bundled patches are optional and each ships hashes for only the
            // builds it was made for (KOTOR 1's widescreen covers the three vanilla
            // 1.0.3 builds; KOTOR 2's two cover the Aspyr Steam and GOG builds). An
            // install is one transaction, so including one on an executable it does
            // not declare fails the shared version gate and takes our patch down
            // with it -- which is how a Russian-translation install (its own
            // relinked executable) ended up unable to install the mod at all.
            // Include each only when this executable is one it claims, and carry on
            // without it otherwise: these are nice-to-haves, the accessibility patch
            // is the point.
            //
            // The same gate also keeps re-installs idempotent. These patches rewrite
            // the executable, so once applied its SHA-256 is no longer a declared
            // build and the patch is skipped rather than force-applied over itself.
            //
            // Deliberately NOT solved with a version-mismatch override: they write
            // to the executable, and forcing that onto a build whose addresses they
            // were never built for is the one thing worth refusing.
            foreach (var (_, patchId) in _target.BundledPatches)
            {
                if (IsPatchSupportedForExe(repository, patchId, gameExe, out string skipReason))
                    patchIds.Add(patchId);
                else
                    Logger.Info($"Skipping bundled patch '{patchId}': {skipReason}");
            }

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
        /// Whether the accessibility patch itself declares support for this
        /// executable, checked BEFORE <see cref="ApplyKPatch"/> so the caller can
        /// explain the one failure a user can act on.
        ///
        /// <para>Without this the case still fails safe — KPatchCore's version gate
        /// refuses the install — but it surfaces as the generic "patch application
        /// failed" dialog carrying KPatchCore's English error string, which tells a
        /// player nothing about what to do. The bundled third-party patches are
        /// deliberately NOT covered here: an executable they don't declare is
        /// normal and is silently skipped in <see cref="ApplyKPatch"/>.</para>
        ///
        /// <para><paramref name="exeFingerprint"/> is the short SHA-256 prefix, for
        /// the message and for the bug report that should follow it.</para>
        /// </summary>
        public bool IsAccessibilityPatchSupported(string stagingRoot, out string exeFingerprint)
        {
            exeFingerprint = "unknown";
            string gameExe = Path.Combine(_gameDir, _target.ExeName);
            var repository = new PatchRepository(Path.Combine(stagingRoot, "patches"));
            var scanResult = repository.ScanPatches();
            if (!scanResult.Success)
            {
                // Can't tell — let ApplyKPatch run and report whatever it hits,
                // rather than blocking an install on our own pre-flight failing.
                Logger.Warning($"Version pre-check skipped: {scanResult.Error}");
                return true;
            }

            try
            {
                exeFingerprint = KPatchCore.Common.FileHasher.ComputeSha256(gameExe).Substring(0, 16);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Version pre-check could not hash {_target.ExeName}: {ex.Message}");
                return true;
            }

            bool supported = IsPatchSupportedForExe(repository, Config.PatchId, gameExe, out string reason);
            if (!supported)
                Logger.Error($"Unsupported game build: {reason}");
            return supported;
        }

        /// <summary>
        /// Whether a staged patch declares support for this exact executable.
        /// Compares the file's SHA-256 against the patch manifest's
        /// supported_versions rather than asking for a version *name*, because an
        /// unrecognised build has no name to ask about.
        /// </summary>
        private static bool IsPatchSupportedForExe(PatchRepository repository, string patchId, string gameExe, out string reason)
        {
            var entry = repository.GetPatch(patchId);
            if (!entry.Success || entry.Data?.Manifest == null)
            {
                reason = $"patch '{patchId}' not found in the staged repository";
                return false;
            }

            string hash;
            try
            {
                hash = KPatchCore.Common.FileHasher.ComputeSha256(gameExe);
            }
            catch (Exception ex)
            {
                reason = $"could not hash {Path.GetFileName(gameExe)}: {ex.Message}";
                return false;
            }

            foreach (var supported in entry.Data.Manifest.SupportedVersions)
            {
                if (string.Equals(supported.Value, hash, StringComparison.OrdinalIgnoreCase))
                {
                    reason = null;
                    return true;
                }
            }

            reason = $"this {Path.GetFileName(gameExe)} (SHA-256 {hash.Substring(0, 16)}...) is not one of the " +
                     $"{entry.Data.Manifest.SupportedVersions.Count} builds it supports";
            return false;
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
            try
            {
                ExtractEmbeddedResource(LoaderDllName, dest);
            }
            catch (Exception ex) when (ex is UnauthorizedAccessException || ex is IOException)
            {
                // The proxy loader is the one file security software reliably
                // blocks: dropping a "dinput8.dll" into an application folder is
                // the textbook DLL-hijack pattern, so Defender / Kaspersky flag
                // it by name+behaviour even though our use is legitimate. The
                // resilient writer has already retried, so an access-denied /
                // sharing violation here is a persistent block. Turn the raw
                // exception into actionable antivirus guidance for the user.
                throw new LoaderBlockedException(_gameDir, dest, ex);
            }
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

            // Per-asset isolation: a single missing/un-embedded resource must
            // not abort the whole set. Before this guard, one omission (the
            // v0.5.7 acc_steer_*.wav, never added to the .csproj EmbeddedResource
            // list) threw on the first steer cue and silently dropped every
            // asset after it — leaving the swoop steering guidance with no audio.
            int installed = 0;
            foreach (var name in OverrideAssets)
            {
                string dest = Path.Combine(overrideDir, name);
                try
                {
                    ExtractEmbeddedResource(name, dest);
                    installed++;
                }
                catch (Exception ex)
                {
                    Logger.Warning($"Override asset '{name}' could not be installed: {ex.Message}");
                }
            }
            Logger.Info($"Installed {installed}/{OverrideAssets.Length} Override assets into: {overrideDir}");
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
                : ReadEmbeddedResourceBytes(_target.VanillaPriorityGroupResource);

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

        /// <summary>
        /// Prefix every staging directory this installer creates under %TEMP%.
        /// </summary>
        private const string StagingPrefix = "kotor_acc_";

        /// <summary>
        /// Delete any staging directories left behind, ours or an earlier run's.
        /// Called once as the process exits.
        ///
        /// <para>Needed because <see cref="CleanupStaging"/> cannot always win:
        /// KPatchCore opens the address database with SQLite and that handle
        /// lives until the process does, so an in-flight delete of the directory
        /// holding it fails no matter how long it retries. Three directories had
        /// accumulated across a session even with retries in place. At exit the
        /// handle is gone and the delete simply works.</para>
        ///
        /// <para>Deleting other runs' directories too is deliberate: they are all
        /// ours by prefix, and a concurrent installer's files are locked, so the
        /// delete fails harmlessly rather than pulling the floor out from under
        /// it.</para>
        /// </summary>
        public static void SweepAbandonedStagingDirs()
        {
            try
            {
                string temp = Path.GetTempPath();
                foreach (var dir in Directory.EnumerateDirectories(temp, StagingPrefix + "*"))
                {
                    try
                    {
                        Directory.Delete(dir, recursive: true);
                        Logger.Info($"Swept leftover staging dir: {dir}");
                    }
                    catch
                    {
                        // In use by a concurrent run, or still locked. Not worth
                        // a warning at exit — the next run sweeps it.
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Info($"Staging sweep skipped: {ex.Message}");
            }
        }

        /// <summary>
        /// Delete the staging directory, retrying briefly.
        ///
        /// <para>KPatchCore opens the address database with SQLite and the file
        /// handle outlives the install call, so the first delete reliably fails
        /// with "kotor1_0_3.db is being used by another process" and the whole
        /// directory is orphaned. Ten of them had accumulated in %TEMP% on a
        /// real machine — several MB each, every run. A short retry lets the
        /// handle close.</para>
        /// </summary>
        public void CleanupStaging(string stagingRoot)
        {
            if (string.IsNullOrEmpty(stagingRoot) || !Directory.Exists(stagingRoot)) return;

            for (int attempt = 1; attempt <= 5; attempt++)
            {
                try
                {
                    Directory.Delete(stagingRoot, recursive: true);
                    Logger.Info($"Cleaned up staging dir: {stagingRoot}");
                    return;
                }
                catch (Exception ex) when (attempt < 5)
                {
                    Logger.Info($"Staging dir still locked ({ex.GetType().Name}); retrying in {attempt * 400} ms");
                    System.Threading.Thread.Sleep(attempt * 400);
                }
                catch (Exception ex)
                {
                    // Genuinely stuck. A leftover temp directory is untidy, not
                    // harmful, and must never fail an otherwise good install.
                    Logger.Warning($"Could not clean up staging dir {stagingRoot}: {ex.Message}");
                }
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
            byte[] data = ReadEmbeddedResourceBytes(shortName);
            WriteFileResilient(targetPath, data, shortName);
            Logger.Info($"Extracted: {shortName} -> {targetPath}");
        }

        // Backoff schedule (ms) between write retries. Antivirus / security
        // software occasionally holds a brief lock on files we drop into the
        // game folder while it scans them, so a first FileMode.Create can fail
        // with a sharing violation or "access denied" that clears a moment
        // later. One initial attempt plus these delays ≈ 3s of best-effort
        // retry — enough to ride out a transient scan-lock without hanging the
        // install on a genuine, persistent block.
        private static readonly int[] WriteRetryBackoffMs = { 200, 400, 800, 1600 };

        /// <summary>
        /// Writes <paramref name="data"/> to <paramref name="targetPath"/>,
        /// clearing any stale read-only attribute first and retrying on
        /// transient I/O / access errors with backoff. If every attempt fails
        /// the final exception propagates unchanged, so callers can still
        /// distinguish an <see cref="UnauthorizedAccessException"/> (e.g. an
        /// antivirus block) from other failures.
        /// </summary>
        private static void WriteFileResilient(string targetPath, byte[] data, string shortName)
        {
            int totalAttempts = WriteRetryBackoffMs.Length + 1;
            for (int attempt = 1; ; attempt++)
            {
                try
                {
                    ClearReadOnly(targetPath);
                    using (var fileStream = new FileStream(targetPath, FileMode.Create, FileAccess.Write, FileShare.None))
                    {
                        fileStream.Write(data, 0, data.Length);
                    }
                    return;
                }
                catch (Exception ex) when ((ex is IOException || ex is UnauthorizedAccessException) && attempt < totalAttempts)
                {
                    int delay = WriteRetryBackoffMs[attempt - 1];
                    Logger.Warning($"Write of {shortName} to {targetPath} failed (attempt {attempt}/{totalAttempts}): {ex.Message}. Retrying in {delay} ms...");
                    System.Threading.Thread.Sleep(delay);
                }
            }
        }

        /// <summary>
        /// Best-effort clear of the read-only attribute on an existing target so
        /// FileMode.Create can overwrite it. A prior install (or a mod archive
        /// extracted read-only) can leave dinput8.dll / prism.dll read-only,
        /// which otherwise fails the write instantly with "access denied".
        /// </summary>
        private static void ClearReadOnly(string path)
        {
            try
            {
                if (File.Exists(path))
                {
                    var attrs = File.GetAttributes(path);
                    if ((attrs & FileAttributes.ReadOnly) != 0)
                        File.SetAttributes(path, attrs & ~FileAttributes.ReadOnly);
                }
            }
            catch
            {
                // Non-fatal: the write attempt itself will surface any real
                // permission problem with a clearer, actionable exception.
            }
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

    /// <summary>
    /// Raised when the dinput8.dll proxy loader cannot be written into the game
    /// folder even after the resilient writer's retries — almost always because
    /// antivirus / Windows Defender blocked the drop (the proxy-DLL name trips
    /// DLL-hijack heuristics). Carries the game directory and loader path so
    /// MainForm can show localized "add an exclusion and re-run" guidance
    /// instead of a raw access-denied message.
    /// </summary>
    public class LoaderBlockedException : Exception
    {
        public string GameDir { get; }
        public string LoaderPath { get; }

        public LoaderBlockedException(string gameDir, string loaderPath, Exception inner)
            : base($"Could not write the mod loader to '{loaderPath}' — the write was blocked, most likely by antivirus/Windows Defender.", inner)
        {
            GameDir = gameDir;
            LoaderPath = loaderPath;
        }
    }
}
