using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text;
using Microsoft.Win32;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Bundles the newest crash dump of EVERY installed game together with the
    /// patch log of the session that actually produced it (plus that install's
    /// newest log when it is a different session),
    /// plus the installer log and a small system-info file, into a single
    /// archive in the user's Downloads folder so beta testers have a one-file
    /// artefact to attach to a bug report.
    ///
    /// <para>Every game rather than one: the dialog that offers this has no
    /// game picker, and the target it passed in was whichever game the
    /// maintenance flow happened to resolve — always KOTOR 1 when both are
    /// modded, since that is the head of <see cref="GameTarget.All"/>. A KOTOR 2
    /// bug therefore arrived with KOTOR 1's log attached, and neither the
    /// tester nor the dialog gave any sign of it. Collecting both costs
    /// tens of KB in the normal case (patch logs are repetitive enough to
    /// compress 10-70x) and is the only variant with no silent wrong
    /// answer.</para>
    /// </summary>
    public static class LogCollector
    {
        public class Result
        {
            public bool Success;
            public string ArchivePath;
            /// <summary>Patch logs across all games — at most one per game.</summary>
            public int LogCount;
            /// <summary>Crash dumps across all games — at most one per game.</summary>
            public int DumpCount;
            public bool IncludedInstallerLog;
            public string Error;
        }

        /// <summary>An installed game and where it lives.</summary>
        private sealed class GameSite
        {
            public GameTarget Target;
            public string Path;
        }

        /// <param name="target">
        /// The game the calling flow resolved. Only used to honour a path the
        /// caller knows about but detection would not find (an explicit
        /// command-line path); every other game is discovered here.
        /// </param>
        public static Result Collect(GameTarget target, string gamePath)
        {
            var result = new Result();
            try
            {
                string downloadsDir = GetDownloadsDir();
                if (!Directory.Exists(downloadsDir))
                {
                    result.Error = $"Downloads folder not found: {downloadsDir}";
                    return result;
                }

                string stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
                string staging = Path.Combine(Path.GetTempPath(), $"kotor_acc_logs_{stamp}");
                Directory.CreateDirectory(staging);

                try
                {
                    var sites = DiscoverGames(target, gamePath);
                    Logger.Info("[LogCollector] Collecting for: " +
                        (sites.Count == 0 ? "(no game install found)"
                                          : string.Join(", ", sites.Select(s => $"{s.Target.DisplayName} @ {s.Path}"))));

                    var dumpPairings = new List<string>();

                    foreach (var site in sites)
                    {
                        // Staged flat with a game-id prefix rather than in
                        // per-game subfolders: both games name their logs
                        // patch-<timestamp>.log, so a flat copy could collide,
                        // and a prefix keeps the archive one linear list to
                        // read instead of a tree to walk.
                        //
                        // The dump is resolved FIRST because it decides which log
                        // matters. Bundling "the newest log" on its own was
                        // reliably wrong: a crash is followed by the player
                        // relaunching, so by the time anyone collects, the newest
                        // log is the session AFTER the crash and the crashing
                        // session's log is second-newest. One real bundle shipped
                        // a log that began 24 s after its dump was written and
                        // ended in a clean Quit — which reads as "no crash
                        // happened here" and cost a dump-parsing detour to
                        // recover what the right log would have said outright.
                        string newestDump = FindNewestCrashDump(site.Target);
                        DateTime? dumpUtc = newestDump != null
                            ? new FileInfo(newestDump).LastWriteTimeUtc
                            : (DateTime?)null;

                        string sessionLog = dumpUtc.HasValue
                            ? FindPatchLogForSession(site.Path, dumpUtc.Value)
                            : null;
                        string newestLog = FindNewestPatchLog(site.Path);

                        // Ship the crashing session's log AND the newest one when
                        // they differ. The first says what went wrong; the second
                        // is the install's current state, and is the only one that
                        // matters for a report that isn't about a crash. Deduped,
                        // so the ordinary case is still a single file per game.
                        var logsToCopy = new List<string>();
                        if (sessionLog != null) logsToCopy.Add(sessionLog);
                        if (newestLog != null &&
                            !string.Equals(newestLog, sessionLog, StringComparison.OrdinalIgnoreCase))
                        {
                            logsToCopy.Add(newestLog);
                        }

                        foreach (string log in logsToCopy)
                        {
                            File.Copy(log,
                                      Path.Combine(staging, $"{site.Target.Id}-{Path.GetFileName(log)}"),
                                      overwrite: true);
                            result.LogCount++;
                            Logger.Info($"[LogCollector] Included {site.Target.DisplayName} patch log: {log}" +
                                        (log == sessionLog ? " (session that produced the dump)" : " (newest)"));
                        }
                        if (logsToCopy.Count == 0)
                        {
                            Logger.Warning($"[LogCollector] No patch logs found for {site.Target.DisplayName}");
                        }

                        // State the pairing in the bundle. Without it the reader
                        // has to re-derive which log belongs to the dump from
                        // timestamps — exactly the work the pairing exists to save.
                        if (newestDump != null)
                        {
                            dumpPairings.Add(sessionLog != null
                                ? $"{site.Target.Id}: {Path.GetFileName(newestDump)}  <-  {Path.GetFileName(sessionLog)}"
                                : $"{site.Target.Id}: {Path.GetFileName(newestDump)}  <-  (no patch log covers this " +
                                  "crash — the mod may not have been loaded for it, or its log was deleted)");
                        }

                        if (newestDump != null)
                        {
                            string dumpDest = Path.Combine(staging, $"{site.Target.Id}-{Path.GetFileName(newestDump)}");
                            // Strip the dump down to what triage uses (the game
                            // executable + our DLLs, thread stacks, referenced
                            // heap, contexts, module list), dropping stock
                            // system/driver module code+data. Measured on live
                            // dumps: 173 MB -> 11 MB (K1), 293 MB -> 14 MB (K2),
                            // and both together 7-zip to 4 MB, so even a
                            // two-crash bundle fits Discord without a file host.
                            // Any structural surprise throws — we then bundle
                            // the full dump so we never ship nothing.
                            try
                            {
                                var st = MinidumpStripper.StripFile(newestDump, dumpDest);
                                Logger.Info($"[LogCollector] Stripped crash dump {newestDump}: " +
                                    $"{st.OriginalBytes / 1048576.0:F1} MB -> {st.StrippedBytes / 1048576.0:F1} MB " +
                                    $"(kept {st.KeptRanges} ranges, dropped {st.DroppedRanges})");
                            }
                            catch (Exception ex)
                            {
                                Logger.Warning($"[LogCollector] Dump strip failed ({ex.Message}); bundling full dump");
                                File.Copy(newestDump, dumpDest, overwrite: true);
                            }
                            result.DumpCount++;
                        }
                        else
                        {
                            Logger.Info($"[LogCollector] No {site.Target.ExeName} crash dumps found");
                        }
                    }

                    string installerLog = Logger.GetLogPath();
                    if (File.Exists(installerLog))
                    {
                        File.Copy(installerLog, Path.Combine(staging, Path.GetFileName(installerLog)), overwrite: true);
                        result.IncludedInstallerLog = true;
                    }

                    WriteSystemInfo(Path.Combine(staging, "system-info.txt"), sites, dumpPairings);

                    if (result.LogCount == 0 && result.DumpCount == 0 && !result.IncludedInstallerLog)
                    {
                        result.Error = "No logs or crash dumps were found to collect.";
                        return result;
                    }

                    // Crash dumps are the bulk of the bundle (a real swkotor
                    // minidump runs ~150 MB and only deflates to ~67 MB), so we
                    // pack with LZMA2 via the bundled 7zr.exe — that lands the
                    // same dump at ~46 MB. .NET's ZipArchive can only write
                    // Deflate, hence shelling out. If 7z fails for any reason
                    // (AV quarantine of 7zr.exe, etc.) we fall back to a plain
                    // Deflate .zip so the feature never produces nothing.
                    string archivePath = CompressWith7z(staging, downloadsDir, stamp);
                    if (archivePath == null)
                        archivePath = CompressWithZip(staging, downloadsDir, stamp);

                    result.ArchivePath = archivePath;
                    result.Success = true;
                    Logger.Info($"[LogCollector] Wrote {archivePath}");
                    return result;
                }
                finally
                {
                    try { Directory.Delete(staging, recursive: true); }
                    catch (Exception ex) { Logger.Warning($"[LogCollector] Could not clean staging dir: {ex.Message}"); }
                }
            }
            catch (Exception ex)
            {
                Logger.Error("[LogCollector] Collection failed", ex);
                result.Error = ex.Message;
                return result;
            }
        }

        /// <summary>
        /// Open Explorer with the produced archive selected, so the user lands
        /// on it ready to attach to an email / Discord message.
        /// </summary>
        public static void RevealInExplorer(string archivePath)
        {
            try
            {
                var psi = new ProcessStartInfo("explorer.exe", $"/select,\"{archivePath}\"")
                {
                    UseShellExecute = true
                };
                Process.Start(psi);
            }
            catch (Exception ex)
            {
                Logger.Warning($"[LogCollector] Could not open Explorer: {ex.Message}");
            }
        }

        /// <summary>
        /// Compress every file in <paramref name="staging"/> into a single .7z
        /// (LZMA2, max level) in Downloads using the bundled 7zr.exe. Returns
        /// the archive path on success, or null if 7z compression failed — the
        /// caller then falls back to a plain Deflate .zip.
        /// </summary>
        private static string CompressWith7z(string staging, string downloadsDir, string stamp)
        {
            string sevenZrPath = null;
            try
            {
                // Extract 7zr.exe next to (not inside) the staging dir so the
                // archive wildcard never sweeps it in.
                sevenZrPath = Path.Combine(Path.GetTempPath(), $"7zr_{stamp}.exe");
                ExtractResource("7zr.exe", sevenZrPath);

                string archivePath = Path.Combine(downloadsDir, $"KotorAccessibility-Logs-{stamp}.7z");
                if (File.Exists(archivePath)) File.Delete(archivePath);

                // a       add to archive
                // -t7z    7z format
                // -m0=LZMA2 -mx=5  LZMA2 at the "normal" level. On a real
                //         ~158 MB dump this lands ~44.6 MB in ~27 s; -mx=9 only
                //         shaves ~1.3 MB but doubles the time to ~55 s, and the
                //         collect runs synchronously with no progress window —
                //         less wait = less dead air for a screen-reader user.
                //         (-mmt gave no speedup: the dump packs as one solid
                //         stream.)
                // -bso0 -bse0  silence stdout/stderr banners
                // -y      assume yes
                // *       all staged files (7zr does its own wildcard expansion);
                //         WorkingDirectory=staging stores them with flat names.
                var psi = new ProcessStartInfo(sevenZrPath)
                {
                    WorkingDirectory = staging,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                };
                psi.ArgumentList.Add("a");
                psi.ArgumentList.Add("-t7z");
                psi.ArgumentList.Add("-m0=LZMA2");
                psi.ArgumentList.Add("-mx=5");
                psi.ArgumentList.Add("-bso0");
                psi.ArgumentList.Add("-bse0");
                psi.ArgumentList.Add("-y");
                psi.ArgumentList.Add(archivePath);
                psi.ArgumentList.Add("*");

                using var proc = Process.Start(psi);
                if (proc == null)
                {
                    Logger.Warning("[LogCollector] 7zr.exe did not start; falling back to zip");
                    return null;
                }
                string stderr = proc.StandardError.ReadToEnd();
                proc.StandardOutput.ReadToEnd();
                proc.WaitForExit();

                if (proc.ExitCode != 0 || !File.Exists(archivePath))
                {
                    Logger.Warning($"[LogCollector] 7zr.exe exited {proc.ExitCode}; falling back to zip. {stderr}");
                    return null;
                }
                return archivePath;
            }
            catch (Exception ex)
            {
                Logger.Warning($"[LogCollector] 7z compression failed ({ex.Message}); falling back to zip");
                return null;
            }
            finally
            {
                if (sevenZrPath != null)
                {
                    try { if (File.Exists(sevenZrPath)) File.Delete(sevenZrPath); }
                    catch (Exception ex) { Logger.Warning($"[LogCollector] Could not delete temp 7zr.exe: {ex.Message}"); }
                }
            }
        }

        /// <summary>
        /// Plain Deflate .zip fallback (and the original behaviour) for when
        /// the bundled 7zr.exe cannot run on the user's machine.
        /// </summary>
        private static string CompressWithZip(string staging, string downloadsDir, string stamp)
        {
            string zipPath = Path.Combine(downloadsDir, $"KotorAccessibility-Logs-{stamp}.zip");
            if (File.Exists(zipPath)) File.Delete(zipPath);
            ZipFile.CreateFromDirectory(staging, zipPath, CompressionLevel.Optimal, includeBaseDirectory: false);
            return zipPath;
        }

        private static void ExtractResource(string resourceShortName, string targetPath)
        {
            var assembly = Assembly.GetExecutingAssembly();
            string fullName = null;
            foreach (var name in assembly.GetManifestResourceNames())
            {
                if (name.EndsWith(resourceShortName, StringComparison.OrdinalIgnoreCase))
                {
                    fullName = name;
                    break;
                }
            }
            if (fullName == null)
                throw new FileNotFoundException($"Embedded resource not found: {resourceShortName}");

            using var stream = assembly.GetManifestResourceStream(fullName)
                ?? throw new InvalidOperationException($"Could not open resource stream: {fullName}");
            using var fileStream = new FileStream(targetPath, FileMode.Create, FileAccess.Write);
            stream.CopyTo(fileStream);
        }

        /// <summary>
        /// Every game with a real install on this machine. Games without the
        /// mod are included too — they simply have no patch logs, and their
        /// line in system-info.txt ("present, not modded") is an answer worth
        /// having in a bug report.
        /// </summary>
        private static List<GameSite> DiscoverGames(GameTarget preferred, string preferredPath)
        {
            var sites = new List<GameSite>();
            foreach (var t in GameTarget.All)
            {
                // The caller already resolved one game's path and may have got
                // it from an explicit command-line argument that detection
                // cannot reproduce, so for that game its answer wins.
                string path = (t == preferred && GamePathDetector.IsValidGamePath(t, preferredPath))
                    ? preferredPath
                    : GamePathDetector.Detect(t);
                if (!GamePathDetector.IsValidGamePath(t, path)) continue;
                if (sites.Any(s => string.Equals(s.Path, path, StringComparison.OrdinalIgnoreCase))) continue;
                sites.Add(new GameSite { Target = t, Path = path });
            }

            // Last resort: a caller-supplied folder that holds logs but no
            // executable (a moved or partially removed install). The old
            // single-game path collected from it without validating, so
            // refusing here would lose bundles that used to work.
            if (sites.Count == 0 && !string.IsNullOrEmpty(preferredPath) && Directory.Exists(preferredPath))
                sites.Add(new GameSite { Target = preferred, Path = preferredPath });

            return sites;
        }

        private static string FindNewestPatchLog(string gamePath)
        {
            string logsDir = Path.Combine(gamePath, "logs");
            if (!Directory.Exists(logsDir)) return null;
            return Directory.EnumerateFiles(logsDir, "patch-*.log")
                .Select(p => new FileInfo(p))
                .OrderByDescending(f => f.LastWriteTimeUtc)
                .FirstOrDefault()?.FullName;
        }

        /// <summary>
        /// Session start encoded in a patch log's file name, as UTC. acclog names
        /// them <c>patch-yyyyMMdd-HHmmss.log</c> off the UTC clock — the same
        /// clock as <c>FileInfo.LastWriteTimeUtc</c> and WER's dump timestamps,
        /// so all three compare directly with no timezone handling. Returns null
        /// for a name that doesn't parse (a hand-renamed or copied file).
        /// </summary>
        private static DateTime? ParsePatchLogStartUtc(string path)
        {
            const string prefix = "patch-";
            string name = Path.GetFileNameWithoutExtension(path);
            if (name == null || !name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) return null;

            if (!DateTime.TryParseExact(
                    name.Substring(prefix.Length),
                    "yyyyMMdd-HHmmss",
                    System.Globalization.CultureInfo.InvariantCulture,
                    System.Globalization.DateTimeStyles.AssumeUniversal |
                    System.Globalization.DateTimeStyles.AdjustToUniversal,
                    out DateTime parsed))
            {
                return null;
            }
            return parsed;
        }

        /// <summary>
        /// The patch log for the session that produced the dump written at
        /// <paramref name="dumpUtc"/>: the newest log that had already STARTED
        /// when the dump landed.
        ///
        /// <para>Matching on start time rather than on an overlap test is
        /// deliberate. A crashing session's log stops being written the instant
        /// the process dies, and WER finishes writing the dump a few seconds
        /// later — so the log's last-write time is slightly BEFORE the dump's,
        /// and "does this log's span contain the crash?" rejects the very log we
        /// want. Start time has no such race: every later session started after
        /// the crash, and every earlier one started before the one we want.</para>
        ///
        /// <para>Returns null when every log started after the dump (an old dump
        /// with newer sessions on top, or logs since deleted). Falls back to the
        /// newest log when no name parses at all.</para>
        /// </summary>
        private static string FindPatchLogForSession(string gamePath, DateTime dumpUtc)
        {
            string logsDir = Path.Combine(gamePath, "logs");
            if (!Directory.Exists(logsDir)) return null;

            var dated = Directory.EnumerateFiles(logsDir, "patch-*.log")
                .Select(p => new { Path = p, Start = ParsePatchLogStartUtc(p) })
                .Where(x => x.Start.HasValue)
                .ToList();
            if (dated.Count == 0) return FindNewestPatchLog(gamePath);

            return dated
                .Where(x => x.Start.Value <= dumpUtc)
                .OrderByDescending(x => x.Start.Value)
                .FirstOrDefault()?.Path;
        }

        private static string DumpsDir => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "CrashDumps");

        /// <summary>
        /// WER names its dumps <c>&lt;image name&gt;.&lt;pid&gt;.dmp</c>, so the
        /// executable name is the game filter. Anchoring on the full name
        /// matters: the old <c>swkotor*.dmp</c> pattern also matched
        /// <c>swkotor2.exe.*.dmp</c>, which is how a KOTOR 2 dump could end up
        /// in a bundle labelled KOTOR 1.
        /// </summary>
        private static string FindNewestCrashDump(GameTarget target)
        {
            if (!Directory.Exists(DumpsDir)) return null;
            return Directory.EnumerateFiles(DumpsDir, $"{target.ExeName}*.dmp")
                .Select(p => new FileInfo(p))
                .OrderByDescending(f => f.LastWriteTimeUtc)
                .FirstOrDefault()?.FullName;
        }

        private static string GetDownloadsDir()
        {
            // .NET's SpecialFolder enum has no Downloads entry. The user-profile
            // fallback covers the default case; users who have relocated
            // Downloads will still find the zip via the path shown in the
            // success dialog.
            string profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            return Path.Combine(profile, "Downloads");
        }

        private static void WriteSystemInfo(string path, List<GameSite> sites, List<string> dumpPairings)
        {
            var sb = new StringBuilder();
            sb.AppendLine("Voice of the Old Republic — beta-test log bundle");
            sb.AppendLine($"Captured: {DateTime.Now:yyyy-MM-dd HH:mm:ss zzz}");
            sb.AppendLine();
            sb.AppendLine($"Games covered:        " +
                (sites.Count == 0 ? "(none found)" : string.Join(", ", sites.Select(s => s.Target.DisplayName))));
            sb.AppendLine($"OS:                   {Environment.OSVersion}");
            sb.AppendLine($"CLR:                  {Environment.Version}");
            sb.AppendLine($"64-bit OS:            {Environment.Is64BitOperatingSystem}");
            sb.AppendLine($"Locale (UI):          {System.Globalization.CultureInfo.CurrentUICulture.Name}");
            // Which mod pins were in play: a report of "the mod download failed"
            // reads very differently depending on whether the pins were the
            // embedded ones or a refreshed remote set.
            sb.AppendLine($"Mod source pins:      {SourcePins.Origin}");
            sb.AppendLine($"Crash-dump folder:    {DumpsDir}");
            if (!Directory.Exists(DumpsDir))
                sb.AppendLine("(crash-dump folder does not exist — WER may not have captured anything yet)");
            sb.AppendLine();

            foreach (var t in GameTarget.All)
                sb.AppendLine($"WER LocalDumps for {t.ExeName}: {(WerLocalDumps.IsEnabled(t) ? "ENABLED" : "NOT ENABLED")}");

            // Which bundled log belongs to which bundled dump. The newest log is
            // usually NOT the crashing one — the player relaunches after a crash —
            // so this pairing is stated rather than left to be re-derived from
            // timestamps by whoever opens the archive.
            if (dumpPairings != null && dumpPairings.Count > 0)
            {
                sb.AppendLine();
                sb.AppendLine("Crash dump -> log of the session that produced it:");
                foreach (string pairing in dumpPairings)
                    sb.AppendLine($"  {pairing}");
                sb.AppendLine("(any second log per game is that install's newest session, for current state)");
            }

            // One block per game. Both games' recent-file lists are here even
            // when only one of them crashed, because "KOTOR 2 has no logs at
            // all" and "KOTOR 2 ran fine" look identical from the archive's
            // file list alone.
            foreach (var site in sites)
            {
                sb.AppendLine();
                sb.AppendLine($"=== {site.Target.DisplayName} ({site.Target.Id}) ===");
                sb.AppendLine($"Installed mod version: {RegistryManager.GetRegisteredVersion(site.Target) ?? "(unknown)"}");
                sb.AppendLine($"Game install path:    {site.Path}");
                sb.AppendLine($"Game exe present:     {File.Exists(Path.Combine(site.Path, site.Target.ExeName))}");
                sb.AppendLine($"Mod DLL present:      " +
                    File.Exists(Path.Combine(site.Path, "patches", "accessibility.dll")));

                if (Directory.Exists(DumpsDir))
                {
                    var dumps = Directory.EnumerateFiles(DumpsDir, $"{site.Target.ExeName}*.dmp")
                        .Select(p => new FileInfo(p))
                        .OrderByDescending(f => f.LastWriteTimeUtc)
                        .Take(10)
                        .ToList();
                    sb.AppendLine($"Recent {site.Target.ExeName} dumps ({dumps.Count}):");
                    foreach (var d in dumps)
                        sb.AppendLine($"  {d.LastWriteTimeUtc:yyyy-MM-ddTHH:mm:ssZ}  {d.Length,12:N0}  {d.Name}");
                }

                string logsDir = Path.Combine(site.Path, "logs");
                sb.AppendLine($"Patch-log folder:     {logsDir}");
                if (Directory.Exists(logsDir))
                {
                    var logs = Directory.EnumerateFiles(logsDir, "patch-*.log")
                        .Select(p => new FileInfo(p))
                        .OrderByDescending(f => f.LastWriteTimeUtc)
                        .Take(10)
                        .ToList();
                    sb.AppendLine($"Recent patch logs ({logs.Count}):");
                    foreach (var l in logs)
                        sb.AppendLine($"  {l.LastWriteTimeUtc:yyyy-MM-ddTHH:mm:ssZ}  {l.Length,12:N0}  {l.Name}");
                }
            }

            File.WriteAllText(path, sb.ToString(), Encoding.UTF8);
        }
    }

    /// <summary>
    /// Configures Windows Error Reporting to drop a minidump under
    /// %LOCALAPPDATA%\CrashDumps when the game crashes. Without this, beta
    /// testers' machines will not capture any .dmp file and the LogCollector
    /// has nothing to bundle. Idempotent; requires elevation (the installer
    /// already runs as admin).
    ///
    /// <para>WER keys this per executable name, so both games need their own —
    /// a key for swkotor.exe captures nothing when swkotor2.exe faults. The
    /// no-argument overloads cover both, since the install path enables dump
    /// capture before it knows which games the user picked.</para>
    /// </summary>
    public static class WerLocalDumps
    {
        private const string KeyRoot =
            @"SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps";

        private static string KeyPathFor(GameTarget target) => $@"{KeyRoot}\{target.ExeName}";

        /// <summary>True when dump capture is configured for EVERY game.</summary>
        public static bool IsEnabled()
        {
            foreach (var target in GameTarget.All)
                if (!IsEnabled(target)) return false;
            return true;
        }

        public static bool IsEnabled(GameTarget target)
        {
            try
            {
                using var key = Registry.LocalMachine.OpenSubKey(KeyPathFor(target));
                return key != null;
            }
            catch { return false; }
        }

        /// <summary>Enable dump capture for every game. True only if all succeeded.</summary>
        public static bool Enable()
        {
            bool all = true;
            foreach (var target in GameTarget.All)
                all &= Enable(target);
            return all;
        }

        /// <summary>
        /// Remove dump capture for one game's executable. Uninstall path: the
        /// key is ours, so leaving it would keep Windows writing minidumps for a
        /// game the mod is no longer part of.
        /// </summary>
        public static bool Disable(GameTarget target)
        {
            try
            {
                using var parent = Registry.LocalMachine.OpenSubKey(KeyRoot, writable: true);
                if (parent == null) return true;   // never configured
                if (parent.OpenSubKey(target.ExeName) == null) return true;

                parent.DeleteSubKeyTree(target.ExeName);
                Logger.Info($"[WerLocalDumps] Removed crash-dump capture for {target.ExeName}");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Warning($"[WerLocalDumps] Could not remove capture for {target.ExeName}: {ex.Message}");
                return false;
            }
        }

        public static bool Enable(GameTarget target)
        {
            try
            {
                using var key = Registry.LocalMachine.CreateSubKey(KeyPathFor(target), writable: true);
                if (key == null) return false;
                key.SetValue("DumpFolder", @"%LOCALAPPDATA%\CrashDumps", RegistryValueKind.ExpandString);
                key.SetValue("DumpCount", 10, RegistryValueKind.DWord);
                // Custom dump (DumpType=0) controlled by CustomDumpFlags. Full
                // dumps (DumpType=2) routinely came out at 500+ MB because
                // swkotor.exe maps hundreds of MB of texture/audio/BIF pages —
                // mostly asset buffers we never read during triage. The flag
                // set below keeps everything kdev analyze-dump actually uses:
                //   0x0001 MiniDumpWithDataSegs                 — globals
                //   0x0040 MiniDumpWithIndirectlyReferencedMemory — heap pages
                //          pointed at from stack/registers (so "what's at the
                //          freed slot" forensics still work; e.g. the 1f7cd0f
                //          SaveLoad UAF was diagnosed from combat-log strings
                //          sitting at panel+0x20)
                //   0x0100 MiniDumpWithProcessThreadData        — process + per-thread state
                //   0x2000 MiniDumpWithCodeSegs                 — unpacked .text;
                //          on-disk swkotor.exe is packed, so --peek of runtime-
                //          decrypted instructions needs the in-memory image
                // Typical size: ~15-50 MB vs ~500 MB+ for the full variant.
                key.SetValue("DumpType", 0, RegistryValueKind.DWord);
                key.SetValue("CustomDumpFlags", 0x2141, RegistryValueKind.DWord);
                Logger.Info($"[WerLocalDumps] Enabled crash-dump capture for {target.ExeName}");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Warning($"[WerLocalDumps] Could not enable for {target.ExeName}: {ex.Message}");
                return false;
            }
        }
    }
}
