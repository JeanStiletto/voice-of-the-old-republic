# strings_pl.cpp (620 case labels)

Polish translation table for `acc::strings::Id` (strings.h) — a
machine-translated draft flagged for a native-speaker pass (same quality bar as
the fr/it/es/ru tables); contributors can safely edit only the quoted text.

**Windows-1250 hex escapes**, not UTF-8 and not Windows-1252. This is stricter
than the fr/it/es case rather than merely different: `ą ć ę ł ń ś ź ż` have no
Windows-1252 representation at all, so the table could not be written the way
the German one is even in principle. Polish is pinned to codepage 1250 via
`CodepageFor(Lang::Pl)` + `prism::SetSpeechCodepage`, independent of the host
Windows locale — which matters more here than for Russian, because a Polish
KOTOR is almost always a Polish `dialog.tlk` dropped onto an English or German
install, so `CP_ACP` would be the wrong page most of the time.

Escapes were generated from UTF-8 source rather than typed by hand, with the
split-literal trick (`"\xBF""e"`-style, from strings_es.cpp) wherever a hex
escape is followed by a literal hex digit. Format placeholders (`%s`/`%d`/`%u`)
keep the same positional order as English — wording, not argument order,
absorbs Polish's different word order. One exhaustive `switch (id)` in enum
order; no `default:`, falls through to the trailing `return ""`.

Verified mechanically against `strings_en.cpp` on creation: 620/620 case labels,
identical order, identical printf specifier sequence per Id, and zero non-ASCII
bytes left in the file.

Combat speech is instead fed by `combat_strings.cpp::kPl`, sourced from the
official LEM Polish `dialog.tlk` (`LanguageID` 5 — a real BioWare ID, so unlike
Russian it needs no content probe). Note that `kPl` is the one part of the
Polish work not yet confirmed against a live combat log: the Polish engine
orders its attack-summary placeholders differently, which needed a second parse
path in `combat_log.cpp` rather than just different anchor strings. See
`docs/translation-additions.md`.

## Structure

Single function `const char* Get(Id id)` in `namespace acc::strings::lang_pl`,
declared in strings.h and dispatched from `strings.cpp`'s `Get(Id)` switch on
the active `Lang`. Section comments mirror strings_en.cpp's grouping.
