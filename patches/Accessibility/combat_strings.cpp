// Combat-message localization table — DE + EN tables.
//
// Engine anchors were extracted from each locale's dialog.tlk via
// `kdev combat-strings-extract` (the strref→field map lives in
// CombatStringsExtractCommand.cs); speech-side labels are hand-translated.
//
// Encoding: hex escapes in the *game's* codepage, because the anchors are
// memcmp'd/strstr'd directly against CExoString bytes. For de/en/fr/it/es
// that is Windows-1252 (\xFC=ü, \xF6=ö, \xE4=ä, \xDF=ß). kRu and kPl are the
// exceptions: Russian installs are Windows-1251 and Polish ones Windows-1250,
// so those escapes must not be read as 1252. acc::strings::CodepageFor(Lang)
// reports which, and prism::SetSpeechCodepage pins it for the speech path.
//
// Field order in the initialiser MUST match the declaration order in
// combat_strings.h — we use positional init (no designated initialisers
// in this codebase, per patch-build script flag set).

#include "combat_strings.h"

#include "engine_game.h"  // IsKotor2 — K2 anchor deltas in Get()
#include "strings.h"      // for acc::strings::Lang + GetLanguage

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

    // ---- Blaster-deflection breakdown (42417)
    "Reflexionsstatistik: ",       // prefix_reflexion
    " reflektiert Projektil mit ", // reflect_mid_marker
    "%s reflektiert 1 Schuss",     // fmt_deflect_one
    "%s reflektiert %d Sch\xFCsse", // fmt_deflect_many

    // ---- Summary layout: classic <actor><phrase><target>. See kPl for the
    // labelled alternative and why it needs one.
    nullptr,                                    // summary_actor_prefix
    nullptr,                                    // summary_target_marker
    nullptr,                                    // summary_verb_marker
    false,                                      // feat_marker_leads
    nullptr,                                    // damage_amount_marker
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

    // ---- Blaster-deflection breakdown (42417)
    "Deflection Breakdown: ",            // prefix_reflexion
    " deflects projectile with ",        // reflect_mid_marker
    "%s deflects 1 shot",                // fmt_deflect_one
    "%s deflects %d shots",              // fmt_deflect_many

    // ---- Summary layout: classic <actor><phrase><target>. See kPl for the
    // labelled alternative and why it needs one.
    nullptr,                                    // summary_actor_prefix
    nullptr,                                    // summary_target_marker
    nullptr,                                    // summary_verb_marker
    false,                                      // feat_marker_leads
    nullptr,                                    // damage_amount_marker
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

    // ---- Blaster-deflection breakdown (42417)
    "D\xE9""composition de la parade : ",              // prefix_reflexion
    " d\xE9tourne un projectile gr\xE2""ce \xE0 un ",  // reflect_mid_marker
    "%s d\xE9vie 1 tir",                               // fmt_deflect_one
    "%s d\xE9vie %d tirs",                             // fmt_deflect_many

    // ---- Summary layout: classic <actor><phrase><target>. See kPl for the
    // labelled alternative and why it needs one.
    nullptr,                                    // summary_actor_prefix
    nullptr,                                    // summary_target_marker
    nullptr,                                    // summary_verb_marker
    false,                                      // feat_marker_leads
    nullptr,                                    // damage_amount_marker
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

    // ---- Blaster-deflection breakdown (42417)
    "Divisione Respinta: ",                         // prefix_reflexion
    " respinge il proiettile con ",                 // reflect_mid_marker
    "%s devia 1 colpo",                             // fmt_deflect_one
    "%s devia %d colpi",                            // fmt_deflect_many

    // ---- Summary layout: classic <actor><phrase><target>. See kPl for the
    // labelled alternative and why it needs one.
    nullptr,                                    // summary_actor_prefix
    nullptr,                                    // summary_target_marker
    nullptr,                                    // summary_verb_marker
    false,                                      // feat_marker_leads
    nullptr,                                    // damage_amount_marker
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

    // ---- Blaster-deflection breakdown (42417)
    "Colapso de desv\xEDo: ",                // prefix_reflexion
    " desv\xED""a proyectil con ",           // reflect_mid_marker
    "%s desv\xED""a 1 disparo",              // fmt_deflect_one
    "%s desv\xED""a %d disparos",            // fmt_deflect_many

    // ---- Summary layout: classic <actor><phrase><target>. See kPl for the
    // labelled alternative and why it needs one.
    nullptr,                                    // summary_actor_prefix
    nullptr,                                    // summary_target_marker
    nullptr,                                    // summary_verb_marker
    false,                                      // feat_marker_leads
    nullptr,                                    // damage_amount_marker
};

