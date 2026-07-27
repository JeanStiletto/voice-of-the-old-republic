using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Principal;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;

namespace KotorAccessibilityInstaller
{
    public enum UpdateChoice
    {
        Close,
        UpdateOnly,
        FullInstall,
        ToggleSpatialAudio,
        CollectLogs
    }

    static class Program
    {

        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            Logger.Info("Voice of the Old Republic Installer starting...");
            Logger.Info($"Running as: {Environment.UserName}");
            Logger.Info($"Is Admin: {GamePathDetector.IsRunningAsAdmin()}");
            Logger.Info($"Arguments: {string.Join(" ", args)}");

            InstallerLocale.Initialize(LanguageDetector.DetectLanguage());

            if (!GamePathDetector.IsRunningAsAdmin())
            {
                Logger.Error("Not running as administrator");
                MessageBox.Show(
                    InstallerLocale.Get("Program_AdminRequired_Text"),
                    InstallerLocale.Get("Program_AdminRequired_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                return;
            }

            if (GamePathDetector.IsGameRunning())
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
            bool autoUpdateMode = false;
            string pathArg = null;
            string localKpatchPath = null;

            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i].ToLowerInvariant();
                if (arg == "/uninstall" || arg == "-uninstall" || arg == "--uninstall") uninstallMode = true;
                else if (arg == "/quiet" || arg == "-quiet" || arg == "--quiet" || arg == "/q" || arg == "-q") quietMode = true;
                else if (arg == "/auto-update" || arg == "-auto-update" || arg == "--auto-update") autoUpdateMode = true;
                else if (arg == "--local-kpatch" && i + 1 < args.Length) localKpatchPath = args[++i];
                else if (!arg.StartsWith("/") && !arg.StartsWith("-")) pathArg = args[i];
            }

            if (localKpatchPath != null)
            {
                if (!File.Exists(localKpatchPath))
                {
                    Logger.Error($"--local-kpatch path does not exist: {localKpatchPath}");
                    MessageBox.Show(
                        $"--local-kpatch path does not exist:\n{localKpatchPath}",
                        "Installer (dev)",
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Error);
                    return;
                }
                Logger.Info($"DEV: --local-kpatch override active; using {localKpatchPath} instead of GitHub release");
            }

            if (uninstallMode)
            {
                Logger.Info("Running in uninstall mode");
                string gamePath = pathArg ?? RegistryManager.GetRegisteredInstallLocation() ?? GamePathDetector.DetectGamePath();

                if (string.IsNullOrEmpty(gamePath) || !GamePathDetector.IsValidGamePath(gamePath))
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
                    UninstallFlow.PerformUninstall(gamePath);
                }
                else
                {
                    Application.Run(new UninstallForm(gamePath));
                }
                return;
            }

            // Install mode
            string detectedGamePath = pathArg ?? GamePathDetector.DetectGamePath() ?? GamePathDetector.DefaultGamePath;
            string installedModPath = Path.Combine(detectedGamePath, "patches", "accessibility.dll");
            bool modExists = File.Exists(installedModPath);

            // --auto-update: in-game F5 updater handoff. Skips every Welcome /
            // ModSelection / UpdateAvailable / InstalledOptions dialog and runs
            // MainForm directly in headless update-only mode. Exit code is 0 on
            // success, 1 on failure (the caller batch reads it). This path
            // assumes the mod is already installed; the install-only path is
            // out of scope here because the in-game updater only fires when an
            // existing patch DLL has loaded.
            if (autoUpdateMode)
            {
                Logger.Info("Auto-update mode active — running headless update");
                if (!modExists)
                {
                    Logger.Error($"--auto-update invoked but no installed mod found at {installedModPath}");
                    Environment.ExitCode = 1;
                    return;
                }
                // Re-apply install-time settings the running installer ships
                // with. Without this any installer-side config change between
                // versions (WER dump flags, registry tweaks, future helpers)
                // would only land on a manual reinstall, since the auto-update
                // skips the welcome / mod-selection path that normally calls
                // these. Idempotent + best-effort — failure does not block the
                // update.
                WerLocalDumps.Enable();
                Application.Run(new MainForm(detectedGamePath, updateOnly: true, localKpatchPath: localKpatchPath, headless: true));
                return;
            }
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
                bool spatialOn = SpatialAudioManager.IsEnabled(detectedGamePath);
                var updateForm = new UpdateAvailableForm(installedVersion, latestVersion, spatialOn);
                Application.Run(updateForm);

                switch (updateForm.UserChoice)
                {
                    case UpdateChoice.UpdateOnly:
                        Logger.Info("User chose to update mod only");
                        Application.Run(new MainForm(detectedGamePath, updateOnly: true, language: LanguageDetector.DetectLanguage(), localKpatchPath: localKpatchPath));
                        break;

                    case UpdateChoice.FullInstall:
                        Logger.Info("User chose full install");
                        InstallFlow.RunFullInstallFlow(detectedGamePath, pathArg, latestVersion, localKpatchPath);
                        break;

                    case UpdateChoice.ToggleSpatialAudio:
                        Logger.Info("User chose to toggle spatial audio");
                        InstallFlow.ToggleSpatialAudioAndReport(detectedGamePath);
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

                bool spatialOn = SpatialAudioManager.IsEnabled(detectedGamePath);
                var form = new InstalledOptionsForm(displayVersion, spatialOn);
                Application.Run(form);

                switch (form.UserChoice)
                {
                    case UpdateChoice.FullInstall:
                        Logger.Info("User chose full reinstall");
                        InstallFlow.RunFullInstallFlow(detectedGamePath, pathArg, latestVersion, localKpatchPath);
                        break;

                    case UpdateChoice.ToggleSpatialAudio:
                        Logger.Info("User chose to toggle spatial audio");
                        InstallFlow.ToggleSpatialAudioAndReport(detectedGamePath);
                        break;

                    case UpdateChoice.CollectLogs:
                        Logger.Info("User chose to collect logs for beta-test bundle");
                        InstallFlow.CollectLogsAndReport(detectedGamePath);
                        break;

                    case UpdateChoice.Close:
                    default:
                        Logger.Info("User closed installed-options dialog");
                        break;
                }
            }
            else
            {
                InstallFlow.RunFullInstallFlow(detectedGamePath, pathArg, latestVersion, localKpatchPath);
            }
        }

        /// <summary>
        /// Welcome → Base-components info → Optional-mods checkboxes → Main install.
        /// Each form can cancel the chain.
        /// </summary>
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
