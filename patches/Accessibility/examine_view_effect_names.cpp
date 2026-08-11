// localized effect-name and effect-icon-name tables.
//
// Split out of examine_view.cpp by the Phase-1 structure pass (refactoring
// candidate 8). This is pure data: the EFFECT_TYPES enum and the
// effecticon.2da row ids mapped to display names, one switch table per
// language, plus the two dispatchers that pick a table from the current
// language.
//
// These are localized inline rather than through strings.h Get(Id) because
// they are keyed by raw engine ids, not by mod string ids - a different
// keyspace from the strings_<lang>.cpp tables. Russian has no table yet
// and falls back to English.
//
// Moved verbatim; no logic here at all beyond the language switch.

#include "examine_view.h"

#include <cstdint>
#include <cstring>

#include "engine_game.h"   // IsKotor2 — K2 icon names resolve via TLK
#include "engine_reads.h"  // LookupTlk
#include "strings.h"

namespace acc::examine_view {

namespace {

// EFFECT_TYPES enum (110 entries) → display name. Unmapped types fall
// back to "Effect #N" in the caller. Localized inline rather than burning
// a strings.h ID per enum entry.

const char* EffectNameEn(int type) {
    switch (type) {
        case 1:   return "Haste";
        case 2:   return "Damage resistance";
        case 3:   return "Slow";
        case 5:   return "Disease";
        case 7:   return "Regeneration";
        case 8:   return "State";
        case 10:  return "Attack bonus";
        case 11:  return "Attack penalty";
        case 12:  return "Damage reduction";
        case 13:  return "Damage bonus";
        case 14:  return "Damage penalty";
        case 15:  return "Temporary hit points";
        case 16:  return "Damage immunity";
        case 17:  return "Damage vulnerability";
        case 18:  return "Entangled";
        case 19:  return "Death";
        case 20:  return "Knocked down";
        case 21:  return "Deaf";
        case 22:  return "Immunity";
        case 25:  return "Arcane spell failure";
        case 26:  return "Saving-throw bonus";
        case 27:  return "Saving-throw penalty";
        case 28:  return "Movement speed bonus";
        case 29:  return "Movement speed penalty";
        case 33:  return "Force resistance bonus";
        case 34:  return "Force resistance penalty";
        case 35:  return "Poisoned";
        case 36:  return "Ability bonus";
        case 37:  return "Ability penalty";
        case 38:  return "Damage";
        case 39:  return "Heal";
        case 43:  return "Droid stunned";
        case 44:  return "Modify number of attacks";
        case 45:  return "Cursed";
        case 46:  return "Silenced";
        case 47:  return "Invisible";
        case 48:  return "Armor class bonus";
        case 49:  return "Armor class penalty";
        case 50:  return "Spell immunity";
        case 53:  return "Taunted";
        case 55:  return "Skill bonus";
        case 56:  return "Skill penalty";
        case 60:  return "Force push";
        case 61:  return "Damage shield";
        case 62:  return "Disguised";
        case 63:  return "Sanctuary";
        case 64:  return "Time stop";
        case 73:  return "Blinded";
        case 75:  return "Miss chance";
        case 76:  return "Concealment";
        case 82:  return "Negative level";
        case 84:  return "Wounding";
        case 87:  return "Disarmed";
        case 90:  return "Force drain";
        case 91:  return "Temporary force points";
        case 92:  return "Blaster deflection bonus";
        case 93:  return "Blaster deflection penalty";
        case 94:  return "Horrified";
        case 95:  return "Force point damage";
        case 96:  return "Force point heal";
        case 97:  return "Choked";
        case 99:  return "Psychic static";
        case 100: return "Lightsaber throw";
        case 101: return "Assured hit";
        case 104: return "Assured deflection";
        case 107: return "Force shield";
        case 108: return "Pure good powers";
        case 109: return "Pure evil powers";
        default:  return nullptr;
    }
}

const char* EffectNameDe(int type) {
    switch (type) {
        case 1:   return "Eile";
        case 2:   return "Schadensresistenz";
        case 3:   return "Verlangsamung";
        case 5:   return "Krankheit";
        case 7:   return "Regeneration";
        case 8:   return "Zustand";
        case 10:  return "Angriffsbonus";
        case 11:  return "Angriffsmalus";
        case 12:  return "Schadensreduktion";
        case 13:  return "Schadensbonus";
        case 14:  return "Schadensmalus";
        case 15:  return "Tempor\xE4re Lebenspunkte";
        case 16:  return "Schadensimmunit\xE4t";
        case 17:  return "Schadensanf\xE4lligkeit";
        case 18:  return "Verwickelt";
        case 19:  return "Tod";
        case 20:  return "Niedergeschlagen";
        case 21:  return "Taub";
        case 22:  return "Immunit\xE4t";
        case 25:  return "Machtversagen";
        case 26:  return "Rettungswurfbonus";
        case 27:  return "Rettungswurfmalus";
        case 28:  return "Tempobonus";
        case 29:  return "Tempomalus";
        case 33:  return "Machtresistenzbonus";
        case 34:  return "Machtresistenzmalus";
        case 35:  return "Vergiftet";
        case 36:  return "Attributsbonus";
        case 37:  return "Attributsmalus";
        case 38:  return "Schaden";
        case 39:  return "Heilung";
        case 43:  return "Droid bet\xE4ubt";
        case 44:  return "Anzahl Angriffe ge\xE4ndert";
        case 45:  return "Verflucht";
        case 46:  return "Verstummt";
        case 47:  return "Unsichtbar";
        case 48:  return "R\xFCstungsbonus";
        case 49:  return "R\xFCstungsmalus";
        case 50:  return "Machtimmunit\xE4t";
        case 53:  return "Verspottet";
        case 55:  return "Talentbonus";
        case 56:  return "Talentmalus";
        case 60:  return "Machtsto\xDF";
        case 61:  return "Schadensschild";
        case 62:  return "Verkleidet";
        case 63:  return "Heiligtum";
        case 64:  return "Zeitstopp";
        case 73:  return "Blind";
        case 75:  return "Verfehlchance";
        case 76:  return "Tarnung";
        case 82:  return "Negative Stufe";
        case 84:  return "Verwundend";
        case 87:  return "Entwaffnet";
        case 90:  return "Machtentzug";
        case 91:  return "Tempor\xE4re Machtpunkte";
        case 92:  return "Blasterabwehrbonus";
        case 93:  return "Blasterabwehrmalus";
        case 94:  return "Entsetzt";
        case 95:  return "Machtpunktschaden";
        case 96:  return "Machtpunktheilung";
        case 97:  return "Erstickt";
        case 99:  return "Psychische Statik";
        case 100: return "Laserschwert werfen";
        case 101: return "Sicherer Treffer";
        case 104: return "Sichere Abwehr";
        case 107: return "Machtschild";
        case 108: return "Reine gute Mächte";
        case 109: return "Reine böse Mächte";
        default:  return nullptr;
    }
}

const char* EffectNameEs(int type) {
    switch (type) {
        case 1:   return "Prisa";
        case 2:   return "Resistencia al da\xF1o";
        case 3:   return "Lentitud";
        case 5:   return "Enfermedad";
        case 7:   return "Regeneraci\xF3n";
        case 8:   return "Estado";
        case 10:  return "Bonificaci\xF3n al ataque";
        case 11:  return "Penalizaci\xF3n al ataque";
        case 12:  return "Reducci\xF3n del da\xF1o";
        case 13:  return "Bonificaci\xF3n al da\xF1o";
        case 14:  return "Penalizaci\xF3n al da\xF1o";
        case 15:  return "Puntos de vida temporales";
        case 16:  return "Inmunidad al da\xF1o";
        case 17:  return "Vulnerabilidad al da\xF1o";
        case 18:  return "Enredado";
        case 19:  return "Muerte";
        case 20:  return "Derribado";
        case 21:  return "Sordo";
        case 22:  return "Inmunidad";
        case 25:  return "Fallo arcano";
        case 26:  return "Bonificaci\xF3n a salvaciones";
        case 27:  return "Penalizaci\xF3n a salvaciones";
        case 28:  return "Bonificaci\xF3n de velocidad";
        case 29:  return "Penalizaci\xF3n de velocidad";
        case 33:  return "Bonificaci\xF3n a resistencia a la Fuerza";
        case 34:  return "Penalizaci\xF3n a resistencia a la Fuerza";
        case 35:  return "Envenenado";
        case 36:  return "Bonificaci\xF3n a caracter\xEDstica";
        case 37:  return "Penalizaci\xF3n a caracter\xEDstica";
        case 38:  return "Da\xF1o";
        case 39:  return "Curaci\xF3n";
        case 43:  return "Droide aturdido";
        case 44:  return "N\xFAmero de ataques modificado";
        case 45:  return "Maldito";
        case 46:  return "Silenciado";
        case 47:  return "Invisible";
        case 48:  return "Bonificaci\xF3n de CA";
        case 49:  return "Penalizaci\xF3n de CA";
        case 50:  return "Inmunidad m\xE1gica";
        case 53:  return "Provocado";
        case 55:  return "Bonificaci\xF3n a habilidad";
        case 56:  return "Penalizaci\xF3n a habilidad";
        case 60:  return "Empuje de Fuerza";
        case 61:  return "Escudo de da\xF1o";
        case 62:  return "Disfrazado";
        case 63:  return "Santuario";
        case 64:  return "Detenci\xF3n del tiempo";
        case 73:  return "Cegado";
        case 75:  return "Probabilidad de fallo";
        case 76:  return "Ocultamiento";
        case 82:  return "Nivel negativo";
        case 84:  return "Herida";
        case 87:  return "Desarmado";
        case 90:  return "Drenaje de Fuerza";
        case 91:  return "Puntos de Fuerza temporales";
        case 92:  return "Bonificaci\xF3n de desv\xEDo de bl\xE1ster";
        case 93:  return "Penalizaci\xF3n de desv\xEDo de bl\xE1ster";
        case 94:  return "Horrorizado";
        case 95:  return "Da\xF1o a Puntos de Fuerza";
        case 96:  return "Curaci\xF3n de Puntos de Fuerza";
        case 97:  return "Ahogado";
        case 99:  return "Est\xE1tica ps\xEDquica";
        case 100: return "Lanzar sable de luz";
        case 101: return "Golpe asegurado";
        case 104: return "Desv\xEDo asegurado";
        case 107: return "Escudo de Fuerza";
        case 108: return "Poderes puros del bien";
        case 109: return "Poderes puros del mal";
        default:  return nullptr;
    }
}

const char* EffectNameFr(int type) {
    switch (type) {
        case 1:   return "H\xE2te";
        case 2:   return "R\xE9sistance aux d\xE9g\xE2ts";
        case 3:   return "Ralentissement";
        case 5:   return "Maladie";
        case 7:   return "R\xE9g\xE9n\xE9ration";
        case 8:   return "\xC9tat";
        case 10:  return "Bonus d'attaque";
        case 11:  return "Malus d'attaque";
        case 12:  return "R\xE9""duction des d\xE9g\xE2ts";
        case 13:  return "Bonus de d\xE9g\xE2ts";
        case 14:  return "Malus de d\xE9g\xE2ts";
        case 15:  return "Points de vie temporaires";
        case 16:  return "Immunit\xE9 aux d\xE9g\xE2ts";
        case 17:  return "Vuln\xE9rabilit\xE9 aux d\xE9g\xE2ts";
        case 18:  return "Entrav\xE9";
        case 19:  return "Mort";
        case 20:  return "Renvers\xE9";
        case 21:  return "Sourd";
        case 22:  return "Immunit\xE9";
        case 25:  return "\xC9""chec arcanique";
        case 26:  return "Bonus de sauvegarde";
        case 27:  return "Malus de sauvegarde";
        case 28:  return "Bonus de vitesse";
        case 29:  return "Malus de vitesse";
        case 33:  return "Bonus de r\xE9sistance \xE0 la Force";
        case 34:  return "Malus de r\xE9sistance \xE0 la Force";
        case 35:  return "Empoisonn\xE9";
        case 36:  return "Bonus de caract\xE9ristique";
        case 37:  return "Malus de caract\xE9ristique";
        case 38:  return "D\xE9g\xE2ts";
        case 39:  return "Soin";
        case 43:  return "Droide \xE9tourdi";
        case 44:  return "Nombre d'attaques modifi\xE9";
        case 45:  return "Maudit";
        case 46:  return "Silenci\xE9";
        case 47:  return "Invisible";
        case 48:  return "Bonus de CA";
        case 49:  return "Malus de CA";
        case 50:  return "Immunit\xE9 aux sorts";
        case 53:  return "Provoqu\xE9";
        case 55:  return "Bonus de comp\xE9tence";
        case 56:  return "Malus de comp\xE9tence";
        case 60:  return "Pouss\xE9""e de Force";
        case 61:  return "Bouclier de d\xE9g\xE2ts";
        case 62:  return "D\xE9guis\xE9";
        case 63:  return "Sanctuaire";
        case 64:  return "Arr\xEAt du temps";
        case 73:  return "Aveugl\xE9";
        case 75:  return "Chance de rat\xE9";
        case 76:  return "Camouflage";
        case 82:  return "Niveau n\xE9gatif";
        case 84:  return "Blessant";
        case 87:  return "D\xE9sarm\xE9";
        case 90:  return "Drain de Force";
        case 91:  return "Points de Force temporaires";
        case 92:  return "Bonus de d\xE9""flexion de blaster";
        case 93:  return "Malus de d\xE9""flexion de blaster";
        case 94:  return "Horrifi\xE9";
        case 95:  return "D\xE9g\xE2ts aux Points de Force";
        case 96:  return "Soin des Points de Force";
        case 97:  return "\xC9touff\xE9";
        case 99:  return "Statique psychique";
        case 100: return "Lancer de sabre laser";
        case 101: return "Toucher assur\xE9";
        case 104: return "D\xE9""flexion assur\xE9""e";
        case 107: return "Bouclier de Force";
        case 108: return "Pouvoirs purement bons";
        case 109: return "Pouvoirs purement mauvais";
        default:  return nullptr;
    }
}

const char* EffectNameIt(int type) {
    switch (type) {
        case 1:   return "Fretta";
        case 2:   return "Resistenza ai danni";
        case 3:   return "Lentezza";
        case 5:   return "Malattia";
        case 7:   return "Rigenerazione";
        case 8:   return "Stato";
        case 10:  return "Bonus di attacco";
        case 11:  return "Malus di attacco";
        case 12:  return "Riduzione dei danni";
        case 13:  return "Bonus ai danni";
        case 14:  return "Malus ai danni";
        case 15:  return "Punti ferita temporanei";
        case 16:  return "Immunit\xE0 ai danni";
        case 17:  return "Vulnerabilit\xE0 ai danni";
        case 18:  return "Impigliato";
        case 19:  return "Morte";
        case 20:  return "Atterrato";
        case 21:  return "Sordo";
        case 22:  return "Immunit\xE0";
        case 25:  return "Fallimento arcano";
        case 26:  return "Bonus al tiro salvezza";
        case 27:  return "Malus al tiro salvezza";
        case 28:  return "Bonus alla velocit\xE0";
        case 29:  return "Malus alla velocit\xE0";
        case 33:  return "Bonus di resistenza alla Forza";
        case 34:  return "Malus di resistenza alla Forza";
        case 35:  return "Avvelenato";
        case 36:  return "Bonus alle caratteristiche";
        case 37:  return "Malus alle caratteristiche";
        case 38:  return "Danni";
        case 39:  return "Cura";
        case 43:  return "Droide stordito";
        case 44:  return "Numero di attacchi modificato";
        case 45:  return "Maledetto";
        case 46:  return "Silenziato";
        case 47:  return "Invisibile";
        case 48:  return "Bonus alla CA";
        case 49:  return "Malus alla CA";
        case 50:  return "Immunit\xE0 agli incantesimi";
        case 53:  return "Provocato";
        case 55:  return "Bonus all'abilit\xE0";
        case 56:  return "Malus all'abilit\xE0";
        case 60:  return "Spinta della Forza";
        case 61:  return "Scudo dei danni";
        case 62:  return "Travestito";
        case 63:  return "Santuario";
        case 64:  return "Arresto del tempo";
        case 73:  return "Accecato";
        case 75:  return "Probabilit\xE0 di mancare";
        case 76:  return "Occultamento";
        case 82:  return "Livello negativo";
        case 84:  return "Ferente";
        case 87:  return "Disarmato";
        case 90:  return "Risucchio della Forza";
        case 91:  return "Punti Forza temporanei";
        case 92:  return "Bonus di deflessione del blaster";
        case 93:  return "Malus di deflessione del blaster";
        case 94:  return "Orripilato";
        case 95:  return "Danni ai Punti Forza";
        case 96:  return "Cura dei Punti Forza";
        case 97:  return "Strozzato";
        case 99:  return "Statica psichica";
        case 100: return "Lancio della spada laser";
        case 101: return "Colpo sicuro";
        case 104: return "Deflessione sicura";
        case 107: return "Scudo della Forza";
        case 108: return "Poteri puramente buoni";
        case 109: return "Poteri puramente malvagi";
        default:  return nullptr;
    }
}

// effecticon.2da row → display name. This is the SIGHTED icon row: the
// engine mirrors each applied EFFECTICON effect into the creature's
// effect-icon array (CSWSEffectListHandler::OnApplyEffectIcon reads the
// row's IconResRef/Priority/Good columns), and the portrait renders those
// icons. Speaking these names is exact sighted parity — one name per icon
// a sighted player sees, in the same priority order.
//
// Names are the game's own: each row label matches a spells.2da row whose
// name strref was resolved in all five locale TLKs (2026-07-17). Hand
// deviations: the two speed abbreviations are expanded (DE "Geschw.-Schub"
// → "Geschwindigkeitsschub" — no homonym abbreviations in speech), droid
// item names drop the "Typ 1" tail (one icon serves all types), and row 59
// (flash-mine stun, no spells row) is hand-labelled. Rows 0/60/61
// (NULL/alignment gauges) stay unmapped.

const char* EffectIconNameEn(int id) {
    switch (id) {
        case 1:  return "Affliction";
        case 2:  return "Burst of Speed";
        case 3:  return "Choke";
        case 4:  return "Disable Droid";
        case 5:  return "Destroy Droid";
        case 6:  return "Fear";
        case 7:  return "Force Armor";
        case 8:  return "Force Aura";
        case 9:  return "Force Immunity";
        case 10: return "Force Valor";
        case 11: return "Force Push";
        case 12: return "Force Shield";
        case 13: return "Force Wave";
        case 14: return "Force Whirlwind";
        case 15: return "Stasis";
        case 16: return "Horror";
        case 17: return "Insanity";
        case 18: return "Kill";
        case 19: return "Knight Valor";
        case 20: return "Knight Speed";
        case 21: return "Master Valor";
        case 22: return "Master Speed";
        case 23: return "Plague";
        case 24: return "Improved Energy Resistance";
        case 25: return "Force Resistance";
        case 26: return "Energy Resistance";
        case 27: return "Stasis Field";
        case 28: return "Slow";
        case 29: return "Stun";
        case 30: return "Stun Droid";
        case 31: return "Wound";
        case 32: return "Adrenaline Shot: Strength";
        case 33: return "Adrenaline Shot: Alacrity";
        case 34: return "Adrenaline Shot: Stamina";
        case 35: return "Hyper Adrenaline Shot: Strength";
        case 36: return "Hyper Adrenaline Shot: Alacrity";
        case 37: return "Hyper Adrenaline Shot: Stamina";
        case 38: return "Combat Shot: Battle Stimulant";
        case 39: return "Combat Shot: Hyper Battle Stimulant";
        case 40: return "Combat Shot: Speed Stimulant";
        case 41: return "Grenade: Stun";
        case 42: return "Grenade: Sonic";
        case 43: return "Grenade: Adhesive";
        case 44: return "Grenade: Cryoban";
        case 45: return "Energy Shield";
        case 46: return "Sith Energy Shield";
        case 47: return "Arkanian Energy Shield";
        case 48: return "Echani Shield";
        case 49: return "Mandalorian Melee Shield";
        case 50: return "Mandalorian Power Shield";
        case 51: return "Echani Dueling Shield";
        case 52: return "Yusanis' Dueling Shield";
        case 53: return "Proto-type Verpine Energy Shield";
        case 54: return "Droid Force Field";
        case 55: return "Droid Stun Ray";
        case 56: return "Droid Flame Thrower";
        case 57: return "Droid Carbonite Stream";
        case 58: return "Droid Gravity Generator";
        case 59: return "Flash mine";
        default: return nullptr;
    }
}

const char* EffectIconNameDe(int id) {
    switch (id) {
        case 1:  return "Leid";
        case 2:  return "Geschwindigkeitsschub";
        case 3:  return "W\xFCrgen";
        case 4:  return "Droiden deaktivieren";
        case 5:  return "Droiden zerst\xF6ren";
        case 6:  return "Angst";
        case 7:  return "Machtr\xFCstung";
        case 8:  return "Machtaura";
        case 9:  return "Machtimmunit\xE4t";
        case 10: return "Macht-Tapferkeit";
        case 11: return "Machtsto\xDF";
        case 12: return "Machtschild";
        case 13: return "Machtwelle";
        case 14: return "Machtwirbelsturm";
        case 15: return "L\xE4hmung";
        case 16: return "Horror";
        case 17: return "Wahnsinn";
        case 18: return "T\xF6ten";
        case 19: return "Ritter-Tapferkeit";
        case 20: return "Ritter-Geschwindigkeit";
        case 21: return "Meister-Tapferkeit";
        case 22: return "Meister-Geschwindigkeit";
        case 23: return "Seuche";
        case 24: return "Verbesserter Energiewiderstand";
        case 25: return "Verbesserter Machtwiderstand";
        case 26: return "Energiewiderstand";
        case 27: return "L\xE4hmungsfeld";
        case 28: return "Langsam";
        case 29: return "Bet\xE4uben";
        case 30: return "Droiden bet\xE4uben";
        case 31: return "Verwunden";
        case 32: return "Adrenalinsto\xDF: St\xE4rke";
        case 33: return "Adrenalinsto\xDF: Eifer";
        case 34: return "Adrenalinsto\xDF: Ausdauer";
        case 35: return "Hyper-Adrenalinsto\xDF: St\xE4rke";
        case 36: return "Hyper-Adrenalinsto\xDF: Eifer";
        case 37: return "Hyper-Adrenalinsto\xDF: Ausdauer";
        case 38: return "Kampfbonus: Kampf-Stimulans";
        case 39: return "Kampfbonus: Hyper-Kampf-Stimulans";
        case 40: return "Kampfbonus: Tempo-Stimulans";
        case 41: return "Granate: Bet\xE4ubung";
        case 42: return "Granate: Schall";
        case 43: return "Granate: Adh\xE4sion";
        case 44: return "Granate: CryoBan";
        case 45: return "Energieschild";
        case 46: return "Sith-Energieschild";
        case 47: return "Arkanischer Energieschild";
        case 48: return "Echani-Schild";
        case 49: return "Mandalorianischer Nahkampfschild";
        case 50: return "Mandalorianischer Energieschild";
        case 51: return "Echani-Duellierschild";
        case 52: return "Yusanis Duellierschild";
        case 53: return "Energieschild-Prototyp der Verpinen";
        case 54: return "Droiden-Energiefeld";
        case 55: return "Droiden-Bet\xE4ubungsstrahl";
        case 56: return "Droiden-Flammenwerfer";
        case 57: return "Droiden-Karbonitstrahl";
        case 58: return "Droiden-Gravitationsgenerator";
        case 59: return "Blitzmine";
        default: return nullptr;
    }
}

const char* EffectIconNameFr(int id) {
    switch (id) {
        case 1:  return "Affliction";
        case 2:  return "Pointe de vitesse";
        case 3:  return "Etouffement";
        case 4:  return "D\xE9sactivation de dro\xEF""de";
        case 5:  return "Destruction de dro\xEF""de";
        case 6:  return "Terreur";
        case 7:  return "Armure de Force";
        case 8:  return "Aura de Force";
        case 9:  return "Immunit\xE9 contre la Force";
        case 10: return "Talent de Force";
        case 11: return "Pouss\xE9""e de Force";
        case 12: return "Bouclier de Force";
        case 13: return "Vague de Force";
        case 14: return "Tourbillon de Force";
        case 15: return "Stase";
        case 16: return "Horreur";
        case 17: return "Insanit\xE9";
        case 18: return "Mise \xE0 mort";
        case 19: return "Talent de chevalier";
        case 20: return "Vitesse de chevalier";
        case 21: return "Talent de ma\xEEtre";
        case 22: return "Vitesse de ma\xEEtre";
        case 23: return "Peste";
        case 24: return "Science de l'\xE9nergie am\xE9lior\xE9""e";
        case 25: return "R\xE9sistance \xE0 la Force";
        case 26: return "R\xE9sistance \xE0 l'\xE9nergie";
        case 27: return "Champ de stase";
        case 28: return "Lenteur";
        case 29: return "Etourdissement";
        case 30: return "Etourdissement de dro\xEF""de";
        case 31: return "Blessure";
        case 32: return "Injection d'adr\xE9naline : force";
        case 33: return "Injection d'adr\xE9naline : promptitude";
        case 34: return "Injection d'adr\xE9naline : endurance";
        case 35: return "Hyper-injection d'adr\xE9naline : force";
        case 36: return "Hyper-injection d'adr\xE9naline : promptitude";
        case 37: return "Hyper-injection d'adr\xE9naline : endurance";
        case 38: return "Injection de combat : stimulant";
        case 39: return "Injection de combat : hyper-stimulant";
        case 40: return "Injection de combat : stimulant de rapidit\xE9";
        case 41: return "Grenade : aveuglante";
        case 42: return "Grenade : sonique";
        case 43: return "Grenade : adh\xE9sive";
        case 44: return "Grenade : cryoban";
        case 45: return "Bouclier \xE0 \xE9nergie";
        case 46: return "Bouclier \xE0 \xE9nergie sith";
        case 47: return "Bouclier \xE0 \xE9nergie arkanien";
        case 48: return "Bouclier \xE9""chani";
        case 49: return "Bouclier de m\xEAl\xE9""e mandalorien";
        case 50: return "Bouclier MF mandalorien";
        case 51: return "Bouclier de duel \xE9""chani";
        case 52: return "Bouclier de duel de Yusanis";
        case 53: return "Prototype de bouclier verpine";
        case 54: return "Champ de force pour dro\xEF""de";
        case 55: return "Rayon \xE9tourdissant pour dro\xEF""de";
        case 56: return "Lance-flammes pour dro\xEF""de";
        case 57: return "Jet de carbonite pour dro\xEF""de";
        case 58: return "G\xE9n\xE9rateur de gravit\xE9 pour dro\xEF""de";
        case 59: return "Mine flash";
        default: return nullptr;
    }
}

const char* EffectIconNameIt(int id) {
    switch (id) {
        case 1:  return "Afflizione";
        case 2:  return "Esplosione di velocit\xE0";
        case 3:  return "Soffocamento";
        case 4:  return "Disattivazione droide";
        case 5:  return "Distruzione droide";
        case 6:  return "Paura";
        case 7:  return "Armatura di Forza";
        case 8:  return "Aura di Forza";
        case 9:  return "Immunit\xE0 alla Forza";
        case 10: return "Valore della Forza";
        case 11: return "Spinta di Forza";
        case 12: return "Scudo di Forza";
        case 13: return "Onda di Forza";
        case 14: return "Tornado di Forza";
        case 15: return "Stasi";
        case 16: return "Orrore";
        case 17: return "Follia";
        case 18: return "Uccisione";
        case 19: return "Valore del Cavaliere";
        case 20: return "Velocit\xE0 del Cavaliere";
        case 21: return "Valore del Maestro";
        case 22: return "Velocit\xE0 del Maestro";
        case 23: return "Pestilenza";
        case 24: return "Resistenza all'energia potenziata";
        case 25: return "Resistenza alla Forza";
        case 26: return "Resistenza all'energia";
        case 27: return "Campo di stasi";
        case 28: return "Lentezza";
        case 29: return "Stordimento";
        case 30: return "Stordimento droide";
        case 31: return "Ferita";
        case 32: return "Scarica di adrenalina: Forza";
        case 33: return "Scarica di adrenalina: Velocit\xE0";
        case 34: return "Scarica di adrenalina: Resistenza";
        case 35: return "Iper-scarica di adrenalina: Forza";
        case 36: return "Iper-scarica di adrenalina: Velocit\xE0";
        case 37: return "Iper-scarica di adrenalina: Resistenza";
        case 38: return "Scarica di combattimento: Stimolante da battaglia";
        case 39: return "Scarica di combattimento: Iper-stimolante da battaglia";
        case 40: return "Scarica di combattimento: Stimolante di velocit\xE0";
        case 41: return "Granata: stordente";
        case 42: return "Granata: sonica";
        case 43: return "Granata: adesiva";
        case 44: return "Granata: Cryoban";
        case 45: return "Scudo di energia";
        case 46: return "Scudo di energia Sith";
        case 47: return "Scudo energia Arkaniano";
        case 48: return "Scudo Echani";
        case 49: return "Scudo da duello Mandaloriano";
        case 50: return "Scudo di potenza Mandaloriano";
        case 51: return "Scudo da duello Echani";
        case 52: return "Scudo da duello di Yusani";
        case 53: return "Prototipo scudo di energia Verpino";
        case 54: return "Droide campo di forza";
        case 55: return "Droide raggio stordente";
        case 56: return "Droide lanciafiamme";
        case 57: return "Droide a flusso di carbonite";
        case 58: return "Droide generatore di gravit\xE0";
        case 59: return "Mina flash";
        default: return nullptr;
    }
}

const char* EffectIconNameEs(int id) {
    switch (id) {
        case 1:  return "Aflicci\xF3n";
        case 2:  return "Potenciador de Velocidad";
        case 3:  return "Estrangular";
        case 4:  return "Desactivar Droide";
        case 5:  return "Destruir Droide";
        case 6:  return "Miedo";
        case 7:  return "Armadura de Fuerza";
        case 8:  return "Aura de Fuerza";
        case 9:  return "Inmunidad a la Fuerza";
        case 10: return "Soltura con la Fuerza";
        case 11: return "Empuj\xF3n con la Fuerza";
        case 12: return "Escudo de Fuerza";
        case 13: return "Ola de Fuerza";
        case 14: return "Remolino de Fuerza";
        case 15: return "Estasis";
        case 16: return "Horror";
        case 17: return "Locura";
        case 18: return "Matar";
        case 19: return "Soltura de Caballero";
        case 20: return "Velocidad de Caballero";
        case 21: return "Soltura Maestra";
        case 22: return "Velocidad Maestra";
        case 23: return "Peste";
        case 24: return "Resistencia a energ\xED""a mejorada";
        case 25: return "Resistencia a la Fuerza";
        case 26: return "Resistencia a la Energ\xED""a";
        case 27: return "Campo de estasis";
        case 28: return "Lento";
        case 29: return "Aturdir";
        case 30: return "Droide Aturdidor";
        case 31: return "Herida";
        case 32: return "Tiro de Adrenalina: Fuerza";
        case 33: return "Tiro de Adrenalina: Aceleraci\xF3n";
        case 34: return "Tiro de Adrenalina: Resistencia";
        case 35: return "Hiper Tiro de Adrenalina: Fuerza";
        case 36: return "Hiper Tiro de Adrenalina: Aceleraci\xF3n";
        case 37: return "Hiper Tiro de Adrenalina: Resistencia";
        case 38: return "Disparo de Combate: Estimulante de Combate";
        case 39: return "Disparo de Combate: Hiper Estimulante de Combate";
        case 40: return "Disparo de Combate: Estimulante de Velocidad";
        case 41: return "Granada: Aturdidora";
        case 42: return "Granada: S\xF3nica";
        case 43: return "Granada: Adhesiva";
        case 44: return "Granada: Cryoban";
        case 45: return "Escudo de Energ\xED""a";
        case 46: return "Escudo de Energ\xED""a Sith";
        case 47: return "Escudo de Energ\xED""a Arkaniano";
        case 48: return "Escudo Echani";
        case 49: return "Escudo Mandaloriano de Refriega";
        case 50: return "Escudo Poderoso Mandaloriano";
        case 51: return "Escudo de Duelo Echani";
        case 52: return "Escudo de Duelo de Yusani";
        case 53: return "Prototipo de Escudo Poderoso Verpine";
        case 54: return "Campo de Fuerza para Droides";
        case 55: return "Rayo Aturdidor para Droides";
        case 56: return "Lanzallamas para Droides";
        case 57: return "Corriente de Carbonita para Droides";
        case 58: return "Generador de Gravedad para Droides";
        case 59: return "Mina de fogonazo";
        default: return nullptr;
    }
}
const char* EffectNamePl(int type) {
    switch (type) {
        case 1:   return "Przyspieszenie";
        case 2:   return "Odporno\x9C\xE6 na obra\xBF""enia";
        case 3:   return "Spowolnienie";
        case 5:   return "Choroba";
        case 7:   return "Regeneracja";
        case 8:   return "Stan";
        case 10:  return "Premia do ataku";
        case 11:  return "Kara do ataku";
        case 12:  return "Redukcja obra\xBF""e\xF1";
        case 13:  return "Premia do obra\xBF""e\xF1";
        case 14:  return "Kara do obra\xBF""e\xF1";
        case 15:  return "Tymczasowe punkty \xBFycia";
        case 16:  return "Niewra\xBFliwo\x9C\xE6 na obra\xBF""enia";
        case 17:  return "Podatno\x9C\xE6 na obra\xBF""enia";
        case 18:  return "Unieruchomiony";
        case 19:  return "\x8Cmier\xE6";
        case 20:  return "Powalony";
        case 21:  return "G\xB3uchy";
        case 22:  return "Niewra\xBFliwo\x9C\xE6";
        case 25:  return "Niepowodzenie magii";
        case 26:  return "Premia do rzut\xF3w obronnych";
        case 27:  return "Kara do rzut\xF3w obronnych";
        case 28:  return "Premia do szybko\x9C""ci";
        case 29:  return "Kara do szybko\x9C""ci";
        case 33:  return "Premia do odporno\x9C""ci na Moc";
        case 34:  return "Kara do odporno\x9C""ci na Moc";
        case 35:  return "Zatruty";
        case 36:  return "Premia do cechy";
        case 37:  return "Kara do cechy";
        case 38:  return "Obra\xBF""enia";
        case 39:  return "Leczenie";
        case 43:  return "Droid og\xB3uszony";
        case 44:  return "Zmieniona liczba atak\xF3w";
        case 45:  return "Przekl\xEAty";
        case 46:  return "Uciszony";
        case 47:  return "Niewidzialny";
        case 48:  return "Premia do klasy pancerza";
        case 49:  return "Kara do klasy pancerza";
        case 50:  return "Niewra\xBFliwo\x9C\xE6 na magi\xEA";
        case 53:  return "Sprowokowany";
        case 55:  return "Premia do umiej\xEAtno\x9C""ci";
        case 56:  return "Kara do umiej\xEAtno\x9C""ci";
        case 60:  return "Pchni\xEA""cie Mocy";
        case 61:  return "Tarcza obra\xBF""e\xF1";
        case 62:  return "Przebrany";
        case 63:  return "Azyl";
        case 64:  return "Zatrzymanie czasu";
        case 73:  return "O\x9Clepiony";
        case 75:  return "Szansa chybienia";
        case 76:  return "Ukrycie";
        case 82:  return "Poziom ujemny";
        case 84:  return "Rana";
        case 87:  return "Rozbrojony";
        case 90:  return "Wyssanie Mocy";
        case 91:  return "Tymczasowe punkty Mocy";
        case 92:  return "Premia do odbijania strza\xB3\xF3w";
        case 93:  return "Kara do odbijania strza\xB3\xF3w";
        case 94:  return "Przera\xBFony";
        case 95:  return "Obra\xBF""enia punkt\xF3w Mocy";
        case 96:  return "Leczenie punkt\xF3w Mocy";
        case 97:  return "Duszony";
        case 99:  return "Zak\xB3\xF3""cenia psychiczne";
        case 100: return "Rzut mieczem \x9Cwietlnym";
        case 101: return "Pewne trafienie";
        case 104: return "Pewne odbicie";
        case 107: return "Tarcza Mocy";
        case 108: return "Czyste moce dobra";
        case 109: return "Czyste moce z\xB3""a";
        default:  return nullptr;
    }
}

const char* EffectIconNamePl(int id) {
    switch (id) {
        case 1:  return "N\xEAkanie";
        case 2:  return "Przyp\xB3yw szybko\x9C""ci";
        case 3:  return "Duszenie";
        case 4:  return "Unieruchom droida";
        case 5:  return "Zniszcz droida";
        case 6:  return "Strach";
        case 7:  return "Pancerz Mocy";
        case 8:  return "Aura Mocy";
        case 9:  return "Niewra\xBFliwo\x9C\xE6 na Moc";
        case 10: return "M\xEAstwo Mocy";
        case 11: return "Pchni\xEA""cie Mocy";
        case 12: return "Tarcza Mocy";
        case 13: return "Fala Mocy";
        case 14: return "Wir Mocy";
        case 15: return "Zast\xF3j";
        case 16: return "Groza";
        case 17: return "Szale\xF1stwo";
        case 18: return "Zabicie";
        case 19: return "M\xEAstwo rycerza";
        case 20: return "Szybko\x9C\xE6 rycerza";
        case 21: return "M\xEAstwo mistrza";
        case 22: return "Szybko\x9C\xE6 mistrza";
        case 23: return "Zaraza";
        case 24: return "Ulepszona odporno\x9C\xE6 na energi\xEA";
        case 25: return "Odporno\x9C\xE6 na Moc";
        case 26: return "Odporno\x9C\xE6 na energi\xEA";
        case 27: return "Pole zastoju";
        case 28: return "Spowolnienie";
        case 29: return "Og\xB3uszenie";
        case 30: return "Og\xB3usz droida";
        case 31: return "Rana";
        case 32: return "Zastrzyk adrenaliny: si\xB3""a";
        case 33: return "Zastrzyk adrenaliny: zwinno\x9C\xE6";
        case 34: return "Zastrzyk adrenaliny: wytrzyma\xB3o\x9C\xE6";
        case 35: return "Hiperzastrzyk adrenaliny: si\xB3""a";
        case 36: return "Hiperzastrzyk adrenaliny: zwinno\x9C\xE6";
        case 37: return "Hiperzastrzyk adrenaliny: wytrzyma\xB3o\x9C\xE6";
        case 38: return "Zastrzyk bojowy: stymulator bojowy";
        case 39: return "Zastrzyk bojowy: hiperstymulator bojowy";
        case 40: return "Zastrzyk bojowy: stymulator szybko\x9C""ci";
        case 41: return "Granat: og\xB3uszaj\xB9""cy";
        case 42: return "Granat: d\x9Fwi\xEAkowy";
        case 43: return "Granat: klej\xB9""cy";
        case 44: return "Granat: krioban";
        case 45: return "Tarcza energetyczna";
        case 46: return "Sitha\xF1ska tarcza energetyczna";
        case 47: return "Arkania\xF1ska tarcza energetyczna";
        case 48: return "Tarcza Echani";
        case 49: return "Mandaloria\xF1ska tarcza bojowa";
        case 50: return "Mandaloria\xF1ska tarcza si\xB3owa";
        case 51: return "Pojedynkowa tarcza Echani";
        case 52: return "Pojedynkowa tarcza Yusanisa";
        case 53: return "Prototypowa tarcza Verpine";
        case 54: return "Pole si\xB3owe droida";
        case 55: return "Promie\xF1 og\xB3uszaj\xB9""cy droida";
        case 56: return "Miotacz ognia droida";
        case 57: return "Strumie\xF1 karbonitu droida";
        case 58: return "Generator grawitacji droida";
        case 59: return "Mina b\xB3yskowa";
        default: return nullptr;
    }
}

// ---------------------------------------------------------------------------
// KOTOR 2 effect-icon names.
//
// K2's effecticon.2da carries a `namestrref` column (K1's does not — its
// names were mined by hand from spells.2da, which is why the K1 tables
// above are static per-language switches). So the K2 table is a single
// language-independent row -> strref map, resolved through the dual-game
// TLK path at speak time — automatically localized to whatever language
// the user's K2 install has. Extracted from K2's effecticon.2da 2026-08-11.
// Rows 0/60/62 (NULL_ICON and the two alignment gauges) carry no strref
// and stay unmapped, matching the K1 tables' skip set.

constexpr uint32_t kEffectIconStrRefK2[] = {
    /*  0 */ 0,      266,    267,    268,    271,    272,    275,    276,
    /*  8 */ 277,    279,    281,    282,    283,    285,    286,    288,
    /* 16 */ 289,    290,    291,    292,    293,    295,    296,    297,
    /* 24 */ 299,    300,    301,    303,    304,    305,    306,    309,
    /* 32 */ 1263,   1271,   1272,   1273,   1274,   1275,   1264,   1276,
    /* 40 */ 1277,   31401,  31404,  31405,  31406,  32136,  32137,  32138,
    /* 48 */ 32139,  32140,  32141,  32142,  32143,  32144,  32184,  32183,
    /* 56 */ 32192,  32197,  32198,  32199,  0,      32201,  0,      108169,
    /* 64 */ 48363,  48364,  48365,  48399,  48400,  48401,  48101,  48102,
    /* 72 */ 48103,  48104,  48105,  48414,  48452,  48453,  48454,  48482,
    /* 80 */ 48483,  48484,  48494,  48495,  48496,  48500,  48501,  48502,
    /* 88 */ 48503,  48504,  48505,  48336,  106491, 32185,  32186,  32188,
    /* 96 */ 32190,  32191,  48668,  48669,  32200,  49137,  49138,  32202,
    /*104 */ 48356,  42426,  42429,  48402,  48403,  48404,  108170, 121910,
    /*112 */ 132463, 42429,
};

// The German TLK abbreviates two shield names ("Mand. Nahkampfschild" /
// "Mand. Energieschild"). Expand them the way the hand-tuned K1 DE table
// does — abbreviations are banned in speech output. Exact-match on the
// full German string, so other locales pass through untouched.
struct TlkSpeechFixup {
    const char* tlk;
    const char* spoken;
};
constexpr TlkSpeechFixup kEffectIconFixupsK2[] = {
    {"Mand. Nahkampfschild", "Mandalorianischer Nahkampfschild"},
    {"Mand. Energieschild",  "Mandalorianischer Energieschild"},
};

// Resolves into a file-static buffer: every caller copies the name into
// its own output before the next EffectIconName call (both consumers are
// single-threaded snprintf-join loops), so one slot is enough.
char g_tlkNameK2[128];

const char* EffectIconNameK2(int id) {
    constexpr int kRows =
        static_cast<int>(sizeof(kEffectIconStrRefK2) /
                         sizeof(kEffectIconStrRefK2[0]));
    if (id < 0 || id >= kRows) return nullptr;
    uint32_t strref = kEffectIconStrRefK2[id];
    if (strref == 0) return nullptr;
    if (!acc::engine::LookupTlk(strref, g_tlkNameK2, sizeof(g_tlkNameK2)) ||
        g_tlkNameK2[0] == '\0') {
        return nullptr;
    }
    for (const TlkSpeechFixup& f : kEffectIconFixupsK2) {
        if (std::strcmp(g_tlkNameK2, f.tlk) == 0) return f.spoken;
    }
    return g_tlkNameK2;
}

}  // namespace

const char* EffectName(int type) {
    // EFFECT_TYPES numbering is IDENTICAL across both games for the shared
    // range: the K1 handler-table registration (InitializeEffects
    // @0x004e4a10) and the K2 twin (0x00593a30) assign matching handlers to
    // matching indices for 3..109 — EFFECTICON is 67 in both. K2 appends
    // 110..117; the only persistent named state among them is 111
    // (Fury / Wookiee Rage — its apply handler 0x00594f50 keys off the
    // Fury/Rage spell rows and writes the stats FuryDamageBonus field), so
    // it resolves via the K2 TLK like the icon names. 110/112..114 are
    // registered as null markers and 115..117 are transient mechanics
    // (instant damage / cleanup) that never sit in an effect list long
    // enough to be spoken; they fall through to the caller's "Effect #N".
    if (type == 111 && acc::game::IsKotor2()) {
        constexpr uint32_t kFuryNameStrRef = 48494;  // spells.2da FORCE_POWER_FURY
        if (acc::engine::LookupTlk(kFuryNameStrRef, g_tlkNameK2,
                                   sizeof(g_tlkNameK2)) &&
            g_tlkNameK2[0] != '\0') {
            return g_tlkNameK2;
        }
        return nullptr;
    }
    switch (acc::strings::GetLanguage()) {
        case acc::strings::Lang::De: return EffectNameDe(type);
        case acc::strings::Lang::Fr: return EffectNameFr(type);
        case acc::strings::Lang::It: return EffectNameIt(type);
        case acc::strings::Lang::Es: return EffectNameEs(type);
        case acc::strings::Lang::Pl: return EffectNamePl(type);
        // No Russian effect-name table yet; English is the fallback.
        case acc::strings::Lang::Ru:
        case acc::strings::Lang::En: break;
    }
    return EffectNameEn(type);
}

const char* EffectIconName(int iconId) {
    // KOTOR 2 keys a different (larger) effecticon.2da and names its rows
    // itself via namestrref — the per-language tables below are K1's rows
    // and would name the wrong effect on K2.
    if (acc::game::IsKotor2()) return EffectIconNameK2(iconId);
    switch (acc::strings::GetLanguage()) {
        case acc::strings::Lang::De: return EffectIconNameDe(iconId);
        case acc::strings::Lang::Fr: return EffectIconNameFr(iconId);
        case acc::strings::Lang::It: return EffectIconNameIt(iconId);
        case acc::strings::Lang::Es: return EffectIconNameEs(iconId);
        case acc::strings::Lang::Pl: return EffectIconNamePl(iconId);
        // No Russian effect-icon table yet; English is the fallback.
        case acc::strings::Lang::Ru:
        case acc::strings::Lang::En: break;
    }
    return EffectIconNameEn(iconId);
}
}  // namespace acc::examine_view
