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

        /// <summary>
        /// The first target whose process is live, or null when neither is.
        /// Checks both games: <c>GetProcessesByName</c> matches the name exactly,
        /// so a "swkotor" query does NOT see a running "swkotor2" — which let an
        /// install proceed against a live KOTOR 2 and fight it for file handles.
        /// </summary>
        internal static GameTarget RunningGame()
        {
            foreach (var target in GameTarget.All)
            {
                try
                {
                    if (Process.GetProcessesByName(target.ProcessName).Length > 0)
                        return target;
                }
                catch { /* fall through to the next target */ }
            }
            return null;
        }

        /// <summary>
        /// Attempts to detect KOTOR's install path. Order:
        /// 1. Registry (previously registered by us)
        /// 2. Steam's per-app registry key (HKLM ...\Steam App 32370)
        /// 3. Common Steam location under Program Files (x86)
        /// 4. CommonObjectives — Default Steam library path
        /// </summary>
        public static string DetectGamePath() => Detect(GameTarget.Kotor1);

        public static string DetectKotor2GamePath() => Detect(GameTarget.Kotor2);

        /// <summary>
        /// Locate <paramref name="target"/>'s install. Order: an install we
        /// registered ourselves, then Steam's own per-app uninstall key, then the
        /// default Steam library folder.
        /// </summary>
        public static string Detect(GameTarget target)
        {
            string registered = RegistryManager.GetRegisteredInstallLocation(target);
            if (IsValidGamePath(target, registered)) return registered;

            string steamReg = TryReadSteamAppInstallPath(target);
            if (IsValidGamePath(target, steamReg)) return steamReg;

            foreach (var root in new[]
            {
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)
            })
            {
                if (string.IsNullOrEmpty(root)) continue;
                string candidate = Path.Combine(root, "Steam", "steamapps", "common",
                                                SteamLibraryFolderName(target));
                if (IsValidGamePath(target, candidate)) return candidate;
            }

            return null;
        }

        /// <summary>Folder name Steam installs each game under inside a library.</summary>
        private static string SteamLibraryFolderName(GameTarget target) =>
            target == GameTarget.Kotor2 ? "Knights of the Old Republic II" : "swkotor";

        /// <summary>
        /// Steam's own per-app uninstall key, which carries InstallLocation.
        ///
        /// <para>Both registry views are checked, and that is the whole point:
        /// this used to read <see cref="RegistryView.Registry32"/> only, which on
        /// 64-bit Windows redirects to <c>WOW6432Node</c> — and Steam writes
        /// these keys to the 64-bit view. So the lookup returned null for BOTH
        /// games on every 64-bit machine. Path detection hid it by falling
        /// through to the default-library scan, but <see cref="IsSteamPath"/>
        /// had no fallback: it always answered "not a Steam install", so the
        /// post-install launch always started the executable directly and the
        /// game announced that it needs to be run from Steam.</para>
        ///
        /// <para>Which view Steam uses is not something to assume — a 32-bit
        /// Steam, or an older one, may well write the other. Checking both is
        /// cheap and removes the guess.</para>
        /// </summary>
        private static string TryReadSteamAppInstallPath(GameTarget target)
        {
            string subKey = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App " + target.SteamAppId;

            foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                try
                {
                    using var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
                    using var key = baseKey.OpenSubKey(subKey);
                    if (key?.GetValue("InstallLocation") is string location && location.Length > 0)
                        return location;
                }
                catch { /* view unavailable — try the other */ }
            }
            return null;
        }

        public static bool IsValidKotor2GamePath(string path) => IsValidGamePath(GameTarget.Kotor2, path);

        public static bool IsValidGamePath(string path) => IsValidGamePath(GameTarget.Kotor1, path);

        public static bool IsValidGamePath(GameTarget target, string path)
        {
            if (string.IsNullOrEmpty(path)) return false;
            return File.Exists(Path.Combine(path, target.ExeName));
        }

        /// <summary>
        /// True iff <paramref name="gamePath"/> is the install path Steam has
        /// registered for this game. Used by the post-install auto-launch to
        /// decide between the <c>steam://run/&lt;appid&gt;</c> URL (preserves the
        /// Steam overlay, cloud saves and a non-elevated token) and a direct exe
        /// launch.
        ///
        /// Returns false for GoG copies, CD re-packs, manually-relocated Steam
        /// installs Steam doesn't know about, and any user-specified custom
        /// path — in those cases the steam:// URL would either silently no-op or
        /// launch a different copy than the one we just patched.
        /// </summary>
        public static bool IsSteamPath(GameTarget target, string gamePath)
        {
            string steamRegistered = TryReadSteamAppInstallPath(target);
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