// Russian — engine anchors extracted from Allard 1.72's dialog.tlk on
// 2026-07-25 via `kdev combat-strings-extract --tlk <allard>/dialog.tlk`.
// Speech labels hand-translated (machine-translation quality bar, same as
// fr/it/es — flagged for a native-speaker pass).
//
// Encoding: Windows-1251, NOT 1252. Russian is the first locale whose bytes
// differ from the OS codepage on a typical player's machine, which is why
// prism::SetSpeechCodepage exists — see acc::strings::CodepageFor(Lang::Ru).
//
// Two Russian-specific engine quirks, both verified against the TLK:
//
//  (a) 42042 is "<CUSTOM0> <CUSTOM1> \xE0\xF2\xE0\xEA\xF3\xE5\xF2 <CUSTOM2>"
//      (actor / adverb / "attacks" / target) with the adverb supplied by
//      42043 "\xF3\xF1\xEF\xE5\xF8\xED\xEE" (successfully) or 42044
//      "\xE1\xE5\xE7\xF3\xF1\xEF\xE5\xF8\xED\xEE" (unsuccessfully). So the
//      hit/miss distinction rides a prefix on the same verb, unlike DE/EN
//      where the verb itself changes.
//
//  (b) There is NO status-echo copula. 42158 is bare "<CUSTOM0> <CUSTOM1>"
//      because Russian drops "to be" in the present tense, so the rendered
//      line is "<target> <status>" with only a space between them. A single
//      space is unusable as an anchor: RuleAttackSummary searches for
//      "<target>" + marker inside the summary, and 42119's gap after the
//      target is "  \xF1 " (glue space + "with"), so a " " marker would
//      match there and capture the entire rest of the line as the status.
//      Per combat_strings.h's documented fallback for locales with a
//      different construction, we use a byte the TLK can never contain: the
//      status word is simply not extracted, the line is still claimed and
//      suppressed by prefix_auswirkung, and nothing regresses. Improving
//      this needs a parser change, not a table change.
const MsgStrings kRu = {
    // ---- Engine-side parse anchors (extracted from Allard dialog.tlk)
    " \xF3\xF1\xEF\xE5\xF8\xED\xEE \xE0\xF2\xE0\xEA\xF3\xE5\xF2 ",  // phrase_hit  (42042 + adverb 42043)
    " \xE1\xE5\xE7\xF3\xF1\xEF\xE5\xF8\xED\xEE \xE0\xF2\xE0\xEA\xF3\xE5\xF2 ",  // phrase_miss (42042 + adverb 42044)
    "  \xF1 ",                                  // phrase_mit        (42119, +1 glue space)
    "\xC7\xE0\xF9\xE8\xF2\xFB ",                // word_verteidigung (42119 gap CUSTOM1..CUSTOM2)
    "\xEF\xEE\xE2\xF0\xE5\xE6\xE4\xE5\xED\xE8\xFF ",  // word_schaden_colon (42119 gap CUSTOM2..CUSTOM3)
    " \xE8\xF1\xEF\xEE\xEB\xFC\xE7\xEE\xE2\xE0\xED\xEE.",  // feat_marker (42046 + engine-appended ".")
    "\xC0\xED\xE0\xEB\xE8\xE7 \xE0\xF2\xE0\xEA\xE8: ",     // prefix_angriff   (42146)
    "\xC0\xED\xE0\xEB\xE8\xE7 \xE7\xE0\xF9\xE8\xF2\xFB: ", // prefix_abwehr    (42149)
    "\xC0\xED\xE0\xEB\xE8\xE7 \xF3\xF0\xEE\xED\xE0: ",     // prefix_schaden   (42150)
    "\xC0\xED\xE0\xEB\xE8\xE7 \xEE\xEF\xE0\xF1\xED\xEE\xF1\xF2\xE8:",  // prefix_bedrohung (42148)
    "\xCA\xF0\xE8\xF2\xE8\xF7\xE5\xF1\xEA\xE8\xE9 \xF3\xE4\xE0\xF0!",  // tag_krit_summary (1511)
    "\xC0\xE2\xF2\xEE\xEC\xE0\xF2\xE8\xF7\xE5\xF1\xEA\xE8\xE9 \xF3\xE4\xE0\xF0!",    // tag_auto_hit  (42390)
    "\xC0\xE2\xF2\xEE\xEC\xE0\xF2\xE8\xF7\xE5\xF1\xEA\xE8\xE9 \xEF\xF0\xEE\xEC\xE0\xF5!",  // tag_auto_fail (42391)
    "\xE1\xF0\xEE\xF1\xEE\xEA ",                // token_wuerfel    (42316)
    "\xEC\xEE\xE4\xE8\xF4\xE8\xEA\xE0\xF2\xEE\xF0 \xEB\xEE\xE2\xEA\xEE\xF1\xF2\xE8 ",  // token_gesch_mod (42339)
    "\xC1\xEE\xED\xF3\xF1 \xE1\xEB\xE8\xE7\xEE\xF1\xF2\xE8 ",  // token_entfernung (42330)
    "\xC1\xEE\xED\xF3\xF1 \xFD\xF4\xF4\xE5\xEA\xF2\xE0 ",      // token_effekt     (42332)
    // 42386 renders as "Kriticheskiy KH<CUSTOM0> na". The multiplier marker is
    // a Cyrillic 'kha' (0xF5), NOT a Latin 'x', and the tail carries no
    // trailing space (unlike DE's " f\xFCr ").
    "\xCA\xF0\xE8\xF2\xE8\xF7\xE5\xF1\xEA\xE8\xE9 \xF5",  // krit_x_prefix  (42386)
    " \xED\xE0",                                // phrase_fuer       (42386 tail)
    "\xE1\xEE\xED\xF3\xF1\xED\xFB\xE5 \xEF\xEE\xE2\xF0\xE5\xE6\xE4\xE5\xED\xE8\xFF",  // token_bonusschaden (42155)
    "\xCF\xF0\xE0\xE2\xE0\xFF \xF0\xF3\xEA\xE0",  // hand_main       (42314)

    // ---- Output-side labels (hand-translated)
    // Russian verbs take a preposition where German/English take a direct
    // object, so the connector is folded into the verb: "<actor> попадает в
    // <target>" / "<actor> промахивается по <target>".
    "\xEF\xEE\xEF\xE0\xE4\xE0\xE5\xF2 \xE2",    // verb_hit       "попадает в"
    "\xEF\xF0\xEE\xEC\xE0\xF5\xE8\xE2\xE0\xE5\xF2\xF1\xFF \xEF\xEE",  // verb_miss "промахивается по"
    "\xEA\xF0\xE8\xF2\xE8\xF7\xE5\xF1\xEA\xE8",  // word_critical "критически"
    "\xC0\xF2\xE0\xEA\xE0",                     // word_angriff   "Атака"
    "\xC7\xE0\xF9.",                            // word_vert      "Защ."
    "\xEF\xF0\xEE\xF2\xE8\xE2",                 // word_gg        "против"
    "\xD3\xF0\xEE\xED",                         // word_schaden   "Урон"
    "\xE8\xE7",                                 // word_von       "из"
    "\xC0\xE2\xF2\xEE\xEF\xEE\xEF\xE0\xE4\xE0\xED\xE8\xE5.",  // word_auto_hit  "Автопопадание."
    "\xC0\xE2\xF2\xEE\xEF\xF0\xEE\xEC\xE0\xF5.",              // word_auto_fail "Автопромах."

    // ---- Short replacements. Spelled out rather than clipped to initials:
    // a screen reader renders a two-letter stub as noise, and the project
    // rule is that a short label must never sound like another word.
    "\xE1\xF0\xEE\xF1\xEE\xEA",                 // short_wuerfel    "бросок"
    "\xEB\xEE\xE2\xEA ",                        // short_gesch      "ловк "
    "\xE4\xE8\xF1\xF2 ",                        // short_reichweite "дист "
    "\xFD\xF4\xF4\xE5\xEA\xF2 ",                // short_effekt     "эффект "
    "\xE1\xEE\xED\xF3\xF1",                     // short_bonus      "бонус"

    // ---- Results-only labels + effect/save/damage/kill anchors
    "\xEC\xE8\xEC\xEE",                         // word_failed  "мимо" (idiomatic "off-target")
    "\xC0\xED\xE0\xEB\xE8\xE7 \xCF\xF0\xE8\xEC\xE5\xED\xE5\xED\xED\xFB\xF5 \xDD\xF4\xF4\xE5\xEA\xF2\xEE\xE2:",  // prefix_auswirkung (42157)
    // "urona" (genitive of "damage") is the last word before <CUSTOM1> in
    // 1455 and also appears in the 1454 energy-shield variant, so unlike DE
    // this single anchor covers both absorb forms. The genitive ending keeps
    // it distinct from damage_marker, which carries the accusative "uron v".
    "\xF3\xF0\xEE\xED\xE0",                     // absorb_anchor
    " \xE8\xF1\xEF\xEE\xEB\xFC\xE7\xF3\xE5\xF2 ",  // ability_use_marker (32292)
    // The three save types (1374-1376) share a leading word ("Spasbrosok
    // <kind>"), not a trailing one, so unlike DE there is no common suffix to
    // anchor on; 1406's type/result separator is all that is usable.
    ". ",                                       // save_marker      (1406 gap C1..C2)
    "\xD3\xE4\xE0\xF7\xE0!",                    // save_success     (1392 + "!")
    "\xCD\xE5\xF3\xE4\xE0\xF7\xE0!",            // save_fail        (1393 + "!")
    " \xED\xE0\xED\xB8\xF1 \xF3\xF0\xEE\xED \xE2 ",  // damage_marker (1403; note ё = 0xB8)
    "\xF1\xEE\xEF\xF0\xEE\xF2\xE8\xE2\xEB\xFF\xE5\xF2\xF1\xFF",  // word_resists    "сопротивляется"
    "\xEF\xF0\xEE\xE2\xE0\xEB\xE5\xED\xEE",     // word_save_failed "провалено"
    "\xF3\xE1\xE8\xEB",                         // kill_marker      (1407)

    // ---- Status-echo copula: none in Russian. See quirk (b) above; 0x01
    // cannot occur in TLK text, so the status word is skipped rather than
    // mis-captured.
    "\x01",                                     // status_ist_marker (sentinel)

    // ---- Blaster-deflection breakdown (42417: "Анализ отражения: <CUSTOM0>
    // отражает направленный луч при помощи <CUSTOM1> = <CUSTOM2> против
    // атаки <CUSTOM3>"). Hand-derived — the extractor predates these fields.
    "\xC0\xED\xE0\xEB\xE8\xE7 \xEE\xF2\xF0\xE0\xE6\xE5\xED\xE8\xFF: ",  // prefix_reflexion
    " \xEE\xF2\xF0\xE0\xE6\xE0\xE5\xF2 \xED\xE0\xEF\xF0\xE0\xE2\xEB\xE5\xED\xED\xFB\xE9 \xEB\xF3\xF7 \xEF\xF0\xE8 \xEF\xEE\xEC\xEE\xF9\xE8 ",  // reflect_mid_marker
    // Russian numerals need three plural forms (1 / 2-4 / 5+) and the format
    // API offers two, so the many-form uses the 5+ genitive plural. Correct
    // for 0 and 5+, understandable for 2-4.
    "%s \xEE\xF2\xF0\xE0\xE6\xE0\xE5\xF2 1 \xE2\xFB\xF1\xF2\xF0\xE5\xEB",  // fmt_deflect_one
    "%s \xEE\xF2\xF0\xE0\xE6\xE0\xE5\xF2 %d \xE2\xFB\xF1\xF2\xF0\xE5\xEB\xEE\xE2",  // fmt_deflect_many

    // ---- Summary layout: classic <actor><phrase><target>. See kPl for the
    // labelled alternative and why it needs one.
    nullptr,                                    // summary_actor_prefix
    nullptr,                                    // summary_target_marker
    nullptr,                                    // summary_verb_marker
    false,                                      // feat_marker_leads
    nullptr,                                    // damage_amount_marker
};

