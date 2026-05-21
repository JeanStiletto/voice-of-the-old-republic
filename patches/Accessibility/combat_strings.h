// Combat-message localization table.
//
// Holds every locale-dependent token used by the combat msg-bus rules:
//   * Engine-side parse anchors (substrings the engine emits that we
//     scan for — "Angriffsstatistik:", " ist erfolgreich ...", etc.).
//   * Our shortened output labels (the words we emit in the compact /
//     short-form announcements — "trifft", "Vert.", "Energie", etc.).
//
// Why a separate table from strings.h:
//   acc::strings is the spoken/user-facing table. Its Id enum is about
//   what the user *hears*. The combat parser also needs locale-dependent
//   *anchors* (what the engine *says*), which would bloat the spoken-
//   string enum if mixed in. Keeping them apart preserves the rule that
//   strings.h IDs are speech-only.
//
// Language switch follows acc::strings::Lang (SetLanguage / GetLanguage)
// so the user's existing language toggle controls both tables.
//
// EN coverage: the EN engine-side strings have NOT been verified
// against a real EN install yet. The kEn table currently aliases the
// kDe table — that means on an EN install no anchor will match and
// every msg-bus line falls through to raw speech (no filter, no
// compaction). Fill in real EN anchors when an EN tester is available.

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
