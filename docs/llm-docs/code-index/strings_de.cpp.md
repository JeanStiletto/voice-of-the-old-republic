# strings_de.cpp (~300 lines)

German string table. Windows-1252 hex escapes for non-ASCII (e.g. \xE4=ä, \xFC=ü). Direction frame uses "auf X Uhr" idiom. String literals not indexed per task rules.

## Declarations (in source order)

- L17 — `namespace acc::strings::lang_de`
- L19 — `const char* Get(Id id)`
  note: switch over all Id values; returns German string literal for each
