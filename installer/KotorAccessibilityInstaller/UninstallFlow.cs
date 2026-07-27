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
    // Uninstall.
    //
    // Split out of Program.cs by the Phase-1 structure pass (refactoring
    // candidate 17). Kept separate from the install flow because
    // UninstallForm only needs this half.
    static class UninstallFlow
    {
        public static void PerformUninstall(string gamePath)
        {
            Logger.Info($"Uninstalling from: {gamePath}");

            try
            {
                // No restore: install no longer creates swkotor.exe.backup.* files.
                // The uninstall confirmation text tells the user to verify game
                // files via Steam or reinstall from GoG to get vanilla back.

                // Remove patcher runtime files from game root.
                string[] runtimeFiles =
                {
                    "KotorPatcher.dll",
                    "sqlite3.dll",
                    "addresses.db",
                    "patch_config.toml",
                    "dinput8.dll"
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

                // Remove the Override assets we shipped (currently the
                // swoop accelpad WAV). Surgical per-file delete — never
                // touch the Override folder itself or any file we didn't
                // ship (users routinely drop their own mods in there).
                string overrideDir = Path.Combine(gamePath, "Override");
                foreach (var assetName in InstallationManager.OverrideAssetNames)
                {
                    string assetPath = Path.Combine(overrideDir, assetName);
                    if (File.Exists(assetPath))
                    {
                        Logger.Info($"Removing Override asset {assetName}...");
                        try { File.Delete(assetPath); }
                        catch (Exception ex) { Logger.Warning($"Could not delete {assetName}: {ex.Message}"); }
                    }
                }

                // Restore the intro movies we renamed during install. Returns
                // biologo / leclogo / legal .bik files to their vanilla names
                // so a vanilla launch plays the BioWare / LucasArts / legal
                // splash like a fresh install.
                Logger.Info("Restoring intro movies...");
                var introResult = IntroMovieDisabler.RestoreIntros(gamePath);
                if (!introResult.Success)
                {
                    Logger.Warning($"Intro restore failed: {introResult.Error}");
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

    }
}
