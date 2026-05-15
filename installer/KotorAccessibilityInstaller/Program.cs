using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Principal;
using System.Windows.Forms;
using Microsoft.Win32;

namespace KotorAccessibilityInstaller
{
    public enum UpdateChoice
    {
        Close,
        UpdateOnly,
        FullInstall
    }

    static class Program
    {
        public const string GameExeName = "swkotor.exe";

        // Steam install — the only supported distribution. GoG/Aspyr are out of scope
        // for the first installer pass; users can run kdev manually if needed.
        public static readonly string DefaultGamePath =
            @"C:\Program Files (x86)\Steam\steamapps\common\swkotor";

        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            Logger.Info("KOTOR Accessibility Installer starting...");
            Logger.Info($"Running as: {Environment.UserName}");
            Logger.Info($"Is Admin: {IsRunningAsAdmin()}");
            Logger.Info($"Arguments: {string.Join(" ", args)}");

            InstallerLocale.Initialize(LanguageDetector.DetectLanguage());

            if (!IsRunningAsAdmin())
            {
                Logger.Error("Not running as administrator");
                MessageBox.Show(
                    InstallerLocale.Get("Program_AdminRequired_Text"),
                    InstallerLocale.Get("Program_AdminRequired_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                return;
            }

            if (IsGameRunning())
            {
                Logger.Warning("KOTOR is currently running");
                MessageBox.Show(
                    InstallerLocale.Get("Program_GameRunning_Text"),
                    InstallerLocale.Get("Program_GameRunning_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Warning);
                return;
            }

            bool uninstallMode = false;
            bool quietMode = false;
            string pathArg = null;

            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i].ToLowerInvariant();
                if (arg == "/uninstall" || arg == "-uninstall" || arg == "--uninstall") uninstallMode = true;
                else if (arg == "/quiet" || arg == "-quiet" || arg == "--quiet" || arg == "/q" || arg == "-q") quietMode = true;
                else if (!arg.StartsWith("/") && !arg.StartsWith("-")) pathArg = args[i];
            }

            if (uninstallMode)
            {
                Logger.Info("Running in uninstall mode");
                string gamePath = pathArg ?? RegistryManager.GetRegisteredInstallLocation() ?? DetectGamePath();

                if (string.IsNullOrEmpty(gamePath) || !IsValidGamePath(gamePath))
                {
                    if (!quietMode)
                    {
                        MessageBox.Show(
                            InstallerLocale.Get("Program_UninstallError_Text"),
                            InstallerLocale.Get("Program_UninstallError_Title"),
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Error);
                    }
                    Logger.Error("Uninstall failed: Could not determine KOTOR path");
                    return;
                }

                if (quietMode)
                {
                    PerformUninstall(gamePath);
                }
                else
                {
                    Application.Run(new UninstallForm(gamePath));
                }
                return;
            }

            // Install mode
            string detectedGamePath = pathArg ?? DetectGamePath() ?? DefaultGamePath;
            string installedModPath = Path.Combine(detectedGamePath, "patches", "accessibility.dll");
            bool modExists = File.Exists(installedModPath);
            bool updateAvailable = false;
            string installedVersion = null;
            string latestVersion = null;

            try
            {
                using (var client = new GitHubClient())
                {
                    latestVersion = client.GetLatestModVersionAsync(Config.ModRepositoryUrl).GetAwaiter().GetResult();
                    Logger.Info($"Latest version: {latestVersion}");
                }
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not check for updates: {ex.Message}");
            }

            if (modExists)
            {
                installedVersion = RegistryManager.GetRegisteredVersion();
                Logger.Info($"Installed version (from registry): {installedVersion ?? "unknown"}");

                if (installedVersion != null && latestVersion != null)
                {
                    updateAvailable = IsNewerVersion(latestVersion, installedVersion);
                    Logger.Info($"Update available: {updateAvailable}");
                }
            }

            if (modExists && updateAvailable)
            {
                Logger.Info("Showing update dialog");
                var updateForm = new UpdateAvailableForm(installedVersion, latestVersion);
                Application.Run(updateForm);

                switch (updateForm.UserChoice)
                {
                    case UpdateChoice.UpdateOnly:
                        Logger.Info("User chose to update mod only");
                        Application.Run(new MainForm(detectedGamePath, updateOnly: true));
                        break;

                    case UpdateChoice.FullInstall:
                        Logger.Info("User chose full install");
                        RunFullInstallFlow(detectedGamePath, pathArg, latestVersion);
                        break;

                    case UpdateChoice.Close:
                    default:
                        Logger.Info("User cancelled from update dialog");
                        break;
                }
            }
            else if (modExists)
            {
                Logger.Info($"Mod is up to date (installed: {installedVersion ?? "unknown"})");
                string displayVersion = installedVersion ?? "?";
                while (displayVersion.EndsWith(".0") && displayVersion.IndexOf('.') != displayVersion.LastIndexOf('.'))
                    displayVersion = displayVersion.Substring(0, displayVersion.Length - 2);

                var result = MessageBox.Show(
                    InstallerLocale.Format("Program_UpToDate_Format", displayVersion),
                    InstallerLocale.Get("Program_UpToDate_Title"),
                    MessageBoxButtons.YesNo,
                    MessageBoxIcon.Information);

                if (result != DialogResult.Yes) return;

                Logger.Info("User chose full reinstall");
                RunFullInstallFlow(detectedGamePath, pathArg, latestVersion);
            }
            else
            {
                RunFullInstallFlow(detectedGamePath, pathArg, latestVersion);
            }
        }

        /// <summary>
        /// Welcome → Modding-info screen → Main install. Each form can cancel the chain.
        /// </summary>
        private static void RunFullInstallFlow(string gamePath, string pathArgOverride, string latestVersion)
        {
            var welcomeForm = new WelcomeForm { LatestModVersion = latestVersion };
            Application.Run(welcomeForm);
            if (!welcomeForm.ProceedWithInstall)
            {
                Logger.Info("Installation cancelled from welcome dialog");
                return;
            }

            // New: modding-info screen between welcome and main install.
            var infoForm = new ModdingInfoForm();
            Application.Run(infoForm);
            if (!infoForm.ProceedWithInstall)
            {
                Logger.Info("Installation cancelled from modding-info screen");
                return;
            }

            string resolvedPath = pathArgOverride ?? DetectGamePath() ?? gamePath;
            Application.Run(new MainForm(resolvedPath, language: welcomeForm.SelectedLanguage));
        }

        public static void PerformUninstall(string gamePath)
        {
            Logger.Info($"Uninstalling from: {gamePath}");

            try
            {
                // Restore swkotor.exe from the most recent install-time backup, if any.
                // Backups land next to swkotor.exe as swkotor.exe.backup.YYYYMMDD_HHMMSS,
                // written by KPatchCore.BackupManager.
                RestoreLatestBackup(gamePath);

                // Remove patcher runtime files from game root.
                string[] runtimeFiles =
                {
                    "KotorPatcher.dll",
                    "sqlite3.dll",
                    "addresses.db",
                    "patch_config.toml"
                };
                foreach (var name in runtimeFiles)
                {
                    string p = Path.Combine(gamePath, name);
                    if (File.Exists(p))
                    {
                        Logger.Info($"Removing {name}...");
                        try { File.Delete(p); } catch (Exception ex) { Logger.Warning($"Could not delete {name}: {ex.Message}"); }
                    }
                }

                // Remove the patches/ folder we put accessibility.dll + prism.dll into.
                string patchesDir = Path.Combine(gamePath, "patches");
                if (Directory.Exists(patchesDir))
                {
                    Logger.Info($"Removing patches folder: {patchesDir}");
                    try { Directory.Delete(patchesDir, recursive: true); }
                    catch (Exception ex) { Logger.Warning($"Could not delete patches/: {ex.Message}"); }
                }

                RegistryManager.Unregister();
                ScheduleUninstallerSelfDelete(gamePath);

                Logger.Info("Uninstallation complete");
                Logger.Flush();
            }
            catch (Exception ex)
            {
                Logger.Error("Uninstallation failed", ex);
                Logger.Flush();
                throw;
            }
        }

        private static void RestoreLatestBackup(string gamePath)
        {
            try
            {
                string exePath = Path.Combine(gamePath, GameExeName);
                var backups = Directory.GetFiles(gamePath, GameExeName + ".backup.*");
                if (backups.Length == 0)
                {
                    Logger.Warning("No backup of swkotor.exe found; leaving current exe in place.");
                    return;
                }

                // KPatchCore writes timestamped backup names. Picking the newest by name
                // (lexicographic == chronological for the YYYYMMDD_HHMMSS format).
                Array.Sort(backups);
                string newest = backups[^1];
                // Skip the sidecar JSON file the backup manager writes (.backup.<ts>.json)
                if (newest.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
                {
                    if (backups.Length < 2) return;
                    newest = backups[^2];
                }

                Logger.Info($"Restoring {GameExeName} from backup: {Path.GetFileName(newest)}");
                File.Copy(newest, exePath, overwrite: true);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not restore swkotor.exe backup: {ex.Message}. " +
                               "Verify game files via Steam if the executable is corrupted.");
            }
        }

        private static void ScheduleUninstallerSelfDelete(string gamePath)
        {
            try
            {
                string uninstallerPath = Path.Combine(gamePath, Config.UninstallerExeName);
                if (!File.Exists(uninstallerPath)) return;

                Logger.Info($"Scheduling deletion of uninstaller: {uninstallerPath}");

                // ping (not timeout) — see docs/installer.md / arena IMPLEMENTATION.md
                // gotcha note: timeout refuses to run without an interactive console.
                var psi = new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    Arguments = $"/c ping 127.0.0.1 -n 5 -w 1000 >nul & del /f /q \"{uninstallerPath}\"",
                    CreateNoWindow = true,
                    UseShellExecute = false,
                    WindowStyle = ProcessWindowStyle.Hidden
                };
                Process.Start(psi);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not schedule uninstaller deletion: {ex.Message}");
            }
        }

