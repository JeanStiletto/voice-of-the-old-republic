// Combat-message localization table — DE + EN tables.
//
// Engine anchors were extracted from each locale's dialog.tlk via
// `kdev combat-strings-extract` (the strref→field map lives in
// CombatStringsExtractCommand.cs); speech-side labels are hand-translated.
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

    // ---- Short results-only line labels
    "fehlgeschlagen",              // word_failed
    "Auswirkungsstatistik:",       // prefix_auswirkung
    "absorbiert",                  // absorb_anchor

    // ---- Ability / grenade / force-power effect sequence
    " benutzt ",                   // ability_use_marker
    "-Rettungswurf.",              // save_marker
    "Erfolg!",                     // save_success
    "Misserfolg!",                 // save_fail
    " verletzt ",                  // damage_marker
    "widersteht",                  // word_resists
    "misslungen",                  // word_save_failed
    "neutralisiert",               // kill_marker

    // ---- Status-echo copula
    " ist ",                       // status_ist_marker
};

// English — engine anchors extracted from EN dialog.tlk on 2026-05-28
// via `kdev combat-strings-extract` (Steam language swap). Speech labels
// hand-translated; one rough spot flagged for tester feedback: BuildCompact
// emits "<actor> hits critical <target>" (German word order) which reads
// slightly awkwardly in English — would need a BuildCompact restructure
// to say "critically hits" — left for post-beta polish.
const MsgStrings kEn = {
    // ---- Engine-side parse anchors (extracted from EN dialog.tlk)
    " succeeds with attack on ",   // phrase_hit       (template 42042 + verb 42043)
    " fails with attack on ",      // phrase_miss      (template 42042 + verb 42044)
    " with ",                      // phrase_mit       (42119; EN is the ONLY locale that fills the
                                   //   suffix's <CUSTOM0> hit/miss tag — strref 42133 "Hit" / 42134
                                   //   "Miss" (empty in DE/FR/IT/ES). So EN renders "...on <target>.
                                   //   Hit with N vs..." — a single-space " with " connector, NOT the
                                   //   glued double-space the extractor assumes for empty-<CUSTOM0>
                                   //   locales. Single leading space here matches both Hit and Miss.)
    "Defense ",                    // word_verteidigung (42119 gap CUSTOM1..CUSTOM2)
    "damage ",                     // word_schaden_colon (42119 gap CUSTOM2..CUSTOM3)
    " used.",                      // feat_marker      (42046 + engine-appended ".")
    "Attack Breakdown: ",          // prefix_angriff   (42146)
    "Defense Breakdown: ",         // prefix_abwehr    (42149)
    "Damage Breakdown: ",          // prefix_schaden   (42150)
    "Threat Breakdown:",           // prefix_bedrohung (42148)
    "Critical Hit!",               // tag_krit_summary (1511)
    "Automatic Hit!",              // tag_auto_hit     (42390)
    "Automatic Miss!",             // tag_auto_fail    (42391)
    "roll ",                       // token_wuerfel    (42316, parser pre-strips leading space)
    "dex mod ",                    // token_gesch_mod  (42339, attack-context label)
    "Close Proximity Ranged Bonus ", // token_entfernung (42330)
    "Effect Bonus ",               // token_effekt     (42332)
    "Critical x",                  // krit_x_prefix    (42386)
    " for ",                       // phrase_fuer      (42386 tail after CUSTOM0)
    "bonus damage",                // token_bonusschaden (42155)
    "Mainhand",                    // hand_main        (42314)

    // ---- Output-side labels (hand-translated)
    "hits",                        // verb_hit
    "misses",                      // verb_miss
    "critical",                    // word_critical (awkward EN word order — see header)
    "Attack",                      // word_angriff
    "Def.",                        // word_vert
    "vs.",                         // word_gg
    "Damage",                      // word_schaden
    "from",                        // word_von
    "Auto-hit.",                   // word_auto_hit
    "Auto-miss.",                  // word_auto_fail

    // ---- Short replacements (compaction of engine breakdown tokens)
    "R",                           // short_wuerfel    (roll → R)
    "Dex ",                        // short_gesch
    "Close-range ",                // short_reichweite (engine label is verbose "Close Proximity Ranged Bonus")
    "Effect ",                     // short_effekt
    "Bonus",                       // short_bonus

    // ---- Short results-only line labels
    "failed",                            // word_failed
    "Effect Application Breakdown:",     // prefix_auswirkung (TLK 42157)
    "absorbs",                           // absorb_anchor (1455 verb; misses 1454 "resists" energy-shield variant)

    // ---- Ability / grenade / force-power effect sequence
    " uses ",                            // ability_use_marker (32292)
    " Save. ",                           // save_marker (1374-1376 common suffix + 1406 separator)
    "success!",                          // save_success (1392 + 1406 punct)
    "failure!",                          // save_fail (1393 + 1406 punct)
    " damages ",                         // damage_marker (1403)
    "resists",                           // word_resists
    "failed",                            // word_save_failed
    "killed",                            // kill_marker (1407)

    // ---- Status-echo copula
    " is ",                              // status_ist_marker
};

