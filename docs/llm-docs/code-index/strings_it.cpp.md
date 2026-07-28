# strings_it.cpp (844 lines)

Italian translation table for `acc::strings::Id` (strings.h). Windows-1252
hex escapes for accented characters (`\xE0/\xE8/\xE9/\xEC/\xF2/\xF9`) — bytes
pass through unchanged via Prism's `CP_ACP` ANSI overload on an Italian
Windows install. One exhaustive `switch (id)` in enum order; no `default:`,
falls through to the trailing `return ""`. Combat speech (attack/save
callouts) is instead fed by `combat_strings.cpp::kIt`, sourced from
`dialog_it.tlk` engine anchors — this file covers only the `Id::*` mod-speech
path.

## Declarations (in source order)

- L14 — `namespace acc::strings::lang_it`
- L16 — `const char* Get(Id id)`
  note: exhaustive switch, one case per Id in strings.h enum order; string literals not indexed per task rules