        private static bool IsRunningAsAdmin()
        {
            try
            {
                using var identity = WindowsIdentity.GetCurrent();
                var principal = new WindowsPrincipal(identity);
                return principal.IsInRole(WindowsBuiltInRole.Administrator);
            }
            catch { return false; }
        }

        private static bool IsGameRunning()
        {
            try
            {
                var processes = Process.GetProcessesByName("swkotor");
                return processes.Length > 0;
            }
            catch { return false; }
        }

        /// <summary>
        /// Attempts to detect KOTOR's install path. Order:
        /// 1. Registry (previously registered by us)
        /// 2. Steam's per-app registry key (HKLM ...\Steam App 32370)
        /// 3. Common Steam location under Program Files (x86)
        /// 4. CommonObjectives — Default Steam library path
        /// </summary>
        public static string DetectGamePath()
        {
            string registered = RegistryManager.GetRegisteredInstallLocation();
            if (IsValidGamePath(registered)) return registered;

            string steamReg = TryReadSteamAppInstallPath();
            if (IsValidGamePath(steamReg)) return steamReg;

            if (IsValidGamePath(DefaultGamePath)) return DefaultGamePath;

            // Fallback: scan ProgramFiles + ProgramFilesX86 for a swkotor folder.
            foreach (var root in new[]
            {
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)
            })
            {
                if (string.IsNullOrEmpty(root)) continue;
                string candidate = Path.Combine(root, "Steam", "steamapps", "common", "swkotor");
                if (IsValidGamePath(candidate)) return candidate;
            }