// French — engine anchors extracted from FR dialog.tlk on 2026-05-28
// via `kdev combat-strings-extract --lang fr` (Steam language swap).
// Speech labels hand-translated. The FR engine glues the stats suffix
// with " : " separators instead of word-prefixes (DE "gegen", EN "vs."),
// so word_verteidigung and word_schaden_colon are intentionally short —
// in context they're unique because the parser searches from AFTER the
// phrase_mit anchor, and the éφ acute in "défense" prevents false hits
// on "de " in that substring.
const MsgStrings kFr = {
    // ---- Engine-side parse anchors (extracted from FR dialog.tlk)
    " r\xE9ussit son attaque contre ",  // phrase_hit       (42042 + verb 42043 "réussit")
    " rate son attaque contre ",        // phrase_miss      (42042 + verb 42044 "rate")
    "  : ",                             // phrase_mit       (42119, +1 glue space; FR uses ":" separator)
    "de ",                              // word_verteidigung (42119, locates def via " de " before <CUSTOM2>)
    ": ",                               // word_schaden_colon (42119, ":" between <CUSTOM2> and <CUSTOM3>)
    " utilis\xE9(e).",                  // feat_marker      (42046 + engine-appended ".")
    "D\xE9""composition de l'attaque : ",      // prefix_angriff  (42146)
    "D\xE9""composition de la d\xE9""fense : ", // prefix_abwehr  (42149)
    "D\xE9""composition des d\xE9g\xE2ts : ",   // prefix_schaden (42150)
    "D\xE9""composition du critique possible :", // prefix_bedrohung (42148)
    "Coup critique !",                  // tag_krit_summary (1511)
    "Coup au but automatique !",        // tag_auto_hit     (42390)
    "Coup automatiquement rat\xE9 !",   // tag_auto_fail    (42391)
    "jet de ",                          // token_wuerfel    (42316, parser pre-strips leading space)
    "modificateur de Dext\xE9rit\xE9 : ", // token_gesch_mod (42339)
    "bonus \xE0 distance (proximit\xE9) : ", // token_entfernung (42330)
    "bonus d'effet : ",                 // token_effekt     (42332)
    "Critique : x",                     // krit_x_prefix    (42386)
    ", pour un total de ",              // phrase_fuer      (42386 tail after CUSTOM0)
    "bonus aux d\xE9g\xE2ts :",         // token_bonusschaden (42155; FR adds " : " so parser's
                                        // no-colon Bonusschaden branch won't fire — short_bonus
                                        // replacement degrades to verbose label; accept for now)
    "Main directrice",                  // hand_main        (42314)

    // ---- Output-side labels (hand-translated)
    "touche",                           // verb_hit
    "rate",                             // verb_miss
    "critique",                         // word_critical
    "Attaque",                          // word_angriff
    "D\xE9""f.",                        // word_vert
    "vs.",                              // word_gg
    "D\xE9g\xE2ts",                     // word_schaden
    "de",                               // word_von
    "Coup auto.",                       // word_auto_hit
    "Rat\xE9 auto.",                    // word_auto_fail

    // ---- Short replacements (compaction of engine breakdown tokens)
    "J",                                // short_wuerfel    (Jet → J)
    "Dex ",                             // short_gesch
    "Port\xE9""e ",                     // short_reichweite
    "Effet ",                           // short_effekt
    "Bonus",                            // short_bonus

    // ---- Short results-only line labels
    "rat\xE9",                                         // word_failed
    "D\xE9""composition de l'application d'effet :",   // prefix_auswirkung (42157)
    "absorbe",                                         // absorb_anchor (1455 verb; misses 1454 "r\xE9siste" variant)

    // ---- Ability / grenade / force-power effect sequence
    " utilise ",                                       // ability_use_marker (32292)
    "  : ",                                            // save_marker (no common save-type suffix; 1406 separator, 2 spaces)
    "succ\xE8s !",                                     // save_success (1392 + 1406 punct)
    "\xE9""chec !",                                    // save_fail (1393 + 1406 punct)
    " touche ",                                        // damage_marker (1403)
    "r\xE9siste",                                      // word_resists
    "\xE9""chou\xE9",                                  // word_save_failed
    "a tu\xE9",                                        // kill_marker (1407)

    // ---- Status-echo copula
    " est ",                                           // status_ist_marker
};

