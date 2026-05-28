# strings.cpp (23 lines)

String table dispatcher. Holds the active language state (default De). Routes Get(id) to lang_de::Get or lang_en::Get based on g_lang.

## Declarations (in source order)

- L3 — `namespace acc::strings`
- L12 — `void SetLanguage(Lang l)`
- L13 — `Lang GetLanguage()`
- L15 — `const char* Get(Id id)`
