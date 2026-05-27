// Combat-message localization table — DE primary, EN aliased to DE
// pending verification on a real English install.
//
// Encoding: Windows-1252 hex escapes for non-ASCII (\xFC=ü, \xF6=ö,
// \xE4=ä, \xDF=ß). Engine emits CP-1252 bytes in CExoString::c_string
// on a German Windows install, so anchor strings must use the same
// byte sequences for direct memcmp/strstr to work.
//
// Field order in the initialiser MUST match the declaration order in
// combat_strings.h — we use positional init (no designated initialisers
// in this codebase, per patch-build script flag set).

#include "combat_strings.h"

#include "strings.h"  // for acc::strings::Lang + GetLanguage

namespace acc::combat::loc {

namespace {

// German — engine-verified against patch-20260521-100345.log.
const MsgStrings kDe = {
    // ---- Engine-side parse anchors
    " ist erfolgreich mit Angriff auf ",
    " scheitert mit Angriff auf ",
    "  mit ",
    "Verteidigung ",
    "Schaden: ",
    " verwendet.",
    "Angriffsstatistik: ",
    "Abwehrstatistik: ",
    "Schadensstatistik: ",
    "Bedrohungsstatistik:",
    "Kritischer Treffer!",
    "Automatischer Treffer!",
    "Automatischer Fehlschlag!",
    "W\xFCrfelergebnis ",          // Würfelergebnis
    "Geschicklichkeit-Mod. ",
    "Entfernungsbonus ",
    "Effektbonus ",
    "Kritischer Treffer x",
    " f\xFCr ",                    // für
    "Bonusschaden",
    "Haupthand",

    // ---- Output-side labels
    "trifft",
    "verfehlt",
    "kritisch",
    "Angriff",
    "Vert.",
    "gg.",
    "Schaden",
    "von",
    "Auto-Treffer.",
    "Auto-Fehlschlag.",

    // ---- Short replacements
    "W",
    "Gesch ",
    "Reichweite ",
    "Effekt ",
    "Bonus",
};

// English — TODO(combat-loc-en): every engine-side anchor needs
// verification against a real EN swkotor.exe / dialog.tlk. Until then
// we alias the DE table: on an EN install the DE anchors won't match
// EN engine output, so every msg-bus line falls through to raw speech
// (no filter, no compaction). This is the safe degradation — a player
// on EN still hears every combat line, just unfiltered. To verify:
//   1. Boot EN install
//   2. Trigger a vanilla attack
//   3. grep `MsgBuf: raw:` in the patch log
//   4. Copy the exact byte sequences into the table below
const MsgStrings kEn = kDe;  // alias until EN strings are captured

}  // namespace

const MsgStrings& Get() {
    switch (acc::strings::GetLanguage()) {
        case acc::strings::Lang::En: return kEn;
        case acc::strings::Lang::De: return kDe;
    }
    return kDe;
}

}  // namespace acc::combat::loc
