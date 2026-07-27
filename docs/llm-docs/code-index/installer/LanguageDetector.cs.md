# LanguageDetector.cs (55 lines)

Detects the Windows display language and maps it to one of the installer's supported language codes, defaulting to English. Adding a language requires only dropping a `Locales/<code>.json` file (auto-embedded via csproj wildcard) plus adding the code to all three tables here.

## Declarations (in source order)

- L11 — `public static class LanguageDetector`
- L13 — `SupportedLanguages` = en, de, fr, it, es, ru
- L15 — `DisplayNames` — code -> native display name dictionary
- L25 — `LanguageMap` — currently identity mapping en/de/fr/it/es/ru
- L35 — `static string DetectLanguage()` — logs CurrentUICulture + result
- L43 — `static string GetBestLanguage(CultureInfo culture)` — tries exact culture name, then two-letter ISO code, then parent culture, else falls back to "en"
