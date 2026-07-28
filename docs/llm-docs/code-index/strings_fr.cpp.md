# strings_fr.cpp (845 lines)

French translation table for `acc::strings::Id` (strings.h). Windows-1252 hex
escapes for accented characters (`\xE9`=e-acute, `\xE8`=e-grave, `\xEA`=
e-circumflex, `\xE0`=a-grave, `\xE7`=c-cedilla, `\xF4`=o-circumflex, etc.);
where a hex escape is immediately followed by a literal hex-digit character
the string is split into two adjacent literals (e.g. `"\xE9""e"`) so the
compiler doesn't swallow the digit into the escape. One exhaustive
`switch (id)` in enum order; no `default:`, falls through to the trailing
`return ""`. Despite strings.h's stale comment suggesting lang_fr aliases
lang_en, this file is a full independent French translation. Combat speech
(attack/save callouts) is instead fed by `combat_strings.cpp::kFr`, sourced
from `dialog_fr.tlk` engine anchors.

## Declarations (in source order)

- L15 — `namespace acc::strings::lang_fr`
- L17 — `const char* Get(Id id)`
  note: exhaustive switch, one case per Id in strings.h enum order; string literals not indexed per task rules
