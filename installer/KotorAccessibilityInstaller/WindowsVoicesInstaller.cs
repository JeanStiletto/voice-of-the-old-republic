using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Microsoft.Win32;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Makes the Windows voices the game cannot see available to the mod's
    /// announcement and compass voices. Ported from the WowVision installer's
    /// <c>WindowsVoicesInstaller</c> (the same step as NVDA Voice for WoW and
    /// the Sku installer).
    ///
    /// The mod speaks its own voices through SAPI 5, which lists the tokens
    /// under HKLM\SOFTWARE\Microsoft\Speech\Voices\Tokens. Windows keeps its
    /// newer voices (the Narrator voices, e.g. Katja and Stefan for German) in
    /// a separate list, Speech_OneCore\Voices\Tokens, which SAPI never reads.
    /// Their engine is Microsoft's own SAPI engine, so copying a token into the
    /// SAPI list is all it takes; the voice files stay where Windows installed
    /// them.
    ///
    /// Unlike WoW, both KOTOR games are 32-bit, and a 32-bit process reads the
    /// 32-bit registry view (WOW6432Node) — a separate SAPI list that is
    /// typically missing the OneCore voices even on machines where the 64-bit
    /// one already has them. Windows registers the OneCore tokens and their
    /// engine in the 32-bit view too, so the copy runs entirely there.
    ///
    ///   1. find the OneCore tokens missing from the SAPI list (by token name);
    ///   2. back up the SAPI list (reg export) — no backup, no change;
    ///   3. copy each missing token with its values and Attributes subkey.
    ///
    /// Runs on every install, so voices installed since are picked up; a second
    /// run (both games selected) finds nothing missing. Every failure is logged
    /// and reported but never fails the install. Nothing is removed on
    /// uninstall: the voices are a machine-wide addition other programs may
    /// rely on by then, and the backup holds the original list.
    /// </summary>
    internal static class WindowsVoicesInstaller
    {
        private const string SapiTokens = @"SOFTWARE\Microsoft\Speech\Voices\Tokens";
        private const string OneCoreTokens = @"SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens";

        private static string BackupDir => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "KotorAccessibility",
            "sapi-voices-backup");

        public static void Install(Action<string> report)
        {
            try
            {
                using var hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry32);
                using var oneCore = hklm.OpenSubKey(OneCoreTokens);
                using var sapi = hklm.CreateSubKey(SapiTokens);

                var present = new HashSet<string>(sapi.GetSubKeyNames(), StringComparer.OrdinalIgnoreCase);
                var missing = oneCore == null
                    ? new List<string>()
                    : oneCore.GetSubKeyNames().Where(name => !present.Contains(name)).ToList();
                Logger.Info($"Windows voices: {present.Count} SAPI token(s), {missing.Count} OneCore voice(s) missing from it (32-bit view)");
                if (missing.Count == 0)
                {
                    report(InstallerLocale.Get("WindowsVoices_None"));
                    return;
                }

                if (!Backup())
                {
                    report(InstallerLocale.Get("WindowsVoices_BackupFailed"));
                    return;
                }

                int added = 0;
                foreach (var name in missing)
                {
                    using var source = oneCore.OpenSubKey(name);
                    string label = source?.GetValue("") as string ?? name;
                    try
                    {
                        using (var target = sapi.CreateSubKey(name))
                            CopyKey(source, target);
                        Logger.Info($"Windows voices: copied token {name} ({label})");
                        report(InstallerLocale.Format("WindowsVoices_Added_Format", label));
                        added++;
                    }
                    catch (Exception ex)
                    {
                        Logger.Error($"Windows voices: copying token {name} failed", ex);
                        // A half-copied token would list a voice that cannot speak.
                        try { sapi.DeleteSubKeyTree(name, false); } catch { }
                        report(InstallerLocale.Format("WindowsVoices_FailedOne_Format", label, ex.Message));
                    }
                }
                if (added > 0)
                    report(InstallerLocale.Format("WindowsVoices_Count_Format", added));
            }
            catch (Exception ex)
            {
                Logger.Error("Windows voices failed", ex);
                report(InstallerLocale.Format("WindowsVoices_Failed_Format", ex.Message));
            }
        }

        /// <summary>Values (kinds and unexpanded %variables% kept) and subkeys,
        /// recursively.</summary>
        private static void CopyKey(RegistryKey source, RegistryKey target)
        {
            foreach (var valueName in source.GetValueNames())
            {
                target.SetValue(valueName,
                    source.GetValue(valueName, null, RegistryValueOptions.DoNotExpandEnvironmentNames),
                    source.GetValueKind(valueName));
            }
            foreach (var subName in source.GetSubKeyNames())
            {
                using var sourceSub = source.OpenSubKey(subName);
                using var targetSub = target.CreateSubKey(subName);
                CopyKey(sourceSub, targetSub);
            }
        }

        private static bool Backup()
        {
            try
            {
                Directory.CreateDirectory(BackupDir);
                string file = Path.Combine(BackupDir, $"sapi-voices-32bit-{DateTime.Now:yyyy-MM-dd_HH-mm-ss}.reg");
                string reg = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "reg.exe");
                var psi = new ProcessStartInfo(reg, $"export \"HKLM\\{SapiTokens}\" \"{file}\" /y /reg:32")
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                using var p = Process.Start(psi);
                if (!p.WaitForExit(30000) || p.ExitCode != 0 || !File.Exists(file))
                {
                    Logger.Warning("Windows voices: voice list backup failed");
                    return false;
                }
                Logger.Info("Windows voices: voice list backed up to " + file);
                return true;
            }
            catch (Exception ex)
            {
                Logger.Error("Windows voices: voice list backup failed", ex);
                return false;
            }
        }
    }
}