            return null;
        }

        private static string TryReadSteamAppInstallPath()
        {
            try
            {
                // KOTOR 1 Steam App ID = 32370. Steam writes InstallLocation here.
                using var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry32)
                    .OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 32370");
                return key?.GetValue("InstallLocation") as string;
            }
            catch { return null; }
        }

        public static bool IsValidGamePath(string path)
        {
            if (string.IsNullOrEmpty(path)) return false;
            return File.Exists(Path.Combine(path, GameExeName));
        }

        internal static bool IsNewerVersion(string latestVersion, string installedVersion)
        {
            try
            {
                var latest = NormalizeVersion(latestVersion);
                var installed = NormalizeVersion(installedVersion);
                Logger.Info($"Version comparison: latest={latest}, installed={installed}");
                return latest > installed;
            }
            catch (Exception ex)
            {
                Logger.Warning($"Version comparison failed: {ex.Message}");
                return !string.Equals(latestVersion, installedVersion, StringComparison.OrdinalIgnoreCase);
            }
        }

        internal static Version NormalizeVersion(string version)
        {
            if (string.IsNullOrEmpty(version)) return new Version(0, 0, 0, 0);
            version = version.TrimStart('v', 'V');

            int dashIndex = version.IndexOf('-');
            if (dashIndex > 0) version = version.Substring(0, dashIndex);

            int spaceIndex = version.IndexOf(' ');
            if (spaceIndex > 0) version = version.Substring(0, spaceIndex);

            string[] parts = version.Trim().Split('.');
            int major = parts.Length > 0 && int.TryParse(parts[0], out int m) ? m : 0;
            int minor = parts.Length > 1 && int.TryParse(parts[1], out int n) ? n : 0;
            int build = parts.Length > 2 && int.TryParse(parts[2], out int b) ? b : 0;
            int revision = parts.Length > 3 && int.TryParse(parts[3], out int r) ? r : 0;

            return new Version(major, minor, build, revision);
        }
    }
}