// Italian — engine anchors extracted from IT dialog.tlk on 2026-05-28
// via `kdev combat-strings-extract --lang it` (Steam language swap).
// Speech labels hand-translated. IT engine quirk: 42386 is "X<CUSTOM0>
// critico per " (multiplier-FIRST word order), so krit_x_prefix is just
// "X". This is unique-enough because critical lines are the only ones
// starting with "X" after prefix_schaden's "Divisione dei Danni: " is
// consumed.
const MsgStrings kIt = {
    // ---- Engine-side parse anchors (extracted from IT dialog.tlk)
    " riesce ad attaccare ",            // phrase_hit       (42042 + verb 42043 "riesce")
    " non riesce ad attaccare ",        // phrase_miss      (42042 + verb 42044 "non riesce")
    "  con ",                           // phrase_mit       (42119, +1 glue space)
    "Difesa ",                          // word_verteidigung (42119 gap CUSTOM1..CUSTOM2)
    "danni ",                           // word_schaden_colon (42119 gap CUSTOM2..CUSTOM3 — "per danni N")
    " usato.",                          // feat_marker      (42046 + engine-appended ".")
    "Divisione dell'attacco: ",         // prefix_angriff   (42146)
    "Divisione della Difesa: ",         // prefix_abwehr    (42149)
    "Divisione dei Danni: ",            // prefix_schaden   (42150)
    "Divisione della Minaccia:",        // prefix_bedrohung (42148)
    "Colpo Critico!",                   // tag_krit_summary (1511)
    "Colpo automatico!",                // tag_auto_hit     (42390)
    "Errore automatico!",               // tag_auto_fail    (42391)
    "tiro ",                            // token_wuerfel    (42316)
    "modificatore destrezza ",          // token_gesch_mod  (42339)
    "Bonus di Vicinanza ",              // token_entfernung (42330)
    "Bonus Effetto ",                   // token_effekt     (42332)
    "X",                                // krit_x_prefix    (42386 — multiplier-first IT word order)
    " critico per ",                    // phrase_fuer      (42386 tail after CUSTOM0)
    "danno bonus",                      // token_bonusschaden (42155)
    "Mano dominante",                   // hand_main        (42314)

    // ---- Output-side labels (hand-translated)
    "colpisce",                         // verb_hit
    "manca",                            // verb_miss
    "critico",                          // word_critical
    "Attacco",                          // word_angriff
    "Dif.",                             // word_vert
    "vs.",                              // word_gg
    "Danni",                            // word_schaden
    "da",                               // word_von
    "Colpo auto.",                      // word_auto_hit
    "Errore auto.",                     // word_auto_fail

    // ---- Short replacements (compaction of engine breakdown tokens)
    "T",                                // short_wuerfel    (Tiro → T)
    "Des ",                             // short_gesch      (Destrezza)
    "Vicinanza ",                       // short_reichweite (engine label "Bonus di Vicinanza")
    "Effetto ",                         // short_effekt
    "Bonus",                            // short_bonus

    // ---- Short results-only line labels
    "fallito",                                      // word_failed
    "Divisione dell'Applicazione degli Effetti.",   // prefix_auswirkung (42157; note trailing '.')
    "assorbe",                                      // absorb_anchor (1455 verb)

    // ---- Ability / grenade / force-power effect sequence
    " usa ",                                        // ability_use_marker (32292)
    ". ",                                           // save_marker (no common save-type suffix; 1406 separator)
    "successo!",                                    // save_success (1392 + 1406 punct)
    "fallimento!",                                  // save_fail (1393 + 1406 punct)
    " danni ",                                      // damage_marker (1403)
    "resiste",                                      // word_resists
    "fallito",                                      // word_save_failed
    "ucciso",                                       // kill_marker (1407)

    // ---- Status-echo copula
    " \xE8 ",                                       // status_ist_marker (" è ")
};

