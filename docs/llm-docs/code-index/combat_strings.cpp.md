# combat_strings.cpp (88 lines)

Provides the MsgStrings instances for DE and EN. DE strings are engine-verified against
patch-20260521-100345.log. EN is aliased to DE pending real-install capture (safe
degradation: EN combat lines fall through to raw speech unfiltered).
Non-ASCII encoded as Windows-1252 hex escapes to match engine's CExoString byte output.

## Declarations (in source order)

- L17 — `namespace acc::combat::loc`
- L22 — `const MsgStrings kDe`
  note: German engine-verified anchor strings + output labels; positional init order must match MsgStrings field declaration
- L76 — `const MsgStrings kEn`
  note: aliases kDe — EN engine-side anchors not yet verified; safe fallback is raw speech passthrough
- L80 — `const MsgStrings& Get()`
  note: returns kEn or kDe by acc::strings::GetLanguage(); falls back to kDe for unrecognised language