// Polish — engine anchors extracted from the LEM dialog.tlk on 2026-07-31 by
// dumping the same strrefs `kdev combat-strings-extract` uses. The extractor
// itself cannot emit this table: it crashes on 42042 because the Polish
// template orders the placeholders <CUSTOM0> <CUSTOM2> <CUSTOM1>, and its
// stitching assumes CUSTOM1 precedes CUSTOM2. Every value below is still taken
// straight from the tlk, by the same per-field rules, so it is transcription
// rather than invention — but see the UNVERIFIED note.
//
// Encoding: Windows-1250, not 1252 — ą ć ę ł ń ś ź ż are absent from 1252
// entirely. See acc::strings::CodepageFor(Lang::Pl).
//
// UNVERIFIED against a live Polish combat log. DE is engine-verified;
// EN/FR/IT/ES were extracted mechanically and still want one in-locale capture;
// Polish needs that capture more than they do, because it is the first locale
// whose *line structure* differs rather than just its words. To verify: play
// one fight on a Polish install and compare `MsgBuf: raw:` against `emit-*` in
// the patch log. Every anchor that does not match falls through to raw speech,
// so a wrong value here is a missing shortening, never a crash.
//
// Four Polish-specific engine quirks, all read off the tlk:
//
//  (a) 42042 is "Atakujący: <CUSTOM0> Cel: <CUSTOM2> Atak: <CUSTOM1>" — the
//      actor sits behind a label, the target comes BEFORE the verb, and the
//      verb is last. phrase_hit/phrase_miss are therefore the bare verbs
//      (42043/42044) matched at the verb position, and the layout is described
//      by summary_actor_prefix / _target_marker / _verb_marker instead. Note
//      42044 "nie trafia" contains 42043 "trafia", which is why the parser
//      tests miss first.
//
//  (b) 42046 is "Użyto atutu: <CUSTOM0>" — the feat label LEADS the name,
//      where DE trails it with " verwendet.". Hence feat_marker_leads.
//
//  (c) 1403 is "<CUSTOM0> trafia. <CUSTOM1> otrzymuje <CUSTOM2> pkt obrażeń" —
//      no colon between target and amount, so damage_amount_marker closes the
//      target instead. The " trafia. " actor marker also appears in a summary
//      line that carries a feat clause, but CombatSummary is registered ahead
//      of CombatDirectDamage and claims that line first.
//
//  (d) Like Russian, there is no status-echo copula: 42158 is a bare
//      "<CUSTOM0> <CUSTOM1>". A single space cannot be used as an anchor, so
//      status_ist_marker takes the same 0x01 sentinel — the status word is not
//      lifted onto the attack line, the echo is still claimed and suppressed by
//      prefix_auswirkung, and nothing regresses.
//
//  Save types 1374-1376 also share a LEADING phrase ("Rzut obronny na …")
//  rather than a trailing one, so save_marker uses 1406's ". " separator, the
//  same fallback Russian needed.
const MsgStrings kPl = {
    // ---- Engine-side parse anchors (LEM dialog.tlk)
    "trafia",                                   // phrase_hit   (42043, bare verb — see quirk (a))
    "nie trafia",                               // phrase_miss  (42044)
    "  za ",                                    // phrase_mit          (42119, +1 glue space)
    "obrona ",                                  // word_verteidigung   (42119 gap CUSTOM1..CUSTOM2)
    "obra\xBF""e\xF1: ",                                // word_schaden_colon  (42119 gap CUSTOM2..CUSTOM3)
    "U\xBFyto atutu: ",                            // feat_marker         (42046, leading label)
    "Szczeg\xF3\xB3y ataku: ",                        // prefix_angriff      (42146)
    "Szczeg\xF3\xB3y obrony: ",                       // prefix_abwehr       (42149)
    "Szczeg\xF3\xB3y obra\xBF""e\xF1: ",                      // prefix_schaden      (42150)
    "Szczeg\xF3\xB3y zagro\xBF""enia:",                    // prefix_bedrohung    (42148)
    "Trafienie krytyczne!",                     // tag_krit_summary    (1511)
    "Automatyczne trafienie!",                  // tag_auto_hit        (42390)
    "Automatyczne pud\xB3o!",                      // tag_auto_fail       (42391)
    "rzut ",                                    // token_wuerfel       (42316)
    "modyfikator ze ZR ",                       // token_gesch_mod     (42339)
    "premia za bliski zasi\xEAg ",                 // token_entfernung    (42330)
    "premia za efekt ",                         // token_effekt        (42332)
    // 42386 is "Trafienie krytyczne x <CUSTOM0> za" — unlike DE there is a
    // space before the multiplier, which atoi skips, and the tail carries no
    // trailing space.
    "Trafienie krytyczne x ",                   // krit_x_prefix       (42386)
    " za",                                      // phrase_fuer         (42386 tail)
    "premia do obra\xBF""e\xF1:",                       // token_bonusschaden  (42155)
    "G\xB3\xF3wna r\xEAka",                              // hand_main           (42314)

    // ---- Output-side labels (hand-translated; machine-translation quality
    // bar, same as fr/it/es/ru — flagged for a native-speaker pass)
    "trafia",                                   // verb_hit
    "chybia",                                   // verb_miss
    "krytycznie",                               // word_critical
    "Atak",                                     // word_angriff
    "Obrona",                                   // word_vert
    "kontra",                                   // word_gg
    "Obra\xBF""enia",                                // word_schaden
    "z",                                        // word_von
    "Automatyczne trafienie.",                  // word_auto_hit
    "Automatyczne pud\xB3o.",                      // word_auto_fail

    // ---- Short replacements. Spelled out rather than clipped to initials, per
    // the project rule that a short label must never sound like another word.
    // "rzut " keeps its trailing space where DE's "W" drops it: "rzut 14" reads,
    // "rzut14" does not.
    "rzut ",                                    // short_wuerfel
    "zr\xEA""czno\x9C\xE6 ",                               // short_gesch
    "zasi\xEAg ",                                  // short_reichweite
    "efekt ",                                   // short_effekt
    "premia",                                   // short_bonus

    // ---- Results-only labels + effect/save/damage/kill anchors
    "chybione",                                 // word_failed
    "Szczeg\xF3\xB3y na\xB3o\xBF""enia efektu:",              // prefix_auswirkung   (42157)
    // The verb shared by both absorb forms: 1455 "zmniejszenie obrażeń
    // absorbuje" and 1456 "odporność absorbuje". (1454 uses "odpiera" and is
    // left on raw speech, as in DE.)
    "absorbuje",                                // absorb_anchor       (1455/1456)
    " u\xBFywa: ",                                 // ability_use_marker  (32292)
    ". ",                                       // save_marker         (1406 gap C1..C2)
    "sukces!",                                  // save_success        (1392 + "!")
    "pora\xBFka!",                                 // save_fail           (1393 + "!")
    " trafia. ",                                // damage_marker       (1403 gap C0..C1)
    "opiera si\xEA",                               // word_resists
    "nieudany",                                 // word_save_failed
    "zabija",                                   // kill_marker         (1407)

    // ---- Status-echo copula: none in Polish. See quirk (d).
    "\x01",                                     // status_ist_marker (sentinel)

    // ---- Blaster-deflection breakdown (42417: "Szczegóły odbicia: <CUSTOM0>
    // odbija pocisk za pomocą <CUSTOM1> = <CUSTOM2> kontra atak <CUSTOM3>")
    "Szczeg\xF3\xB3y odbicia: ",                      // prefix_reflexion
    " odbija pocisk za pomoc\xB9 ",                // reflect_mid_marker
    // Polish counts in three plural forms (1 / 2-4 / 5+) against the two the
    // format API offers, so the many-form uses the 5+ genitive plural: right
    // for 0 and 5+, understandable for 2-4. Same compromise as Russian.
    "%s odbija 1 strza\xB3",                       // fmt_deflect_one
    "%s odbija %d strza\xB3\xF3w",                    // fmt_deflect_many

    // ---- Summary layout: labelled. See quirks (a)-(c).
    "Atakuj\xB9""cy: ",                              // summary_actor_prefix
    " Cel: ",                                   // summary_target_marker
    " Atak: ",                                  // summary_verb_marker
    true,                                       // feat_marker_leads
    " otrzymuje ",                              // damage_amount_marker (1403 gap C1..C2)
};

