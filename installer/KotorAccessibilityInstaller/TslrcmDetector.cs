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
    /// <para>UNVERIFIED assumption, still: that TSLRCM 1.8.6's Inno script
    /// registers an uninstall entry at all. Nobody has watched a real 1.8.6 run
    /// do it. The caller therefore does NOT treat a miss here as proof of
    /// absence — <see cref="InstallFlow"/> prefers the silent installer's own
    /// dialog.tlk fingerprint verification, and asks the user when neither
    /// signal is available. Without that, a wrong assumption here would
    /// silently skip K2CP and the Tweak Pack on every install: no error, no
    /// prompt, two mods quietly missing.</para>
    /// </summary>
    public static class TslrcmDetector
    {
        private const string UninstallKeyPath =
            @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall";

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
                                using var sub = uninstall.OpenSubKey(subKeyName);
                                string displayName = sub?.GetValue("DisplayName") as string;
                                if (displayName != null &&
                                    displayName.IndexOf("Sith Lords Restored Content",
                                        StringComparison.OrdinalIgnoreCase) >= 0)
                                {
                                    Logger.Info($"TSLRCM detected via uninstall entry: \"{displayName}\" ({hive}/{view})");
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
