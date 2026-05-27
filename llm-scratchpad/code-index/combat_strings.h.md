# combat_strings.h (69 lines)

Combat-message localization table. Holds locale-dependent tokens for the msg-bus rules:
engine-side parse anchors (substrings scanned in engine output) and output-side labels
(what gets spoken). Separate from strings.h so the Id enum stays user-facing only.
EN anchors unverified; kEn aliases kDe until an EN tester captures real strings.

## Declarations (in source order)

- L17 — `namespace acc::combat::loc`
- L19 — `struct MsgStrings`
  note: all fields are const char*; field order in the .cpp initialiser must match declaration order (no designated initialisers in this codebase)
- L67 — `const MsgStrings& Get()`
  note: returns kDe or kEn based on acc::strings::GetLanguage(); kEn currently aliases kDe
