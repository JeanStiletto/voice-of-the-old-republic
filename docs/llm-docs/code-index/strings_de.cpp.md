# strings_de.cpp (863 lines)

German translation table for `acc::strings::Id` (strings.h). Default active
language (`SetLanguage` defaults to `Lang::De`). One giant `switch (id)`
covering every Id in enum order, each returning a Windows-1252-encoded
literal (`\xE4`=a-umlaut, `\xF6`=o-umlaut, `\xFC`=u-umlaut, `\xDF`=sharp-s,
`\xC4/\xD6/\xDC`=uppercase umlauts) — the raw bytes pass through unchanged
because Prism's ANSI overload widens via `CP_ACP`, which is Windows-1252 on a
German Windows install. Notable idiom choices: navigation direction uses "auf
X Uhr" (not bare "X Uhr", which would read as time-of-day); empty-category
phrases use the plural noun form ("Keine Türen", not "Keine Tür"). No
`default:` case — falls through to the function's trailing `return ""` for
any `Id::Count_` or (in practice unreachable) unmatched value.

## Declarations (in source order)

- L17 — `namespace acc::strings::lang_de`
- L19 — `const char* Get(Id id)`
  note: exhaustive switch, one case per Id in strings.h enum order; string literals not indexed per task rules
