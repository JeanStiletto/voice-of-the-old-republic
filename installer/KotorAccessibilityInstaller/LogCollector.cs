using System;
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
    /// Bundles the freshest patch log, crash dump, installer log, and a small
    /// system-info file into a single zip in the user's Downloads folder so
    /// beta testers have a one-file artefact to attach to a bug report.
    /// </summary>
    public static class LogCollector
    {
        public class Result
        {
            public bool Success;
            public string ArchivePath;
            public int LogCount;
            public int DumpCount;
            public bool IncludedInstallerLog;
            public string Error;
        }

        public static Result Collect(string gamePath)
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
                    string newestLog = FindNewestPatchLog(gamePath);
                    if (newestLog != null)
                    {
                        File.Copy(newestLog, Path.Combine(staging, Path.GetFileName(newestLog)), overwrite: true);
                        result.LogCount = 1;
                        Logger.Info($"[LogCollector] Included patch log: {newestLog}");
                    }
                    else
                    {
                        Logger.Warning("[LogCollector] No patch logs found");
                    }

                    string newestDump = FindNewestCrashDump();
                    if (newestDump != null)
                    {
                        File.Copy(newestDump, Path.Combine(staging, Path.GetFileName(newestDump)), overwrite: true);
                        result.DumpCount = 1;
                        Logger.Info($"[LogCollector] Included crash dump: {newestDump}");
                    }
                    else
                    {
                        Logger.Info("[LogCollector] No swkotor crash dumps found");
                    }

                    string installerLog = Logger.GetLogPath();
                    if (File.Exists(installerLog))
                    {
                        File.Copy(installerLog, Path.Combine(staging, Path.GetFileName(installerLog)), overwrite: true);
                        result.IncludedInstallerLog = true;
                    }

                    WriteSystemInfo(Path.Combine(staging, "system-info.txt"), gamePath);

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

        private static string FindNewestPatchLog(string gamePath)
        {
            string logsDir = Path.Combine(gamePath, "logs");
            if (!Directory.Exists(logsDir)) return null;
            return Directory.EnumerateFiles(logsDir, "patch-*.log")
                .Select(p => new FileInfo(p))
                .OrderByDescending(f => f.LastWriteTimeUtc)
                .FirstOrDefault()?.FullName;
        }

        private static string FindNewestCrashDump()
        {
            string dumpsDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "CrashDumps");
            if (!Directory.Exists(dumpsDir)) return null;
            return Directory.EnumerateFiles(dumpsDir, "swkotor*.dmp")
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

        private static void WriteSystemInfo(string path, string gamePath)
        {
            var sb = new StringBuilder();
            sb.AppendLine("Voice of the Old Republic — beta-test log bundle");
            sb.AppendLine($"Captured: {DateTime.Now:yyyy-MM-dd HH:mm:ss zzz}");
            sb.AppendLine();
            sb.AppendLine($"Installed mod version: {RegistryManager.GetRegisteredVersion() ?? "(unknown)"}");
            sb.AppendLine($"Game install path:    {gamePath}");
            sb.AppendLine($"Game exe present:     {File.Exists(Path.Combine(gamePath, "swkotor.exe"))}");
            sb.AppendLine($"OS:                   {Environment.OSVersion}");
            sb.AppendLine($"CLR:                  {Environment.Version}");
            sb.AppendLine($"64-bit OS:            {Environment.Is64BitOperatingSystem}");
            sb.AppendLine($"Locale (UI):          {System.Globalization.CultureInfo.CurrentUICulture.Name}");
            sb.AppendLine();

            string dumpsDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "CrashDumps");
            sb.AppendLine($"Crash-dump folder:    {dumpsDir}");
            if (Directory.Exists(dumpsDir))
            {
                var dumps = Directory.EnumerateFiles(dumpsDir, "swkotor*.dmp")
                    .Select(p => new FileInfo(p))
                    .OrderByDescending(f => f.LastWriteTimeUtc)
                    .Take(10)
                    .ToList();
                sb.AppendLine($"Recent swkotor dumps ({dumps.Count}):");
                foreach (var d in dumps)
                    sb.AppendLine($"  {d.LastWriteTimeUtc:yyyy-MM-ddTHH:mm:ssZ}  {d.Length,12:N0}  {d.Name}");
            }
            else
            {
                sb.AppendLine("(crash-dump folder does not exist — WER may not have captured anything yet)");
            }
            sb.AppendLine();

            string logsDir = Path.Combine(gamePath, "logs");
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
            sb.AppendLine();

            sb.AppendLine($"WER LocalDumps for swkotor.exe: {(WerLocalDumps.IsEnabled() ? "ENABLED" : "NOT ENABLED")}");

            File.WriteAllText(path, sb.ToString(), Encoding.UTF8);
        }
    }

    /// <summary>
    /// Configures Windows Error Reporting to drop a minidump under
    /// %LOCALAPPDATA%\CrashDumps when swkotor.exe crashes. Without this, beta
    /// testers' machines will not capture any .dmp file and the LogCollector
    /// has nothing to bundle. Idempotent; requires elevation (the installer
    /// already runs as admin).
    /// </summary>
    public static class WerLocalDumps
    {
        private const string KeyPath =
            @"SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\swkotor.exe";

        public static bool IsEnabled()
        {
            try
            {
                using var key = Registry.LocalMachine.OpenSubKey(KeyPath);
                return key != null;
            }
            catch { return false; }
        }

        public static bool Enable()
        {
            try
            {
                using var key = Registry.LocalMachine.CreateSubKey(KeyPath, writable: true);
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
                Logger.Info("[WerLocalDumps] Enabled crash-dump capture for swkotor.exe");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Warning($"[WerLocalDumps] Could not enable: {ex.Message}");
                return false;
            }
        }
    }
}
