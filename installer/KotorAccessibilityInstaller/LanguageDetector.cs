using System.Collections.Generic;
using System.Globalization;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Detects the Windows display language and maps it to a supported installer language code.
    /// Add a new language by dropping a Locales/&lt;code&gt;.json file (auto-embedded via the
    /// csproj wildcard) and adding the code here in all three tables.
    /// </summary>
    public static class LanguageDetector
    {
        public static readonly string[] SupportedLanguages = new[] { "en", "de", "fr", "it", "es" };

        public static readonly Dictionary<string, string> DisplayNames = new Dictionary<string, string>
        {
            { "en", "English" },
            { "de", "Deutsch (German)" },
            { "fr", "Français (French)" },
            { "it", "Italiano (Italian)" },
            { "es", "Español (Spanish)" }
        };

        private static readonly Dictionary<string, string> LanguageMap = new Dictionary<string, string>
        {
            { "en", "en" },
            { "de", "de" },
            { "fr", "fr" },
            { "it", "it" },
            { "es", "es" }
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
