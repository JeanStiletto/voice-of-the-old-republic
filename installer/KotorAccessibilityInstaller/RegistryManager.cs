using System;
using System.IO;
using Microsoft.Win32;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Manages Windows registry entries for Add/Remove Programs.
    /// </summary>
    public static class RegistryManager
    {
        private const string UninstallKeyPath = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall";
        private const string AppKeyName = "KotorAccessibility";

        public static void Register(string installPath, string version, string uninstallerPath = null)
        {
            try
            {
                Logger.Info("Registering in Add/Remove Programs...");
                string uninstallKeyFullPath = $@"{UninstallKeyPath}\{AppKeyName}";

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

                    key.SetValue("DisplayName", Config.DisplayName);
                    key.SetValue("DisplayVersion", version ?? "1.0.0");
                    key.SetValue("Publisher", Config.Publisher);
                    key.SetValue("InstallLocation", installPath);
                    key.SetValue("InstallDate", DateTime.Now.ToString("yyyyMMdd"));

                    key.SetValue("UninstallString", $"\"{installerPath}\" /uninstall \"{installPath}\"");
                    key.SetValue("QuietUninstallString", $"\"{installerPath}\" /uninstall \"{installPath}\" /quiet");

                    key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                    key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                    key.SetValue("EstimatedSize", 8000, RegistryValueKind.DWord);

                    key.SetValue("URLInfoAbout", Config.ModRepositoryUrl);
                    key.SetValue("HelpLink", Config.ModRepositoryUrl + "/issues");

                    Logger.Info("Successfully registered in Add/Remove Programs");
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

        public static void Unregister()
        {
            try
            {
                Logger.Info("Removing from Add/Remove Programs...");
                using (var parentKey = Registry.LocalMachine.OpenSubKey(UninstallKeyPath, writable: true))
                {
                    if (parentKey == null)
                    {
                        Logger.Warning("Could not open Uninstall registry key");
                        return;
                    }
                    using (var existingKey = parentKey.OpenSubKey(AppKeyName))
                    {
                        if (existingKey == null)
                        {
                            Logger.Info("Registry entry does not exist, nothing to remove");
                            return;
                        }
                    }
                    parentKey.DeleteSubKeyTree(AppKeyName);
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

        public static bool IsRegistered()
        {
            try
            {
                using (var key = Registry.LocalMachine.OpenSubKey($@"{UninstallKeyPath}\{AppKeyName}"))
                    return key != null;
            }
            catch { return false; }
        }

        public static string GetRegisteredInstallLocation()
        {
            try
            {
                using (var key = Registry.LocalMachine.OpenSubKey($@"{UninstallKeyPath}\{AppKeyName}"))
                    return key?.GetValue("InstallLocation") as string;
            }
            catch { return null; }
        }

        public static string GetRegisteredVersion()
        {
            try
            {
                using (var key = Registry.LocalMachine.OpenSubKey($@"{UninstallKeyPath}\{AppKeyName}"))
                    return key?.GetValue("DisplayVersion") as string;
            }
            catch { return null; }
        }
    }
}
