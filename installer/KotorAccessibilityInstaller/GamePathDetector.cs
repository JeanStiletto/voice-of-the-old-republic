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
    // Game-install discovery and validation for KOTOR 1 and 2.
    //
    // Split out of Program.cs by the Phase-1 structure pass (refactoring
    // candidate 17). Everything here answers "where is the game, is this
    // path really it, and is it safe to write to right now" - no install
    // logic, no UI.
    static class GamePathDetector
    {
        public const string GameExeName = "swkotor.exe";

        // Steam install — the only supported distribution. GoG/Aspyr are out of scope
        // for the first installer pass; users can run kdev manually if needed.
        public static readonly string DefaultGamePath =
            @"C:\Program Files (x86)\Steam\steamapps\common\swkotor";

        internal static bool IsRunningAsAdmin()
        {
            try
            {
                using var identity = WindowsIdentity.GetCurrent();
                var principal = new WindowsPrincipal(identity);
                return principal.IsInRole(WindowsBuiltInRole.Administrator);
            }
            catch { return false; }
        }

        internal static bool IsGameRunning()
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

        // ------------------------------------------------------------------
        // KOTOR 2 detection — used by the game-version screen's preparation
        // notice today; becomes the KOTOR 2 flow's path detection when that
        // flow activates. Mirrors the KOTOR 1 chain minus the registry entry
        // we write ourselves (nothing is registered for KOTOR 2 yet).
        // ------------------------------------------------------------------

        public const string Kotor2ExeName = "swkotor2.exe";

        public static string DetectKotor2GamePath()
        {
            string steamReg = TryReadSteamKotor2InstallPath();
            if (IsValidKotor2GamePath(steamReg)) return steamReg;

            foreach (var root in new[]
            {
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)
            })
            {
                if (string.IsNullOrEmpty(root)) continue;
                string candidate = Path.Combine(root, "Steam", "steamapps", "common",
                    "Knights of the Old Republic II");
                if (IsValidKotor2GamePath(candidate)) return candidate;
            }

            return null;
        }

        private static string TryReadSteamKotor2InstallPath()
        {
            try
            {
                // KOTOR 2 Steam App ID = 208580 (the Aspyr build).
                using var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry32)
                    .OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 208580");
                return key?.GetValue("InstallLocation") as string;
            }
            catch { return null; }
        }

        public static bool IsValidKotor2GamePath(string path)
        {
            if (string.IsNullOrEmpty(path)) return false;
            return File.Exists(Path.Combine(path, Kotor2ExeName));
        }

        public static bool IsValidGamePath(string path)
        {
            if (string.IsNullOrEmpty(path)) return false;
            return File.Exists(Path.Combine(path, GameExeName));
        }

        /// <summary>
        /// True iff <paramref name="gamePath"/> is the install path Steam has
        /// registered for KOTOR (App ID 32370). Used by the post-install auto-
        /// launch to decide between <c>steam://run/32370</c> (preserves Steam
        /// overlay + cloud saves + non-elevated token) and a direct exe launch.
        ///
        /// Returns false for GoG copies, CD re-packs, manually-relocated Steam
        /// installs Steam doesn't know about, and any user-specified custom
        /// path — in those cases <c>steam://run/32370</c> would either silently
        /// no-op or launch a different copy than the one we just patched.
        /// </summary>
        public static bool IsSteamPath(string gamePath)
        {
            string steamRegistered = TryReadSteamAppInstallPath();
            if (string.IsNullOrEmpty(steamRegistered) || string.IsNullOrEmpty(gamePath))
                return false;
            return string.Equals(NormalizePath(steamRegistered), NormalizePath(gamePath),
                                 StringComparison.OrdinalIgnoreCase);
        }

        private static string NormalizePath(string p)
        {
            if (string.IsNullOrEmpty(p)) return p;
            try
            {
                return Path.GetFullPath(p)
                    .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            }
            catch { return p; }
        }

    }
}
