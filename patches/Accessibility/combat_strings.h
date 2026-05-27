// Combat-message localization table.
//
// Holds locale-dependent tokens the combat msg-bus rules use: engine-
// side parse anchors (substrings the engine emits that we scan for) and
// our shortened output labels (what we speak). Kept separate from
// strings.h so the speech-only Id enum stays focused on what the user
// hears, not what the engine says.
//
// Language follows acc::strings::Lang.
//
// EN engine-side anchors haven't been verified against a real EN install
// yet — kEn currently aliases kDe, so EN msg-bus lines fall through to
// raw speech. Fill in when an EN tester is available.

#pragma once

namespace acc::combat::loc {

struct MsgStrings {
    // ---- Engine-side parse anchors. Each must match the engine's
    //      emitted text exactly (case + spaces + trailing punctuation).
    const char* phrase_hit;          // " ist erfolgreich mit Angriff auf "
    const char* phrase_miss;         // " scheitert mit Angriff auf "
    const char* phrase_mit;          // "  mit "   (note: 2 leading spaces)
    const char* word_verteidigung;   // "Verteidigung "
    const char* word_schaden_colon;  // "Schaden: "
    const char* feat_marker;         // " verwendet."
    const char* prefix_angriff;      // "Angriffsstatistik: "
    const char* prefix_abwehr;       // "Abwehrstatistik: "
    const char* prefix_schaden;      // "Schadensstatistik: "
    const char* prefix_bedrohung;    // "Bedrohungsstatistik:"
    const char* tag_krit_summary;    // "Kritischer Treffer!"
    const char* tag_auto_hit;        // "Automatischer Treffer!"
    const char* tag_auto_fail;       // "Automatischer Fehlschlag!"
    const char* token_wuerfel;       // "Würfelergebnis "
    const char* token_gesch_mod;     // "Geschicklichkeit-Mod. "
    const char* token_entfernung;    // "Entfernungsbonus "
    const char* token_effekt;        // "Effektbonus "
    const char* krit_x_prefix;       // "Kritischer Treffer x"
    const char* phrase_fuer;         // " für "   (5 bytes, leading+trailing space)
    const char* token_bonusschaden;  // "Bonusschaden"
    const char* hand_main;           // "Haupthand" — default hand label, suppressed from output

    // ---- Output-side labels (what we speak).
    const char* verb_hit;            // "trifft"
    const char* verb_miss;           // "verfehlt"
    const char* word_critical;       // "kritisch"
    const char* word_angriff;        // "Angriff"
    const char* word_vert;           // "Vert."
    const char* word_gg;             // "gg."   (separator "vs.")
    const char* word_schaden;        // "Schaden"
    const char* word_von;            // "von"
    const char* word_auto_hit;       // "Auto-Treffer."
    const char* word_auto_fail;      // "Auto-Fehlschlag."

    // Shortened replacements for verbose engine labels in the breakdown.
    // Length-paired with the source token above; the parser uses these
    // to compress lines like "Würfelergebnis 14+ Basis 1+ ..." to
    // "W14+Basis 1+...".
    const char* short_wuerfel;       // "W"           (no trailing space — value follows immediately)
    const char* short_gesch;         // "Gesch "      (with trailing space)
    const char* short_reichweite;    // "Reichweite "
    const char* short_effekt;        // "Effekt "
    const char* short_bonus;         // "Bonus"       (no trailing space — value follows " 5")
};

const MsgStrings& Get();

}  // namespace acc::combat::loc
