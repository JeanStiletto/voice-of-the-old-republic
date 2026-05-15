using System.Collections.Generic;
using System.Globalization;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Detects the Windows display language and maps it to a supported installer language code.
    /// Only English and German are shipped today; more can be added by dropping a Locales/&lt;code&gt;.json.
    /// </summary>
    public static class LanguageDetector
    {
        public static readonly string[] SupportedLanguages = new[] { "en", "de" };

        public static readonly Dictionary<string, string> DisplayNames = new Dictionary<string, string>
        {
            { "en", "English" },
            { "de", "Deutsch (German)" }
        };

        private static readonly Dictionary<string, string> LanguageMap = new Dictionary<string, string>
        {
            { "en", "en" },
            { "de", "de" }
        };

        public static string DetectLanguage()
        {
            var culture = CultureInfo.CurrentUICulture;
            string result = GetBestLanguage(culture);
            Logger.Info($"Windows UI culture: {culture.Name} ({culture.DisplayName}), detected installer language: {result}");
            return result;
        }

        private static string GetBestLanguage(CultureInfo culture)
        {
            if (LanguageMap.TryGetValue(culture.Name, out string match)) return match;
            if (LanguageMap.TryGetValue(culture.TwoLetterISOLanguageName, out string isoMatch)) return isoMatch;
            if (!culture.IsNeutralCulture && culture.Parent != null && culture.Parent != CultureInfo.InvariantCulture)
            {
                if (LanguageMap.TryGetValue(culture.Parent.Name, out string parentMatch)) return parentMatch;
            }
            return "en";
        }
    }
}
