# InstallerLocale.cs (150 lines)

Static localization engine for the whole installer. Loads flat-string JSON locale files (`locale.<code>.json`) from embedded resources with a hand-rolled minimal JSON parser (no System.Text.Json dependency for this — avoids pulling in a full parser for a flat key/value structure). Fallback chain on lookup: active language -> English -> the key name itself (so a missing translation degrades to a readable identifier rather than throwing). `SetLanguage` raises `OnLanguageChanged` so every open form's `ApplyLocale()` handler re-pulls text live (used across GameVersionSelectionForm, ModSelectionForm, Kotor2ModSelectionForm, ModdingInfoForm).

## Declarations (in source order)

- L13 — `public static class InstallerLocale`
- L19 — `event Action OnLanguageChanged`
- L21 — `void Initialize(string languageCode)`
- L23 — `void SetLanguage(string code)` — no-ops if unchanged, else reloads + fires event
- L30 — `string Get(string key)` — active -> fallback -> key
- L37 — `string Format(string key, params object[] args)` — string.Format over Get(key), swallows FormatException
- L44 — `void LoadLanguage(string code)` — always loads English as fallback dict
- L52 — `Dictionary<string,string> LoadEmbeddedLocale(string code)` — reads `locale.<code>.json` embedded resource
- L81 — `void ParseFlatJson(string json, Dictionary<string,string> dict)` — hand-rolled flat `{"key":"value",...}` parser
- L102 — `string ParseJsonString(string json, ref int i)` — handles `\"`, `\\`, `\/`, `\n`, `\r`, `\t`, `\uXXXX` escapes
