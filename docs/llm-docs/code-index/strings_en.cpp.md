# strings_en.cpp (844 lines)

English translation table for `acc::strings::Id` (strings.h). Plain 7-bit
ASCII throughout — no encoding concerns. One exhaustive `switch (id)` in enum
order, mirroring strings_de.cpp's structure/section comments but as the
non-default fallback language (German is the default active `Lang`). No
`default:` case; unmatched/`Id::Count_` falls through to the trailing
`return ""`.

## Declarations (in source order)

- L7 — `namespace acc::strings::lang_en`
- L9 — `const char* Get(Id id)`
  note: exhaustive switch, one case per Id in strings.h enum order; string literals not indexed per task rules
