using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.Json;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Static localization class. Loads flat JSON files from embedded resources.
    /// Fallback chain: active language -> English -> key name.
    /// </summary>
    public static class InstallerLocale
    {
        private static Dictionary<string, string> _activeStrings = new Dictionary<string, string>();
        private static Dictionary<string, string> _fallbackStrings = new Dictionary<string, string>();
        private static string _activeLanguage = "en";

        public static event Action OnLanguageChanged;

        public static void Initialize(string languageCode) => LoadLanguage(languageCode);

        public static void SetLanguage(string code)
        {
            if (code == _activeLanguage) return;
            LoadLanguage(code);
            OnLanguageChanged?.Invoke();
        }

        public static string Get(string key)
        {
            if (_activeStrings.TryGetValue(key, out string val)) return val;
            if (_fallbackStrings.TryGetValue(key, out string fallback)) return fallback;
            return key;
        }

        public static string Format(string key, params object[] args)
        {
            string template = Get(key);
            try { return string.Format(template, args); }
            catch (FormatException) { return template; }
        }

        private static void LoadLanguage(string code)
        {
            _activeLanguage = code;
            _fallbackStrings = LoadEmbeddedLocale("en");
            _activeStrings = code == "en" ? _fallbackStrings : LoadEmbeddedLocale(code);
            Logger.Info($"[InstallerLocale] Loaded language: {code} ({_activeStrings.Count} active, {_fallbackStrings.Count} fallback strings)");
        }

        private static Dictionary<string, string> LoadEmbeddedLocale(string code)
        {
            var dict = new Dictionary<string, string>();
            try
            {
                var assembly = Assembly.GetExecutingAssembly();
                string resourceName = $"locale.{code}.json";

                using (var stream = assembly.GetManifestResourceStream(resourceName))
                {
                    if (stream == null)
                    {
                        Logger.Warning($"[InstallerLocale] Locale resource not found: {resourceName}");
                        return dict;
                    }
                    using (var reader = new StreamReader(stream, Encoding.UTF8))
                    {
                        string json = reader.ReadToEnd();
                        // Locale files are flat {"key": "value"} objects.
                        // This replaced ~65 lines of hand-rolled scanning
                        // (Phase-3 B6); System.Text.Json was already a
                        // dependency via GitHubClient. Verified equivalent
                        // over all 1146 keys in the six shipped locales
                        // before the swap.
                        var parsed = JsonSerializer
                            .Deserialize<Dictionary<string, string>>(json);
                        if (parsed != null)
                        {
                            foreach (var kv in parsed) dict[kv.Key] = kv.Value;
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Warning($"[InstallerLocale] Error loading locale '{code}': {ex.Message}");
            }
            return dict;
        }

    }
}
