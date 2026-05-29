using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
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
            public string ZipPath;
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

                    string zipPath = Path.Combine(downloadsDir, $"KotorAccessibility-Logs-{stamp}.zip");
                    if (File.Exists(zipPath)) File.Delete(zipPath);
                    ZipFile.CreateFromDirectory(staging, zipPath, CompressionLevel.Optimal, includeBaseDirectory: false);

                    result.ZipPath = zipPath;
                    result.Success = true;
                    Logger.Info($"[LogCollector] Wrote {zipPath}");
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
        /// Open Explorer with the produced zip selected, so the user lands on
        /// it ready to attach to an email / Discord message.
        /// </summary>
        public static void RevealInExplorer(string zipPath)
        {
            try
            {
                var psi = new ProcessStartInfo("explorer.exe", $"/select,\"{zipPath}\"")
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
                key.SetValue("DumpType", 2, RegistryValueKind.DWord); // 2 = full dump, more useful for triage
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
