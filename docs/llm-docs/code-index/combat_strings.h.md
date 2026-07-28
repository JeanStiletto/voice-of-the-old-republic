# combat_strings.h (133 lines)

Locale table of engine-side parse anchors (exact substrings the German/English/French/Italian/Spanish/Russian engine emits into the combat message buffer) plus our shortened output labels, consumed by combat.cpp's message-bus rules. Kept separate from strings.h so the user-facing `acc::strings::Id` enum stays about what's spoken, not what the engine says. DE is engine-verified against a real log; other locales were extracted mechanically from each dialog.tlk and reproduce the DE rules byte-for-byte, but want an in-locale capture to fully confirm. Any anchor that doesn't match falls through to raw speech (no regression).

## Declarations (in source order)

- L21 — `namespace acc::combat::loc`
- L23 — `struct MsgStrings`
  note: ~45 const char* fields — engine anchors (phrase_hit, prefix_angriff, absorb_anchor, etc.) then output-side labels (verb_hit, word_critical, short_wuerfel, etc.); field order must match the positional initialisers in combat_strings.cpp
- L131 — `const MsgStrings& Get()`
