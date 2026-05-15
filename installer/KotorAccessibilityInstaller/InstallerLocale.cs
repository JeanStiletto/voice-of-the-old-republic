using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;

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
                        ParseFlatJson(json, dict);
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Warning($"[InstallerLocale] Error loading locale '{code}': {ex.Message}");
            }
            return dict;
        }

        private static void ParseFlatJson(string json, Dictionary<string, string> dict)
        {
            int i = 0;
            int len = json.Length;
            while (i < len && json[i] != '{') i++;
            i++;
            while (i < len)
            {
                while (i < len && (char.IsWhiteSpace(json[i]) || json[i] == ',')) i++;
                if (i >= len || json[i] == '}') break;
                string key = ParseJsonString(json, ref i);
                if (key == null) break;
                while (i < len && json[i] != ':') i++;
                i++;
                while (i < len && char.IsWhiteSpace(json[i])) i++;
                string value = ParseJsonString(json, ref i);
                if (value == null) break;
                dict[key] = value;
            }
        }

        private static string ParseJsonString(string json, ref int i)
        {
            int len = json.Length;
            while (i < len && char.IsWhiteSpace(json[i])) i++;
            if (i >= len || json[i] != '"') return null;
            i++;

            var sb = new StringBuilder();
            while (i < len)
            {
                char c = json[i];
                if (c == '"') { i++; return sb.ToString(); }
                if (c == '\\' && i + 1 < len)
                {
                    i++;
                    char esc = json[i];
                    switch (esc)
                    {
                        case '"': sb.Append('"'); break;
                        case '\\': sb.Append('\\'); break;
                        case '/': sb.Append('/'); break;
                        case 'n': sb.Append('\n'); break;
                        case 'r': sb.Append('\r'); break;
                        case 't': sb.Append('\t'); break;
                        case 'u':
                            if (i + 4 < len)
                            {
                                string hex = json.Substring(i + 1, 4);
                                if (int.TryParse(hex, System.Globalization.NumberStyles.HexNumber, null, out int code))
                                {
                                    sb.Append((char)code);
                                    i += 4;
                                }
                            }
                            break;
                        default: sb.Append(esc); break;
                    }
                }
                else
                {
                    sb.Append(c);
                }
                i++;
            }
            return sb.ToString();
        }
    }
}
