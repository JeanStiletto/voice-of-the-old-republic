using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Toggle for the bundled dsoal + OpenAL Soft spatial-audio layer.
    ///
    /// "Enabled" = dsoal's <c>dsound.dll</c> sits in the game folder, hijacking
    /// the DirectSound import. OpenAL Soft does per-source HRTF + EAX rendering.
    /// "Disabled" = the three files are absent; the engine falls back to Windows'
    /// native DirectSound path.
    ///
    /// Source state is detected purely by presence of <c>dsound.dll</c> in the
    /// game directory — the other two files are dead unless it loads them.
    /// </summary>
    public static class SpatialAudioManager
    {
        // Names as they appear in the game folder once extracted.
        public const string DsoundDllName = "dsound.dll";
        public const string AldrvDllName = "dsoal-aldrv.dll";
        public const string AlsoftIniName = "alsoft.ini";
        public const string DsoalLicenseName = "dsoal-DSOAL-License.txt";
        public const string OpenAlSoftLicenseName = "dsoal-OpenALSoft-License.txt";

        // Embedded resource short-names (Resources\dsoal-*).
        private const string ResDsoundDll = "dsoal-dsound.dll";
        private const string ResAldrvDll = "dsoal-aldrv.dll";
        private const string ResAlsoftIni = "dsoal-alsoft.ini";
        private const string ResDsoalLicense = "dsoal-DSOAL-License.txt";
        private const string ResOpenAlSoftLicense = "dsoal-OpenALSoft-License.txt";

        // Files we own in the game folder. Disable() deletes exactly these.
        private static readonly string[] DeployedFileNames =
        {
            DsoundDllName,
            AldrvDllName,
            AlsoftIniName,
            DsoalLicenseName,
            OpenAlSoftLicenseName,
        };

        public sealed class Result
        {
            public bool Success { get; init; }
            public bool NowEnabled { get; init; }
            public string Error { get; init; }
        }

        /// <summary>
        /// True if dsound.dll is present in the game folder — the dsoal hijack
        /// only works when this exists, so it's the authoritative state bit.
        /// </summary>
        public static bool IsEnabled(string gameDir)
        {
            if (string.IsNullOrEmpty(gameDir)) return false;
            return File.Exists(Path.Combine(gameDir, DsoundDllName));
        }

        /// <summary>
        /// Extracts the bundled dsoal binaries into the game folder and sets
        /// <c>EAX=1</c> under <c>[Sound Options]</c> in swkotor.ini. Overwrites
        /// any existing files of the same names (idempotent re-enable).
        /// </summary>
        public static Result Enable(string gameDir)
        {
            try
            {
                if (!Program.IsValidGamePath(gameDir))
                {
                    return new Result { Success = false, Error = $"swkotor.exe not found at {gameDir}" };
                }

                Logger.Info($"Enabling spatial audio in: {gameDir}");

                Extract(ResDsoundDll, Path.Combine(gameDir, DsoundDllName));
                Extract(ResAldrvDll, Path.Combine(gameDir, AldrvDllName));
                Extract(ResAlsoftIni, Path.Combine(gameDir, AlsoftIniName));
                Extract(ResDsoalLicense, Path.Combine(gameDir, DsoalLicenseName));
                Extract(ResOpenAlSoftLicense, Path.Combine(gameDir, OpenAlSoftLicenseName));

                SetEaxValue(gameDir, enable: true);

                Logger.Info("Spatial audio enabled.");
                return new Result { Success = true, NowEnabled = true };
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to enable spatial audio", ex);
                return new Result { Success = false, Error = ex.Message };
            }
        }

        /// <summary>
        /// Deletes the dsoal files from the game folder and sets <c>EAX=0</c>.
        /// Missing files are ignored — the operation is idempotent.
        /// </summary>
        public static Result Disable(string gameDir)
        {
            try
            {
                if (!Program.IsValidGamePath(gameDir))
                {
                    return new Result { Success = false, Error = $"swkotor.exe not found at {gameDir}" };
                }

                Logger.Info($"Disabling spatial audio in: {gameDir}");

                foreach (var name in DeployedFileNames)
                {
                    string p = Path.Combine(gameDir, name);
                    if (File.Exists(p))
                    {
                        try
                        {
                            File.Delete(p);
                            Logger.Info($"  Removed {name}");
                        }
                        catch (Exception ex)
                        {
                            Logger.Warning($"  Could not delete {name}: {ex.Message}");
                        }
                    }
                }

                SetEaxValue(gameDir, enable: false);

                Logger.Info("Spatial audio disabled.");
                return new Result { Success = true, NowEnabled = false };
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to disable spatial audio", ex);
                return new Result { Success = false, Error = ex.Message };
            }
        }

        /// <summary>
        /// Writes <c>EAX=0</c> or <c>EAX=1</c> under <c>[Sound Options]</c>.
        /// Preserves all other keys, ordering, and whitespace. Idempotent.
        /// </summary>
        private static void SetEaxValue(string gameDir, bool enable)
        {
            const string sectionHeader = "[Sound Options]";
            const string key = "EAX";
            string desired = enable ? "1" : "0";

            string iniPath = Path.Combine(gameDir, "swkotor.ini");
            if (!File.Exists(iniPath))
            {
                Logger.Warning($"  swkotor.ini not found at {iniPath} — EAX not toggled");
                return;
            }

            var lines = new List<string>(File.ReadAllLines(iniPath));

            int sectionStart = -1;
            for (int i = 0; i < lines.Count; i++)
            {
                if (lines[i].Trim().Equals(sectionHeader, StringComparison.OrdinalIgnoreCase))
                {
                    sectionStart = i;
                    break;
                }
            }

            int sectionEnd = lines.Count;
            if (sectionStart >= 0)
            {
                for (int i = sectionStart + 1; i < lines.Count; i++)
                {
                    string t = lines[i].Trim();
                    if (t.StartsWith("[", StringComparison.Ordinal) && t.EndsWith("]", StringComparison.Ordinal))
                    {
                        sectionEnd = i;
                        break;
                    }
                }
            }
            else
            {
                if (lines.Count > 0 && !string.IsNullOrEmpty(lines[^1])) lines.Add(string.Empty);
                lines.Add(sectionHeader);
                sectionStart = lines.Count - 1;
                sectionEnd = lines.Count;
                Logger.Info($"  {sectionHeader} not found; appending");
            }

            bool found = false;
            for (int i = sectionStart + 1; i < sectionEnd; i++)
            {
                string line = lines[i];
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;

                string k = line.Substring(0, eq).Trim();
                if (!k.Equals(key, StringComparison.OrdinalIgnoreCase)) continue;

                string current = line.Substring(eq + 1).Trim();
                if (current.Equals(desired, StringComparison.Ordinal))
                {
                    Logger.Info($"  EAX={desired} already set");
                }
                else
                {
                    lines[i] = $"{key}={desired}";
                    Logger.Info($"  EAX: {current} -> {desired}");
                }
                found = true;
                break;
            }

            if (!found)
            {
                lines.Insert(sectionEnd, $"{key}={desired}");
                Logger.Info($"  EAX={desired} appended (key was missing)");
            }

            var sb = new StringBuilder();
            foreach (var line in lines) sb.Append(line).Append("\r\n");
            File.WriteAllText(iniPath, sb.ToString(), new UTF8Encoding(false));
        }

        private static void Extract(string resourceShortName, string targetPath)
        {
            var assembly = Assembly.GetExecutingAssembly();
            string fullName = null;
            foreach (var name in assembly.GetManifestResourceNames())
            {
                if (name.EndsWith(resourceShortName, StringComparison.OrdinalIgnoreCase))
                {
                    fullName = name;
                    break;
                }
            }
            if (fullName == null)
                throw new FileNotFoundException($"Embedded resource not found: {resourceShortName}");

            using var stream = assembly.GetManifestResourceStream(fullName)
                ?? throw new InvalidOperationException($"Could not open resource stream: {fullName}");
            using var fileStream = new FileStream(targetPath, FileMode.Create, FileAccess.Write);
            stream.CopyTo(fileStream);
            Logger.Info($"  Extracted {resourceShortName} -> {targetPath}");
        }
    }
}
