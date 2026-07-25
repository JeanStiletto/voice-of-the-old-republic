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

        // Russian is NOT a BioWare language ID. The community translations
        // re-encode an existing slot instead of claiming a new one — Allard
        // 1.72, the current active one, ships LanguageID 0 (the English slot)
        // with Windows-1251 text. So this member deliberately sits outside the
        // 0..4 ID range: it is a detection result, not a tlk header value, and
        // nothing may cast a language ID straight to it.
        Russian = 100,
    }

    /// <summary>
    /// Reads the game install's locale from the dialog.tlk header.
    /// TLK format: bytes 0..3 = "TLK ", 4..7 = "V3.0", 8..11 = uint32 language ID (LE),
    /// 12..15 = uint32 string count, 16..19 = uint32 offset to the string blob.
    /// 0=EN, 1=FR, 2=DE, 3=IT, 4=ES, others unknown — except Russian, which is
    /// identified by content rather than by ID (see <see cref="LooksCyrillic"/>).
    /// </summary>
    public static class GameLocaleDetector
    {
        // Keep these in lockstep with TlkLooksCyrillic in
        // patches/Accessibility/core_dllmain.cpp. If the installer and the DLL
        // ever disagree about what language an install is, the user gets an
        // installer in one language and speech in another.
        private const int CyrillicSampleBytes = 64 * 1024;
        private const double CyrillicThreshold = 0.20;

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
                var header = new byte[20];
                int read = fs.Read(header, 0, 20);
                if (read < 20)
                {
                    Logger.Warning($"dialog.tlk header too short ({read} bytes); locale = Unknown");
                    return GameLocale.Unknown;
                }

                uint lang = (uint)(header[8] | (header[9] << 8) | (header[10] << 16) | (header[11] << 24));
                uint blobOffset = (uint)(header[16] | (header[17] << 8) | (header[18] << 16) | (header[19] << 24));

                // Content probe outranks the declared ID — a Russian tlk can
                // claim any ID, so checking the bytes is the only reliable
                // test. Doing it first also picks up future Russian repacks
                // that choose a different slot.
                if (LooksCyrillic(fs, blobOffset))
                {
                    Logger.Info($"Detected game locale: Russian (dialog.tlk declares language id {lang} " +
                                "but the string blob reads as Windows-1251 Cyrillic)");
                    return GameLocale.Russian;
                }

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

        /// <summary>
        /// Samples the tlk's string blob and reports whether it reads as
        /// Windows-1251 Cyrillic. In CP1251 the Cyrillic block is a solid run
        /// at 0xC0-0xFF, so Russian prose is overwhelmingly high-byte:
        /// measured 77.8% on Allard 1.72 against 1.3% on a German tlk (whose
        /// umlauts also sit above 0xC0). The 20% threshold sits far from both.
        /// </summary>
        private static bool LooksCyrillic(FileStream fs, uint blobOffset)
        {
            if (blobOffset == 0 || blobOffset >= fs.Length) return false;
            fs.Seek(blobOffset, SeekOrigin.Begin);

            var sample = new byte[CyrillicSampleBytes];
            int got = fs.Read(sample, 0, sample.Length);
            // Too small a sample to judge; let the ID switch decide.
            if (got < 512) return false;

            int cyrillic = 0;
            for (int i = 0; i < got; i++)
            {
                if (sample[i] >= 0xC0) cyrillic++;
            }

            double ratio = (double)cyrillic / got;
            Logger.Info($"dialog.tlk CP1251 probe: {cyrillic}/{got} bytes >= 0xC0 ({ratio:P1})");
            return ratio >= CyrillicThreshold;
        }
    }
}
