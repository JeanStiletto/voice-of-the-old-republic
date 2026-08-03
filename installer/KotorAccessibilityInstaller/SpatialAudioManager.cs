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

        /// <summary>
        /// The same list, for the uninstaller. Exposed because "which files did
        /// we put there" is a question removal has to answer too, and a second
        /// hand-maintained copy in UninstallFlow is exactly how a file gets left
        /// behind — as the licence files and aldrv DLL were, surviving a
        /// disable-then-uninstall on a real install.
        /// </summary>
        public static IReadOnlyList<string> DeployedFiles => DeployedFileNames;

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
                if (!GamePathDetector.IsValidGamePath(gameDir))
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
                if (!GamePathDetector.IsValidGamePath(gameDir))
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
        /// Sets <c>EAX=0</c> or <c>EAX=1</c> under <c>[Sound Options]</c> of
        /// swkotor.ini. Preserves all other keys, ordering and whitespace.
        /// Idempotent.
        ///
        /// The section-walk used to be reimplemented here; it is
        /// SwkotorIniTweaker's job and always was (Phase-3 B6). Failure is
        /// non-fatal by design and only logged: the dsoal DLLs are already in
        /// place at this point, and the user can set EAX from the game's own
        /// sound options if the ini write did not land.
        /// </summary>
        private static void SetEaxValue(string gameDir, bool enable)
        {
            var result = SwkotorIniTweaker.ApplyEaxSetting(gameDir, enable);
            if (!result.Success)
            {
                Logger.Warning($"  EAX not toggled: {result.Error}");
            }
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
