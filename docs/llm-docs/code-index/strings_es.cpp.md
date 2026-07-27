# strings_es.cpp (846 lines)

Spanish translation table for `acc::strings::Id` (strings.h). Windows-1252
hex escapes for accented/punctuation characters (`\xE1/\xE9/\xED/\xF3/\xFA`
lowercase accents, `\xF1`=n-tilde, `\xBF`=inverted-question-mark, `\xA1`=
inverted-exclamation, plus uppercase variants) — bytes pass through unchanged
via Prism's `CP_ACP` ANSI overload on a Spanish Windows install. One
exhaustive `switch (id)` in enum order; no `default:` case, falls through to
the trailing `return ""`. Combat speech (attack/save callouts) is instead fed
by `combat_strings.cpp::kEs`, sourced from `dialog_es.tlk` engine anchors —
this file covers only the `Id::*` mod-speech path.

## Declarations (in source order)

- L16 — `namespace acc::strings::lang_es`
- L18 — `const char* Get(Id id)`
  note: exhaustive switch, one case per Id in strings.h enum order; string literals not indexed per task rules
