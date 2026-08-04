using System;
using System.IO;
using Microsoft.Win32;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Detects an existing TSLRCM install via the uninstall entry its Inno
    /// Setup installer registers (DisplayName "The Sith Lords Restored Content
    /// Mod ..."). Scans both registry views and both hives because Inno's
    /// PrivilegesRequired setting decides where the key lands.
    ///
    /// <para>Used to gate K2CP / Tweak Pack ordering: they must install AFTER
    /// TSLRCM.</para>
    ///
    /// <para>RESOLVED 2026-08-03, by reading a real 1.8.6 install's Inno log
    /// and then the registry it wrote. TSLRCM does register an uninstall entry:
    /// <c>HKLM\SOFTWARE\...\Uninstall\the sith lords restored content mod_is1</c>,
    /// in the 32-bit view. What it does NOT do is put the mod's full name in
    /// <c>DisplayName</c> — that value is the terse <c>"tslrcm 1.8.6"</c>. This
    /// detector matched DisplayName against "Sith Lords Restored Content" and so
    /// reported "not installed" for an install that was sitting right there.
    /// The descriptive name lives in the KEY name, so both are matched now, and
    /// against either wording.</para>
    ///
    /// <para><b>Structural blind spot (found 2026-08-04).</b> This can only ever
    /// see the English DeadlyStream edition, because only that one runs an Inno
    /// installer. Non-English players get the localized Steam Workshop edition
    /// via <see cref="WorkshopTslrcmForm"/> — a file copy into the game folder
    /// that registers nothing at all. For those users a registry miss is not
    /// weak evidence of absence, it is *no* evidence, and our own installer is
    /// what put them in that state. See <see cref="CountPatchedModules"/> and
    /// the caller.</para>
    ///
    /// <para>The caller still does not treat a miss here as proof of absence —
    /// see <see cref="InstallFlow"/>. Being wrong in that direction silently
    /// skips K2CP and the Tweak Pack, which is the one failure worth extra
    /// belt-and-braces.</para>
    /// </summary>
    public static class TslrcmDetector
    {
        private const string UninstallKeyPath =
            @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall";

        /// <summary>
        /// Matched case-insensitively against both the uninstall key's name and
        /// its DisplayName. Two spellings because the two fields disagree: the
        /// key is named for the mod, the DisplayName for its abbreviation.
        /// </summary>
        private static readonly string[] Needles =
        {
            "sith lords restored content",
            "tslrcm",
        };

        private static bool Matches(string value) =>
            value != null && Array.Exists(Needles,
                n => value.IndexOf(n, StringComparison.OrdinalIgnoreCase) >= 0);

        public static bool IsInstalled()
        {
            foreach (var hive in new[] { RegistryHive.LocalMachine, RegistryHive.CurrentUser })
            {
                foreach (var view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
                {
                    try
                    {
                        using var baseKey = RegistryKey.OpenBaseKey(hive, view);
                        using var uninstall = baseKey.OpenSubKey(UninstallKeyPath);
                        if (uninstall == null) continue;

                        foreach (var subKeyName in uninstall.GetSubKeyNames())
                        {
                            try
                            {
                                if (Matches(subKeyName))
                                {
                                    Logger.Info($"TSLRCM detected via uninstall key name: \"{subKeyName}\" ({hive}/{view})");
                                    return true;
                                }

                                using var sub = uninstall.OpenSubKey(subKeyName);
                                string displayName = sub?.GetValue("DisplayName") as string;
                                if (Matches(displayName))
                                {
                                    Logger.Info($"TSLRCM detected via DisplayName: \"{displayName}\" ({hive}/{view})");
                                    return true;
                                }
                            }
                            catch { /* unreadable subkey — keep scanning */ }
                        }
                    }
                    catch { /* view/hive not accessible — keep scanning */ }
                }
            }

            Logger.Info("TSLRCM not detected (no matching uninstall entry).");
            return false;
        }

        /// <summary>
        /// How many <c>.mod</c> files sit in the game's <c>Modules</c> folder.
        ///
        /// <para>Context for a question we cannot answer from the registry. A
        /// stock KOTOR 2 ships its modules as <c>.rim</c> / <c>_s.rim</c> pairs;
        /// <c>.mod</c> files appear when a module-patching mod has run. So a
        /// large count means *some* substantial mod set is installed — which,
        /// for the mods this installer offers, in practice means TSLRCM.</para>
        ///
        /// <para>Deliberately NOT turned into a boolean here. It cannot tell
        /// TSLRCM apart from any other module-patching mod, and both ways of
        /// being wrong hurt: claiming absence silently skips the user's mods,
        /// claiming presence installs them in the wrong order. It is reported to
        /// the user as evidence inside the question instead of being used to
        /// answer the question for them.</para>
        ///
        /// <para>Returns 0 for a missing folder or an unreadable one — the
        /// caller asks either way, so a failure here costs nothing.</para>
        /// </summary>
        public static int CountPatchedModules(string gameDir)
        {
            if (string.IsNullOrEmpty(gameDir)) return 0;

            try
            {
                string modulesDir = Path.Combine(gameDir, "Modules");
                if (!Directory.Exists(modulesDir)) return 0;
                return Directory.GetFiles(modulesDir, "*.mod", SearchOption.TopDirectoryOnly).Length;
            }
            catch (Exception ex)
            {
                Logger.Warning($"TSLRCM probe: could not count .mod files in {gameDir}: {ex.Message}");
                return 0;
            }
        }
    }
}
