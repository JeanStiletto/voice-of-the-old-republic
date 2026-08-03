using System;
using System.IO;
using Microsoft.Win32;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Manages Windows registry entries for Add/Remove Programs.
    ///
    /// <para>One entry per game (<see cref="GameTarget.RegistryKeyName"/>). The
    /// two are separate installs in separate folders that a user can add and
    /// remove independently, and a single shared entry could only ever record
    /// one InstallLocation — which is also what made an installed KOTOR 2
    /// invisible to a later run of the installer.</para>
    /// </summary>
    public static class RegistryManager
    {
        private const string UninstallKeyPath = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall";

        public static void Register(GameTarget target, string installPath, string version, string uninstallerPath = null)
        {
            try
            {
                Logger.Info($"Registering {target.DisplayName} in Add/Remove Programs...");
                string uninstallKeyFullPath = $@"{UninstallKeyPath}\{target.RegistryKeyName}";

                using (var key = Registry.LocalMachine.CreateSubKey(uninstallKeyFullPath))
                {
                    if (key == null)
                    {
                        Logger.Warning("Could not create registry key - may need admin rights");
                        return;
                    }

                    string installerPath = !string.IsNullOrEmpty(uninstallerPath) && File.Exists(uninstallerPath)
                        ? uninstallerPath
                        : (Environment.ProcessPath ?? string.Empty);

                    // Both entries appear in the same Add/Remove Programs list,
                    // so the display name has to say which game it removes.
                    key.SetValue("DisplayName", $"{Config.DisplayName} ({target.DisplayName})");
                    key.SetValue("DisplayVersion", version ?? "1.0.0");
                    key.SetValue("Publisher", Config.Publisher);
                    key.SetValue("InstallLocation", installPath);
                    key.SetValue("InstallDate", DateTime.Now.ToString("yyyyMMdd"));

                    // --game is what tells the uninstaller which file set to
                    // remove; the path alone would not distinguish them.
                    key.SetValue("UninstallString",
                        $"\"{installerPath}\" /uninstall --game {target.Id} \"{installPath}\"");
                    key.SetValue("QuietUninstallString",
                        $"\"{installerPath}\" /uninstall --game {target.Id} \"{installPath}\" /quiet");

                    key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                    key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                    key.SetValue("EstimatedSize", 8000, RegistryValueKind.DWord);

                    key.SetValue("URLInfoAbout", Config.ModRepositoryUrl);
                    key.SetValue("HelpLink", Config.ModRepositoryUrl + "/issues");

                    Logger.Info($"Successfully registered {target.DisplayName} in Add/Remove Programs");
                }
            }
            catch (UnauthorizedAccessException ex)
            {
                Logger.Warning($"Could not register in Add/Remove Programs (access denied): {ex.Message}");
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to register in Add/Remove Programs", ex);
            }
        }

        public static void Unregister(GameTarget target)
        {
            try
            {
                Logger.Info($"Removing {target.DisplayName} from Add/Remove Programs...");
                using (var parentKey = Registry.LocalMachine.OpenSubKey(UninstallKeyPath, writable: true))
                {
                    if (parentKey == null)
                    {
                        Logger.Warning("Could not open Uninstall registry key");
                        return;
                    }
                    using (var existingKey = parentKey.OpenSubKey(target.RegistryKeyName))
                    {
                        if (existingKey == null)
                        {
                            Logger.Info("Registry entry does not exist, nothing to remove");
                            return;
                        }
                    }
                    parentKey.DeleteSubKeyTree(target.RegistryKeyName);
                    Logger.Info("Successfully removed from Add/Remove Programs");
                }
            }
            catch (UnauthorizedAccessException ex)
            {
                Logger.Warning($"Could not remove from Add/Remove Programs (access denied): {ex.Message}");
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to remove from Add/Remove Programs", ex);
            }
        }

        public static bool IsRegistered(GameTarget target) => ReadValue(target, "InstallLocation") != null;

        public static string GetRegisteredInstallLocation(GameTarget target) => ReadValue(target, "InstallLocation");

        public static string GetRegisteredVersion(GameTarget target) => ReadValue(target, "DisplayVersion");

        private static string ReadValue(GameTarget target, string valueName)
        {
            try
            {
                using (var key = Registry.LocalMachine.OpenSubKey($@"{UninstallKeyPath}\{target.RegistryKeyName}"))
                    return key?.GetValue(valueName) as string;
            }
            catch { return null; }
        }
    }
}
