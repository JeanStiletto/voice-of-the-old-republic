using System.IO;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Game-install locale, sourced from the dialog.tlk header.
    /// </summary>
    public enum GameLocale
    {
        Unknown = -1,
        English = 0,
        French = 1,
        German = 2,
        Italian = 3,
        Spanish = 4,
    }

    /// <summary>
    /// Reads the game install's locale from the dialog.tlk header.
    /// TLK format: bytes 0..3 = "TLK ", 4..7 = "V3.0", 8..11 = uint32 language ID (LE).
    /// 0=EN, 1=FR, 2=DE, 3=IT, 4=ES, others unknown.
    /// </summary>
    public static class GameLocaleDetector
    {
        public static GameLocale Detect(string gameDir)
        {
            try
            {
                string tlkPath = Path.Combine(gameDir, "dialog.tlk");
                if (!File.Exists(tlkPath))
                {
                    Logger.Warning($"dialog.tlk not found at {tlkPath}; locale = Unknown");
                    return GameLocale.Unknown;
                }

                using var fs = new FileStream(tlkPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                var header = new byte[12];
                int read = fs.Read(header, 0, 12);
                if (read < 12)
                {
                    Logger.Warning($"dialog.tlk header too short ({read} bytes); locale = Unknown");
                    return GameLocale.Unknown;
                }

                uint lang = (uint)(header[8] | (header[9] << 8) | (header[10] << 16) | (header[11] << 24));
                var locale = lang switch
                {
                    0 => GameLocale.English,
                    1 => GameLocale.French,
                    2 => GameLocale.German,
                    3 => GameLocale.Italian,
                    4 => GameLocale.Spanish,
                    _ => GameLocale.Unknown,
                };
                Logger.Info($"Detected game locale: {locale} (dialog.tlk language id = {lang})");
                return locale;
            }
            catch (System.Exception ex)
            {
                Logger.Warning($"Locale detection failed: {ex.Message}; locale = Unknown");
                return GameLocale.Unknown;
            }
        }
    }
}
