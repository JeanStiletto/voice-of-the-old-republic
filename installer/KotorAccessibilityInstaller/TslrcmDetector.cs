using System;
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
    }
}
