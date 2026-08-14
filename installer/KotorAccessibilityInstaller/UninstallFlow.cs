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
        public static void PerformUninstall(GameTarget target, string gamePath)
        {
            Logger.Info($"Uninstalling {target.DisplayName} from: {gamePath}");

            try
            {
                // The executable itself. Install no longer creates
                // <game exe>.backup.* files, so there is nothing to restore
                // wholesale — but our bundled engine patches (KOTOR 2's 4 GB and
                // borderless) write static hooks into it, and those we put back
                // by hand. Must run BEFORE the deletions below: it reads
                // kpm_install_state.json to learn which build the executable
                // started as, and that file is on the list.
                new InstallationManager(target, gamePath).RevertBundledExePatches();

                // Everything we drop into the game root, in one list.
                //
                // kpm_install_state.json is KPatchCore's record of what it
                // installed and the executable's ORIGINAL hash. Leaving it
                // behind was the subtlest of the leftovers: the game folder
                // still claimed our patch was installed after we had removed
                // it, which is both untrue and something a later install has to
                // reason about.
                //
                var rootFiles = new List<string>
                {
                    "KotorPatcher.dll",
                    "sqlite3.dll",
                    "addresses.db",
                    "patch_config.toml",
                    "dinput8.dll",
                    "kpm_install_state.json",
                };

                // The dsoal spatial-audio layer (KOTOR 1 only). Sourced from
                // SpatialAudioManager rather than re-listed here — a second copy
                // of that list is how its licence files and aldrv DLL came to
                // survive a disable-then-uninstall on a real install.
                rootFiles.AddRange(SpatialAudioManager.DeployedFiles);

                // The app-local Visual C++ runtime we drop next to the game exe.
                // We put it there, so we take it back out; the game itself never
                // needed it (it predates this CRT by a decade). Same reasoning as
                // above for sourcing the names from InstallationManager.
                rootFiles.AddRange(InstallationManager.VcRuntimeFileNames);

                // Artifacts from installer versions that no longer exist. No
                // current code path writes either name, and both were found in
                // a real KOTOR 1 folder — the uninstaller is the only thing that
                // will ever clean them up, so it has to know the old names as
                // well as the current ones.
                //   KotorAccessibility_Uninstaller.exe — the pre-rename
                //     uninstaller (now VoiceOfTheOldRepublic_Uninstaller.exe).
                //   <ini>.pre-dsoal.bak — a backup an earlier spatial-audio
                //     toggle wrote; nothing creates it today.
                rootFiles.Add("KotorAccessibility_Uninstaller.exe");
                rootFiles.Add(target.IniFileName + ".pre-dsoal.bak");

                foreach (var name in rootFiles)
                {
                    string p = Path.Combine(gamePath, name);
                    if (!File.Exists(p)) continue;
                    Logger.Info($"Removing {name}...");
                    try { File.Delete(p); }
                    catch (Exception ex) { Logger.Warning($"Could not delete {name}: {ex.Message}"); }
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
                foreach (var assetName in InstallationManager.AllOverrideAssetNamesEverInstalled)
                {
                    string assetPath = Path.Combine(overrideDir, assetName);
                    if (File.Exists(assetPath))
                    {
                        Logger.Info($"Removing Override asset {assetName}...");
                        try { File.Delete(assetPath); }
                        catch (Exception ex) { Logger.Warning($"Could not delete {assetName}: {ex.Message}"); }
                    }
                }

                // Restore the intro movies we renamed during install, so a
                // vanilla launch plays the publisher / legal splashes like a
                // fresh install. Per-game list — see GameTarget.
                Logger.Info("Restoring intro movies...");
                var introResult = IntroMovieDisabler.RestoreIntros(target, gamePath);
                if (!introResult.Success)
                {
                    Logger.Warning($"Intro restore failed: {introResult.Error}");
                }

                // Hand the movement keys back to the game's own defaults. The
                // mod's A/D strafe + Y/C camera layout is the most visible thing
                // it changes outside its own UI, and leaving it behind meant an
                // uninstalled mod still had the player's controls rearranged
                // with nothing left to explain why.
                Logger.Info("Restoring default movement keybinds...");
                var keymapResult = SwkotorIniTweaker.RemoveKeymapDefaults(target, gamePath);
                if (!keymapResult.Success)
                {
                    Logger.Warning($"Keymap restore failed: {keymapResult.Error}");
                }

                // Our crash-dump capture for this game's executable. Set by the
                // installer, so it is ours to unset — and it would otherwise
                // keep writing dumps for a game we are no longer part of.
                WerLocalDumps.Disable(target);

                RegistryManager.Unregister(target);
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