// ---- KOTOR 2 anchor deltas ------------------------------------------------
//
// The K2 German dialog.tlk keeps every combat strref the parser depends on
// (42042-42150 summary/breakdown block, 1374-1456 save/damage/absorb block,
// 32292 ability use, 42417 deflection) with byte-identical text except four
// anchors, found 2026-08-02 by running `kdev combat-strings-extract` against
// both installs' TLKs and diffing:
//   - 42043 "ist erfolgreich " grew a trailing space -> double space in the
//     glued hit phrase (the miss verb 42044 did not -> phrase_miss unchanged).
//   - 42046 "verwendet " grew a trailing space -> it lands before the
//     engine-appended '.' of the feat clause.
//   - 42157 "Auswirkungsstatistik: " grew a trailing space; RuleAuswirkung
//     slices the target directly after this prefix, so the anchor must carry
//     the space or every target name starts with one.
//   - 32292 swapped verbs: "benutzt" -> "verwendet". Doubles as the spoken
//     ability line's glue (RuleAbilityUse), which stays correct German.
// The auto-hit/-fail tags (42390/42391) also grew trailing spaces, but they
// are strstr'd as substrings, so the shorter K1 form matches either way and
// is kept — it also survives any engine-side trailing-space trim.
//
// Reconstruction caveat as in the header: glue order is assumed identical to
// K1; confirm with one K2 combat capture (grep `MsgBuf: raw:` vs `emit-*`).
// Non-DE locales: no K2 TLKs locally (same Steam language-swap workflow as
// K1); they stay on the K1 anchors, where any mismatch falls through to raw
// speech — no regression.
MsgStrings BuildDeK2() {
    MsgStrings m = kDe;
    m.phrase_hit         = " ist erfolgreich  mit Angriff auf ";
    m.feat_marker        = " verwendet .";
    m.prefix_auswirkung  = "Auswirkungsstatistik: ";
    m.ability_use_marker = " verwendet ";
    return m;
}

}  // namespace

const MsgStrings& Get() {
    if (acc::game::IsKotor2() &&
        acc::strings::GetLanguage() == acc::strings::Lang::De) {
        static const MsgStrings kDeK2 = BuildDeK2();
        return kDeK2;
    }
    switch (acc::strings::GetLanguage()) {
        case acc::strings::Lang::En: return kEn;
        case acc::strings::Lang::De: return kDe;
        case acc::strings::Lang::Fr: return kFr;
        case acc::strings::Lang::It: return kIt;
        case acc::strings::Lang::Es: return kEs;
        case acc::strings::Lang::Ru: return kRu;
        case acc::strings::Lang::Pl: return kPl;
    }
    return kDe;
}

}  // namespace acc::combat::loc
