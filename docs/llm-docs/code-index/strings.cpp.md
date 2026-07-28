# strings.cpp (43 lines)

Tiny language-selection shim on top of the per-language `lang_xx::Get(Id)`
tables (strings_de/en/es/fr/it/ru.cpp). Holds the single global `g_lang`
(default `Lang::De` per user direction — a Phase 7 options UI will add a
runtime toggle; `SetLanguage` is currently the only switch). `Get(Id)`
dispatches to the active language's table; `CodepageFor` maps each Lang to its
Windows codepage — 1252 for the Western-European set, 1251 pinned for Russian
so a Russian KOTOR install running on non-Russian Windows doesn't garble via
CP_ACP.

## Declarations (in source order)

- L9 — `Lang g_lang = Lang::De`
- L12 — `void SetLanguage(Lang l)` / L13 `Lang GetLanguage()`
- L15 — `const char* Get(Id id)`
  note: dispatches to lang_en/de/fr/it/es/ru::Get(id); returns "" on unhandled Lang
- L27 — `unsigned CodepageFor(Lang l)`
  note: 1252 for En/De/Fr/It/Es, 1251 for Ru (must be pinned, not CP_ACP)