// Spanish — engine anchors extracted from ES dialog.tlk on 2026-05-28
// via `kdev combat-strings-extract --lang es` (Steam language swap).
// Speech labels hand-translated. ES engine quirks: (a) 42386 is
// "X Cr\xEDtico<CUSTOM0> para " — multiplier glued directly to "Crítico"
// with no space, so krit_x_prefix carries the full "X Crítico" literal.
// (b) tag_krit_summary preserves the trailing space present in strref
// 1511 — the engine's stored string includes it and the parser uses
// strstr so it matches engine output verbatim.
const MsgStrings kEs = {
    // ---- Engine-side parse anchors (extracted from ES dialog.tlk)
    " tiene \xE9xito en el ataque contra ",  // phrase_hit       (42042 + verb 42043 "tiene éxito")
    " fracasa en el ataque contra ",         // phrase_miss      (42042 + verb 42044 "fracasa")
    "  con ",                                // phrase_mit       (42119, +1 glue space)
    "Defensa ",                              // word_verteidigung (42119 gap CUSTOM1..CUSTOM2)
    "da\xF1os ",                             // word_schaden_colon (42119 — "para daños N")
    " utilizada.",                           // feat_marker      (42046 + engine-appended ".")
    "Colapso del ataque: ",                  // prefix_angriff   (42146)
    "Colapso de Defensa: ",                  // prefix_abwehr    (42149)
    "Colapso de da\xF1os: ",                 // prefix_schaden   (42150)
    "Colapso de la amenaza:",                // prefix_bedrohung (42148)
    "\xA1Golpe Cr\xEDtico! ",                // tag_krit_summary (1511 — trailing space preserved from TLK)
    "\xA1""Acierto autom\xE1tico!",          // tag_auto_hit     (42390)
    "\xA1""Fallo autom\xE1tico!",            // tag_auto_fail    (42391)
    "tirada ",                               // token_wuerfel    (42316)
    "mod des ",                              // token_gesch_mod  (42339 — abbreviated "modificador destreza")
    "Bonificaci\xF3n de Proximidad Cercana a distancia ", // token_entfernung (42330)
    "Bonificaci\xF3n de Efecto ",            // token_effekt     (42332)
    "X Cr\xEDtico",                          // krit_x_prefix    (42386 — multiplier glued, no space)
    " para ",                                // phrase_fuer      (42386 tail after CUSTOM0)
    "da\xF1o de bonificaci\xF3n",            // token_bonusschaden (42155)
    "Mano principal",                        // hand_main        (42314)

    // ---- Output-side labels (hand-translated)
    "acierta",                               // verb_hit
    "falla",                                 // verb_miss
    "cr\xEDtico",                            // word_critical
    "Ataque",                                // word_angriff
    "Def.",                                  // word_vert
    "vs.",                                   // word_gg
    "Da\xF1os",                              // word_schaden
    "de",                                    // word_von
    "Acierto auto.",                         // word_auto_hit
    "Fallo auto.",                           // word_auto_fail

    // ---- Short replacements (compaction of engine breakdown tokens)
    "T",                                     // short_wuerfel    (Tirada → T)
    "Des ",                                  // short_gesch      (Destreza)
    "Proximidad ",                           // short_reichweite (engine label is verbose "Proximidad Cercana a distancia")
    "Efecto ",                               // short_effekt
    "Bonus",                                 // short_bonus

    // ---- Short results-only line labels
    "fallado",                               // word_failed
    "Colapso de Aplicaci\xF3n de efecto:",   // prefix_auswirkung (42157)
    "absorbe",                               // absorb_anchor (1455 verb; misses 1454 "resiste" variant)

    // ---- Ability / grenade / force-power effect sequence
    "utiliza ",                              // ability_use_marker (32292; no leading space in ES template)
    " : ",                                   // save_marker (no common save-type suffix; 1406 separator)
    "\xE9xito!",                             // save_success (1392 + 1406 punct)
    "fallo!",                                // save_fail (1393 + 1406 punct)
    " da\xF1""a a ",                         // damage_marker (1403)
    "resiste",                               // word_resists
    "fallado",                               // word_save_failed
    "mat\xF3 a",                             // kill_marker (1407)

    // ---- Status-echo copula
    " es ",                                  // status_ist_marker
};

}  // namespace

const MsgStrings& Get() {
    switch (acc::strings::GetLanguage()) {
        case acc::strings::Lang::En: return kEn;
        case acc::strings::Lang::De: return kDe;
        case acc::strings::Lang::Fr: return kFr;
        case acc::strings::Lang::It: return kIt;
        case acc::strings::Lang::Es: return kEs;
    }
    return kDe;
}

}  // namespace acc::combat::loc
