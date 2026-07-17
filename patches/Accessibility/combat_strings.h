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
// All five locales (DE/EN/FR/IT/ES) carry a full anchor set. DE is engine-
// verified against a real combat log; EN/FR/IT/ES anchors were extracted
// mechanically from each locale's dialog.tlk (see
// CombatStringsExtractCommand.cs StrrefMap) and the same rules reproduce
// the DE reference byte-for-byte, but the non-DE sets still want one in-
// locale combat capture to confirm (grep `MsgBuf: raw:` vs `emit-*`).
// Any anchor that doesn't match falls through to raw speech — no regression.

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

    // ---- Short results-only line labels (Phase 2 spoken form).
    const char* word_failed;         // "fehlgeschlagen" — miss line: "<actor>, fehlgeschlagen, <feat>"

    // Engine's trailing duplicate of an applied status ("Auswirkungsstatistik:
    // <target> ist <status>"). We already fold status into the merged attack
    // line from the summary tail, so this line is claimed + suppressed.
    // Non-DE locales: anchor not yet verified — a non-matching sentinel there
    // leaves the line on the raw-speech fallback (no regression).
    const char* prefix_auswirkung;   // "Auswirkungsstatistik:"

    // Damage-absorption lines ("<name> absorbiert N Punkte Schaden", and the
    // resistance variant "<name> : Schadensresistenz absorbiert N Punkte
    // Schaden : M Punkte verbleiben"). The engine fires one per blocked hit,
    // so a sustained shield spams dozens of lines. We coalesce a burst into a
    // single debounced total. anchor = the verb common to both forms; same
    // non-matching-sentinel rule for unverified locales.
    const char* absorb_anchor;       // "absorbiert"

    // ---- Ability / grenade / force-power effect sequence (Phase 3).
    // These power events arrive as separate lines we merge per target:
    //   use:    "<actor> benutzt <action>."  [+ " Investierte Machtpunkte: ..."]
    //   save:   "<saver> <SaveType>-Rettungswurf. Erfolg!|Misserfolg! ..."
    //   damage: "<actor> verletzt <target>: N Schaden (Type: N)"
    //   status: "Auswirkungsstatistik:<target> ist <status>"  (prefix_auswirkung)
    // Same non-matching-sentinel rule for unverified locales (anchors only —
    // output words are never reached when the anchors don't fire).
    const char* ability_use_marker;  // " benutzt "
    const char* save_marker;         // "-Rettungswurf."
    const char* save_success;        // "Erfolg!"
    const char* save_fail;           // "Misserfolg!"
    const char* damage_marker;       // " verletzt "
    const char* word_resists;        // "widersteht" — save-success verb
    const char* word_save_failed;    // "misslungen" — save-failure tag

    // Defeat/kill line ("<actor> neutralisiert <target>: N EPs"). Routed
    // through the urgent SAPI channel so a kill cuts through queued speech.
    const char* kill_marker;         // "neutralisiert"

    // Status-echo copula in "<target> ist <status>" (DE) — the "to be" verb
    // the engine glues between a creature and its applied status. Used to
    // lift the status word onto the attack/effect line and to split the
    // Auswirkungsstatistik echo. Hand-specified per locale (no single TLK
    // template yields it cleanly — FR phrases immunity without a copula).
    // Best-effort: if a locale's status line uses a different construction,
    // the status word is simply omitted (line still suppressed/shortened —
    // no regression).
    const char* status_ist_marker;   // " ist " (leading + trailing space)

    // ---- Blaster-deflection breakdown (strref 42417: "Reflexionsstatistik:
    // <CUSTOM0> reflektiert Projektil mit <CUSTOM1> = <CUSTOM2> gegen Angriff
    // <CUSTOM3>"). The engine fires one full breakdown per deflected pellet —
    // a Jedi tanking turret fire spams hundreds of raw lines. We claim the
    // line, extract the deflector (between prefix and mid marker), and burst-
    // coalesce into "<actor> reflektiert N Schüsse" (party members only).
    // Anchors extracted from all five locale TLKs 2026-07-17.
    const char* prefix_reflexion;    // "Reflexionsstatistik: "
    const char* reflect_mid_marker;  // " reflektiert Projektil mit "
    const char* fmt_deflect_one;     // "%s reflektiert 1 Schuss"
    const char* fmt_deflect_many;    // "%s reflektiert %d Schüsse"
};

const MsgStrings& Get();

}  // namespace acc::combat::loc
