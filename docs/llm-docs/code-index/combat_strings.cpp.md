# combat_strings.cpp (535 lines)

Six `MsgStrings` locale tables (kDe, kEn, kFr, kIt, kEs, kRu) built with positional initialisers matching combat_strings.h's declaration order, plus the `Get()` dispatcher keyed on `acc::strings::GetLanguage()`. Encoding is the game's own codepage — hex escapes are Windows-1252 for de/en/fr/it/es and Windows-1251 for Russian (the exception; `acc::strings::CodepageFor(Lang::Ru)` and `prism::SetSpeechCodepage` handle the split). Each non-DE table carries inline comments documenting per-locale quirks discovered while extracting anchors from that language's dialog.tlk (word order, missing copula, glued multiplier tokens, etc.) — e.g. Italian's "X" krit_x_prefix (multiplier-first), Russian's missing "to be" copula (status_ist_marker uses a `\x01` sentinel that can never occur in TLK text), French's " : " separator style requiring short anchors.

## Declarations (in source order)

- L22 — `namespace acc::combat::loc`
- L27 — `const MsgStrings kDe` — German, engine-verified against patch-20260521-100345.log
- L101 — `const MsgStrings kEn` — English; one flagged rough spot (BuildCompact's German word order for "critical")
- L182 — `const MsgStrings kFr` — French; uses " : " separators instead of word-prefixes
- L259 — `const MsgStrings kIt` — Italian; krit_x_prefix is bare "X" (multiplier-first word order)
- L335 — `const MsgStrings kEs` — Spanish; multiplier glued directly to "Crítico" with no space
- L433 — `const MsgStrings kRu` — Russian, Windows-1251; extracted from Allard 1.72's community-translated TLK which declares LanguageID=0 (English slot) despite being Cyrillic — detected by content-probe (`TlkLooksCyrillic` in core_dllmain.cpp), not the declared ID
  note: no status-echo copula exists in Russian; status word is skipped rather than mis-captured (see quirk (b) comment)
- L523 — `const MsgStrings& Get()`
