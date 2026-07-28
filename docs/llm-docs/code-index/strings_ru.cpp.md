# strings_ru.cpp (868 lines)

Russian translation table for `acc::strings::Id` (strings.h) — a
machine-translated draft flagged for a native-speaker pass (same quality bar
as the fr/it/es tables); contributors can safely edit only the quoted text.
Windows-1251 hex escapes for Cyrillic (NOT UTF-8, NOT Windows-1252 — the
build has no `/utf-8` flag, and Russian is deliberately pinned to codepage
1251 via `CodepageFor(Lang::Ru)` + `prism::SetSpeechCodepage`, independent of
the host Windows locale, so it still speaks correctly on a German/English
Windows). Escapes were generated from UTF-8 source rather than typed by hand;
split-literal trick (`"\xE9""e"`-style) reused from strings_es.cpp wherever a
hex escape is followed by a literal hex digit. Format placeholders (`%s`/`%d`/
`%u`) keep the same positional order as English — wording, not argument order,
absorbs Russian's different word order. One exhaustive `switch (id)` in enum
order; no `default:`, falls through to the trailing `return ""`. Combat speech
is instead fed by `combat_strings.cpp::kRu`, sourced from Allard's
community-translation dialog.tlk (which declares English LanguageID 0 despite
being Cyrillic — see strings.h's `Lang::Ru` note on content-based detection).

## Declarations (in source order)

- L29 — `namespace acc::strings::lang_ru`
- L31 — `const char* Get(Id id)`
  note: exhaustive switch, one case per Id in strings.h enum order; string literals not indexed per task rules
