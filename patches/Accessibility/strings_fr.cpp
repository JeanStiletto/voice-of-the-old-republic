// French string table.
//
// Encoding: Windows-1252 hex escapes for non-ASCII (\xE9=é, \xE8=è,
// \xEA=ê, \xE0=à, \xE2=â, \xE7=ç, \xEE=î, \xEF=ï, \xF4=ô, \xF9=ù,
// \xFB=û, \xFC=ü). Prism's ANSI overload converts from CP_ACP via
// MultiByteToWideChar; on a French Windows install CP_ACP =
// Windows-1252, so the literal bytes pass through unchanged.
//
// Combat speech is fed by combat_strings.cpp::kFr (engine anchors
// extracted from dialog_fr.tlk); this table covers the Id::* speech
// path.

#include "strings.h"

namespace acc::strings::lang_fr {

const char* Get(Id id) {
    switch (id) {
        case Id::CategoryDoor:        return "Porte";
        case Id::CategoryNpc:         return "PNJ";
        case Id::CategoryContainer:   return "Conteneur";
        case Id::CategoryItem:        return "Objet";
        case Id::CategoryLandmark:    return "Lieu";
        case Id::CategoryTransition:  return "Transition";
        case Id::CategoryMapHint:     return "Indication";

        case Id::EmptyDoors:          return "Aucune porte \xE0 port\xE9""e";
        case Id::EmptyNpcs:           return "Aucun PNJ \xE0 port\xE9""e";
        case Id::EmptyContainers:     return "Aucun conteneur \xE0 port\xE9""e";
        case Id::EmptyItems:          return "Aucun objet \xE0 port\xE9""e";
        case Id::EmptyLandmarks:      return "Aucun lieu \xE0 port\xE9""e";
        case Id::EmptyTransitions:    return "Aucune transition \xE0 port\xE9""e";
        case Id::EmptyMapHints:       return "Aucune indication sur cette carte";
        case Id::EmptyAll:            return "Aucun objet \xE0 port\xE9""e";
        case Id::CycleNoTarget:       return "Aucune cible";

        case Id::MapPinNoText:        return "Marqueur";
        case Id::MapPinShiftDashHint: return "Le marqueur ne peut pas \xEAtre atteint directement. Ctrl+Tiret pour activer la balise.";
        case Id::MapPinAltDashUnsupported: return "Marqueur : Alt+Tiret non pris en charge";
        case Id::MapPinInteractHint:  return "Marqueur. Ctrl+Tiret pour activer la balise.";

        case Id::FmtSavedMarkerAutoNumber:   return "Marqueur %d";
        case Id::FmtSavedMarkerAutoWithRoom: return "%s - Marqueur %d";
        case Id::FmtSavedMarkerPlaced:       return "Marqueur enregistr\xE9 : %s";
        case Id::SavedMarkerFailed:          return "Impossible d'enregistrer le marqueur";

        case Id::FmtAnnounceWithClock: return "%s, \xE0 %d heures, %d m\xE8tres";
        case Id::FmtAnnounceNoClock:   return "%s, %d m\xE8tres";
        case Id::FmtCategoryItem:      return "%s. %s";

        case Id::FmtGuidingTo:         return "Guidage vers %s";
        case Id::FmtGuidingFailed:     return "Guidage vers %s \xE9""chou\xE9";
        case Id::GuidanceNoFocus:      return "Aucun objet s\xE9lectionn\xE9";
        case Id::GuidingToPoint:       return "Marche vers le point";

        case Id::MovementCancelled:    return "D\xE9placement annul\xE9";

        // Beacon (Ctrl+-).
        case Id::FmtBeaconStarted:     return "Balise vers %s";
        case Id::BeaconCancelled:      return "Balise annul\xE9""e";
        case Id::FmtBeaconNoPath:      return "Aucun chemin vers %s";
        case Id::BeaconAlreadyAtDest:  return "D\xE9j\xE0 \xE0 destination";
        // Route description.
        // Example: "Itin\xE9raire vers Porte (25 m\xE8tres) : 5 m\xE8tres Nord,
        //           4 m\xE8tres Nord-Est, 6 m\xE8tres Est. Aucune transition."
        case Id::FmtRouteHeader:       return "Itin\xE9raire vers %s (%d m\xE8tres) : %s. %s.";
        case Id::FmtRouteSegment:      return "%d m\xE8tres %s";
        case Id::RouteJoinSeparator:   return ", ";
        case Id::RouteOneTransition:   return "Une transition";
        case Id::RouteNoTransition:    return "Aucune transition";
        case Id::FmtBeaconNextSegment: return "Continuer %d m\xE8tres %s";

        case Id::FmtInteractTalk:      return "Parler \xE0 %s";
        case Id::FmtInteractOpen:      return "Utiliser %s";
        case Id::FmtInteractTake:      return "Ramasser %s";
        case Id::FmtInteractFailed:    return "Interaction avec %s \xE9""chou\xE9""e";
        case Id::FmtInteractEngine:    return "%s %s";
        case Id::FmtInteractRadial:    return "Menu d'action, %s";
        case Id::FmtInteractNoActionsRedirect: return "Aucune action disponible pour %s. Appuyez sur Entr\xE9""e pour activer.";
        case Id::FmtInteractNoActions: return "Aucune action disponible pour %s.";

        case Id::FmtActionBarOpened:      return "Barre d'action colonne %d : %s, %d options";
        case Id::FmtActionBarColumnEmpty: return "La colonne %d est vide";
        case Id::ActionBarColumnEmpty:    return "Colonne vide";
        case Id::FmtActionBarFired:       return "%s utilis\xE9";
        case Id::FmtFireAtPosition:       return "%s, position %d";
        case Id::FmtFireQueueFull:        return "%s, file pleine";
        case Id::ActionBarCancelled:      return "Annul\xE9";

        case Id::MenuCatAttacks:       return "Attaques";
        case Id::MenuCatForcePowers:   return "Pouvoirs de Force";
        case Id::MenuCatItems:         return "Objets";
        case Id::MenuCatSelfPowers:    return "Pouvoirs personnels";
        case Id::MenuCatMedical:       return "M\xE9""dical";           // Médical
        case Id::MenuCatMisc:          return "Divers";
        case Id::MenuCatExplosives:    return "Explosifs";
        case Id::FmtMenuCatMulti:      return "%s : %s, %d options";
        case Id::FmtMenuCatSingle:     return "%s : %s";
        case Id::FmtMenuPlainMulti:    return "%s, %d options";
        case Id::FmtMenuCategoryEmpty: return "%s : vide";

        case Id::NoTooltipAvailable:   return "Aucune description disponible";

        case Id::ContainerEmpty:       return "Vide";
        case Id::ContainerOneItem:     return "1 objet";
        case Id::FmtContainerItems:    return "%d objets";
        case Id::FmtContainerItemAt:   return "%s, %d sur %d";
        case Id::FmtItemStackSuffix:   return "%d en pile";
        case Id::FmtItemChargeSuffix:  return "%d charges";

        case Id::EquipSlotHead:        return "T\xEAte";
        case Id::EquipSlotImplant:     return "Implant";
        case Id::EquipSlotBody:        return "Corps";
        case Id::EquipSlotArmL:        return "Bras gauche";
        case Id::EquipSlotArmR:        return "Bras droit";
        case Id::EquipSlotWeapL:       return "Arme gauche";
        case Id::EquipSlotWeapR:       return "Arme droite";
        case Id::EquipSlotBelt:        return "Ceinture";
        case Id::EquipSlotHands:       return "Mains";

        case Id::FmtEquipSlotItem:     return "%s, %s";
        case Id::FmtEquipSlotEmpty:    return "%s, vide";
        case Id::EquipUnequipped:      return "\xC9quipement retir\xE9";
        case Id::FmtEquipVitality:     return "Vitalit\xE9 %s";
        case Id::FmtEquipDefense:      return "D\xE9""fense %s";
        case Id::FmtEquipAttack:       return "Attaque %s";
        case Id::FmtEquipAttackDual:   return "Attaque gauche %s, droite %s";
        case Id::FmtEquipDamage:       return "D\xE9g\xE2ts %s";
        case Id::FmtEquipDamageDual:   return "D\xE9g\xE2ts gauche %s, droite %s";

        case Id::FmtTransitionArea:    return "Zone : %s";
        case Id::FmtTransitionRoom:    return "Salle : %s";
        case Id::FmtTransitionRoomIndex: return "Salle %d";
        case Id::FmtTransitionLoading: return "Chargement : %s";

        case Id::DoorOpen:             return "ouverte";
        case Id::DoorLocked:           return "verrouill\xE9""e";

        case Id::DirNorth:             return "Nord";
        case Id::DirNortheast:         return "Nord-Est";
        case Id::DirEast:              return "Est";
        case Id::DirSoutheast:         return "Sud-Est";
        case Id::DirSouth:             return "Sud";
        case Id::DirSouthwest:         return "Sud-Ouest";
        case Id::DirWest:              return "Ouest";
        case Id::DirNorthwest:         return "Nord-Ouest";

        case Id::StuckFreeDirsPrefix:  return "Libre";
        case Id::StuckAllBlocked:      return "Tout bloqu\xE9";

        case Id::FmtCompassDegrees:    return "%d degr\xE9s";

        case Id::FmtMapStateOriented:    return "%s. Orient\xE9 \xE0 %d degr\xE9s sur la carte, %s.";
        case Id::FmtMapStateUnknownRoom: return "Orient\xE9 \xE0 %d degr\xE9s sur la carte, %s.";

        case Id::FmtWorldStateOriented:       return "%s. %s.";
        case Id::FmtWorldStateUnknownCluster: return "%s.";

        case Id::MouseLookOn:          return "Vue souris activ\xE9""e";
        case Id::MouseLookOff:         return "Vue souris d\xE9sactiv\xE9""e";

        case Id::ViewModeOn:           return "Mode observation activ\xE9";
        case Id::ViewModeOff:          return "Mode observation d\xE9sactiv\xE9";

        case Id::FmtSaveLoadRow:       return "%s, %s, %s, %d sur %d";
        case Id::FmtSaveLoadRowNoLoc:  return "%s, %d sur %d";

        case Id::LevelUpOpen:          return "Mont\xE9""e de niveau";
        case Id::LevelUpFailed:        return "Mont\xE9""e de niveau \xE9""chou\xE9""e";
        case Id::LevelUpAlreadyOpen:   return "Mont\xE9""e de niveau d\xE9j\xE0 ouverte";

        case Id::PortraitLabel:        return "Portrait";
        case Id::PortraitArrowPrev:    return "Portrait pr\xE9""c\xE9""dent";
        case Id::PortraitArrowNext:    return "Portrait suivant";
        case Id::FmtPortraitArrow:     return "%s : %s";
        case Id::FmtPortraitArrowId:   return "%s %d";
        case Id::PortraitGenderFemale: return "f\xE9minin";
        case Id::PortraitGenderMale:   return "masculin";
        case Id::PortraitRaceAsian:    return "asiatique";
        case Id::PortraitRaceDark:     return "\xE0 peau fonc\xE9""e";
        case Id::PortraitRaceLight:    return "\xE0 peau claire";
        case Id::FmtPortraitDescription: return "%s %s %d";

        case Id::FmtPartyPortraitInTeam:    return "%s, dans l'\xE9quipe";
        case Id::FmtPartyPortraitAvailable: return "%s, disponible";
        case Id::PartySelectionFull:        return "\xC9quipe compl\xE8te";

        case Id::DisabledSuffix:       return ", indisponible";

        case Id::FmtCharSheetClass:    return "%s. ";
        case Id::FmtCharSheetLevel:    return "Niveau %s. ";
        case Id::FmtCharSheetXp:       return "Exp\xE9rience %s sur %s. ";
        case Id::FmtCharSheetHp:       return "Points de vie %s. ";
        case Id::FmtCharSheetFp:       return "Points de Force %s. ";
        case Id::FmtCharSheetStr:      return "Force %s%s%s. ";
        case Id::FmtCharSheetDex:      return "Dext\xE9rit\xE9 %s%s%s. ";
        case Id::FmtCharSheetCon:      return "Constitution %s%s%s. ";
        case Id::FmtCharSheetInt:      return "Intelligence %s%s%s. ";
        case Id::FmtCharSheetWis:      return "Sagesse %s%s%s. ";
        case Id::FmtCharSheetCha:      return "Charisme %s%s%s. ";
        case Id::FmtCharSheetAlignment: return "Alignement %u sur %u.";

        case Id::FmtChargenAttrInfoSuffix:               return "Modificateur %s, Co\xFBt %s";
        case Id::FmtChargenAttrValueChangeBare:          return "%s, points restants %s";
        case Id::FmtChargenAttrValueChangeWithMod:       return "%s, Modificateur %s, points restants %s";
        case Id::FmtChargenAttrValueChangeWithCost:      return "%s, points restants %s, Co\xFBt %s";
        case Id::FmtChargenAttrValueChangeWithModAndCost: return "%s, Modificateur %s, points restants %s, Co\xFBt %s";

        case Id::FmtChargenSkillInfoSuffix:  return "Co\xFBt %s";
        case Id::FmtChargenSkillValueChange: return "%s, points restants %s";

        case Id::ChargenFeatGrantedTitle:    return "Vous recevez ces talents";
        case Id::FmtChargenFeatGrantedRow:   return "%s, %d sur %d";

        case Id::FmtChargenFeatChartCell:    return "%s, %s";
        case Id::ChargenFeatStatusAvailable: return "disponible";
        case Id::ChargenFeatStatusExisting:  return "d\xE9j\xE0 appris";
        case Id::ChargenFeatStatusGranted:   return "accord\xE9 automatiquement";
        case Id::ChargenFeatStatusLocked:    return "pr\xE9requis manquant";
        case Id::ChargenFeatStatusChosen:    return "choisi";

        case Id::EditboxRole:                return "champ de saisie";
        case Id::EditboxEmpty:               return "vide";
        case Id::EditboxEnd:                 return "fin";

        case Id::CombatBegins:               return "D\xE9""but du combat";
        case Id::CombatEnds:                 return "Fin du combat";

        case Id::PcStatNoCharacter:          return "Aucun statut disponible.";

        // Brief is composed in BuildTargetCombatBrief: name, then optional
        // condition / distance / effects / weapons clauses each with a
        // leading space and trailing period.
        case Id::FmtTargetCombatBrief:       return "%s.";
        case Id::FactionHostile:             return "hostile";
        case Id::FactionFriendly:            return "amical";
        case Id::FactionNeutral:             return "neutre";
        case Id::TargetIsDead:               return "mort";

        case Id::FmtBriefCondition:          return " %s.";
        case Id::FmtBriefDistanceMeters:     return " %d m\xE8tres.";
        case Id::FmtBriefEffects:            return " %s.";
        case Id::FmtBriefWielding:           return " %s.";
        case Id::FmtBriefOffHand:            return " main secondaire %s.";
        case Id::FmtBriefEffectsCount:       return " %d effets actifs.";
        case Id::FmtBriefFeatsCount:         return " %d talents.";
        case Id::FmtSelfStatusHp:            return "%d points de vie.";
        case Id::FmtSelfStatusHpOf:          return "%d sur %d points de vie.";
        case Id::FmtSelfStatusFpOf:          return "%d sur %d points de Force.";

        case Id::ExamineOpened:              return "Examen.";
        case Id::ExamineNoTarget:            return "Aucune cible \xE0 examiner.";
        case Id::ExamineFailed:              return "Examen \xE9""chou\xE9.";

        case Id::FmtExamineOpened:           return "Examen : %s. %d entr\xE9""es.";
        case Id::FmtExamineRowOf:            return "%s. %d sur %d.";
        case Id::ExamineViewClosed:          return "Examen ferm\xE9.";
        case Id::FmtExamineRowName:          return "Nom : %s";
        case Id::FmtExamineRowFaction:       return "Disposition : %s";
        case Id::FmtExamineRowHp:            return "Points de vie : %d";
        case Id::FmtExamineRowDistance:      return "Distance : %d m\xE8tres";
        case Id::FmtExamineRowWeapon:        return "Main principale : %s";
        case Id::ExamineRowWeaponNone:       return "Main principale : aucune";
        case Id::FmtExamineRowEffect:        return "Effet : %s";
        case Id::FmtExamineRowFeat:          return "Talent : %s";
        case Id::FmtExamineRowEffectUnknown: return "Effet n\xB0%d";
        case Id::FmtExamineRowFeatUnknown:   return "Talent n\xB0%d";
        case Id::ExamineRowNoEffects:        return "Aucun effet actif";
        case Id::ExamineRowNoFeats:          return "Aucun talent";

        case Id::FmtExamineRowHpFull:        return "Points de vie : %d sur %d";
        case Id::FmtExamineRowLevel:         return "Niveau : %d";
        case Id::FmtExamineRowCondition:     return "\xC9tat : %s";
        case Id::DamageLevel0Healthy:        return "indemne";
        case Id::DamageLevel1Light:          return "l\xE9g\xE8rement bless\xE9";
        case Id::DamageLevel2Wounded:        return "bless\xE9";
        case Id::DamageLevel3Badly:          return "gravement bless\xE9";
        case Id::DamageLevel4Dying:          return "mourant";
        case Id::DamageLevel5Dead:           return "mort";
        case Id::FmtExamineRowOffHand:       return "Main secondaire : %s";
        case Id::FmtExamineRowHead:          return "T\xEAte : %s";
        case Id::FmtExamineRowTorso:         return "Armure : %s";
        case Id::FmtExamineRowHands:         return "Mains : %s";
        case Id::ExamineRowStatusInvisible:  return "Invisible";
        case Id::ExamineRowStatusBlind:      return "Aveugle";

        case Id::FmtQueueOpen:               return "File d'actions, %d actions.";
        case Id::QueueEmpty:                 return "La file d'actions est vide.";
        case Id::FmtQueueRow:                return "%s : %s %s, %d sur %d.";
        case Id::FmtQueueRemoved:            return "Retir\xE9 : %s.";
        case Id::QueueCleared:               return "File vid\xE9""e.";
        case Id::QueueClosed:                return "File ferm\xE9""e.";
        case Id::QueueRemoveFailed:          return "Impossible de retirer cette action.";
        case Id::QueueVerbAttack:            return "Attaquer";
        case Id::QueueVerbCastForce:         return "Lancer un pouvoir de la Force";
        case Id::QueueVerbItemCast:          return "Utiliser un objet";
        case Id::QueueVerbEquip:             return "\xC9quiper";
        case Id::QueueVerbUnequip:           return "D\xE9s\xE9quiper";
        case Id::QueueVerbMove:              return "D\xE9placer";
        case Id::QueueVerbHeal:              return "Soigner";
        case Id::QueueVerbUseTalent:         return "Utiliser un talent";
        case Id::QueueVerbCutscene:          return "Cin\xE9matique";
        case Id::QueueVerbUnknown:           return "Action";

        // Skeleton: max-HP is not yet read safely (suspected engine
        // accessor), so the hit / crit messages omit the "X of Y hp"
        // tail. Args reduced to (attacker, target, damage).
        case Id::FmtAttackHit:               return "%s touche %s pour %d d\xE9g\xE2ts.";
        case Id::FmtAttackMiss:              return "%s rate %s.";
        case Id::FmtAttackCrit:              return "Critique ! %s frappe %s pour %d d\xE9g\xE2ts.";
        case Id::FmtAttackDeflected:         return "L'attaque de %s sur %s est par\xE9""e.";

        case Id::FmtSavingThrowSucceeded:    return "%s r\xE9ussit : %s %d contre %d.";
        case Id::FmtSavingThrowFailed:       return "%s \xE9""choue : %s %d contre %d.";
        case Id::SaveTypeFort:               return "Vigueur";
        case Id::SaveTypeReflex:             return "R\xE9""flexes";
        case Id::SaveTypeWill:               return "Volont\xE9";

        case Id::DialogReplyUnavailable:     return "indisponible";
        case Id::FmtDialogReplyUnavailableRow: return "%s, %s, %d sur %d";

        case Id::MessagesTitleCombatLog:     return "Journal de combat.";
        case Id::MessagesTitleDialogLog:     return "Journal de dialogue.";

        case Id::MapPrevNote:                return "Note pr\xE9""c\xE9""dente";
        case Id::MapNextNote:                return "Note suivante";

        case Id::MapCursorUnexplored:        return "Inexplor\xE9";
        case Id::MapCursorWaypointPOI:       return "Point d'int\xE9r\xEAt";
        case Id::MapCursorOpenArea:          return "Zone ouverte";
        case Id::MapCursorJunction:          return "Carrefour";
        case Id::MapCursorOffPath:           return "Mur";
        case Id::FmtMapCursorCorridor:       return "%s, %.0f m\xE8tres";
        case Id::FmtMapCursorDeadEnd:        return "Cul-de-sac, %s";
        case Id::FmtMapCursorJunctionDirs:   return "Carrefour, %s";
        case Id::FmtMapCursorCorridorDir:    return "%s";
        case Id::MapCursorDoorNoun:          return "Porte";
        case Id::FmtMapCursorDoor:           return "%s %s";
        case Id::FmtMapCursorDoorTransition: return "%s %s vers %s";
        case Id::FmtMapCursorDoorLandmark:   return "%s %s, %s";
        case Id::MapCursorTransitionDoor:    return "Embrasure";
        case Id::FmtMapCursorJunctionDeadEndExit: return "cul-de-sac %s";
        case Id::FmtMapCursorPlazaDirs:      return "Place, %s";
        case Id::AxisNorthSouth:             return "nord-sud";
        case Id::AxisEastWest:               return "est-ouest";

        case Id::FmtStorePriceBuyFinite:    return "Prix %d cr\xE9""dits, stock %d";
        case Id::FmtStorePriceBuyUnlimited: return "Prix %d cr\xE9""dits, stock illimit\xE9";
        case Id::FmtStorePriceSell:         return "Prix %d cr\xE9""dits, vous en avez %d";
        case Id::StoreModeBuy:              return "Mode achat";
        case Id::StoreModeSell:             return "Mode vente";
        case Id::StoreSold:                 return "Vendu";
        case Id::StoreBought:               return "Achet\xE9";
        case Id::StoreCannotSell:           return "Ne peut \xEAtre vendu";
        case Id::StoreCannotBuy:            return "Ne peut \xEAtre achet\xE9";
        case Id::FmtStoreSoldFor:           return "Vendu pour %d cr\xE9""dits";
        case Id::FmtStoreBoughtFor:         return "Achet\xE9 pour %d cr\xE9""dits";

        // ----- Pazaak -----
        case Id::PazaakStart:            return "Pazaak. Appuyez sur C pour votre main, T pour la table.";
        case Id::PazaakEmpty:            return "vide";
        case Id::PazaakFaceDown:         return "face cach\xE9""e";
        case Id::PazaakBoardEmpty:       return "vide";
        case Id::PazaakFmtPlus:          return "plus %d";
        case Id::PazaakFmtMinus:         return "moins %d";
        case Id::PazaakFmtPlain:         return "%d";
        case Id::PazaakFmtFlipBoth:      return "plus ou moins %d";
        case Id::PazaakFmtFlipCurrently: return "%s, actuellement %s";
        case Id::PazaakFmtYouDrew:       return "Vous piochez %s. Votre total %d.";
        case Id::PazaakOverTwenty:       return "Plus de vingt.";
        case Id::PazaakFmtYouPlayed:     return "%s jou\xE9. Votre total %d.";
        case Id::PazaakYourTurn:         return "Votre tour.";
        case Id::PazaakTurnEnded:        return "Tour termin\xE9.";
        case Id::PazaakFmtOppDrew:       return "L'adversaire pioche %s. Total %d.";
        case Id::PazaakFmtOppPlayed:     return "L'adversaire joue %s. Total %d.";
        case Id::PazaakFmtOppStands:     return "L'adversaire reste \xE0 %d.";
        case Id::PazaakFmtYouStand:      return "Vous restez \xE0 %d.";
        case Id::PazaakFmtWinSet:        return "Vous gagnez la manche. %d \xE0 %d.";
        case Id::PazaakFmtLoseSet:       return "Vous perdez la manche. %d \xE0 %d.";
        case Id::PazaakWinMatch:         return "Vous gagnez la partie !";
        case Id::PazaakLoseMatch:        return "Vous perdez la partie.";
        case Id::PazaakTieReplay:        return "\xC9""galit\xE9. Manche rejou\xE9""e.";
        case Id::PazaakFmtHand:          return "Main : %s";
        case Id::PazaakHandEmpty:        return "Main vide.";
        case Id::PazaakFmtYourBoard:     return "Votre table : %s, total %d.";
        case Id::PazaakFmtOppBoard:      return "Table adverse : %s, total %d.";
        case Id::PazaakNoPlayable:       return "Aucune carte \xE0 jouer.";
        case Id::PazaakNotYourTurn:      return "Ce n'est pas votre tour.";
        case Id::PazaakSelectCardFirst:  return "Choisissez d'abord une carte.";
        case Id::PazaakChooseSign:       return "Choisissez le signe. Gauche ou droite pour changer, Entr\xE9""e pour jouer.";
        case Id::PazaakCancelled:        return "Annul\xE9.";
        case Id::PazaakDeckAvailable:    return "%s, %d disponibles";
        case Id::PazaakDeckNoneLeft:     return "%s, plus aucune";
        case Id::PazaakDeckSlotFilled:   return "Emplacement %d : %s";
        case Id::PazaakDeckSlotEmpty:    return "Emplacement %d : vide";
        case Id::PazaakDeckPlay:         return "Jouer, %d sur 10 dans le deck";
        case Id::PazaakDeckAdded:        return "Ajout\xE9 %s. %d sur 10.";
        case Id::PazaakDeckRemoved:      return "Retir\xE9 %s.";
        case Id::PazaakDeckFull:         return "Deck complet.";
        case Id::PazaakFmtOppHand:       return "L'adversaire a %d cartes en main.";
        case Id::PazaakStandLabel:       return "Rester";
        case Id::PazaakEndTurnLabel:     return "Finir le tour";
        case Id::PazaakWagerLess:        return "Diminuer la mise";
        case Id::PazaakWagerMore:        return "Augmenter la mise";
        case Id::PazaakFmtWager:         return "Mise %d sur %d maximum.";
        case Id::PazaakFmtWagerRow:      return "Mise %d. %s";
        case Id::FmtStoreNotEnoughCredits:  return "Cr\xE9""dits insuffisants, il en faut %d, vous en avez %d";
        case Id::JournalQuestItemsButton:   return "Objets de qu\xEAte";

        case Id::FmtCredits:                return "Cr\xE9""dits : %s";

        case Id::WorkbenchSlotWeapon1:       return "Emplacement d'am\xE9lioration 1";
        case Id::WorkbenchSlotWeapon2:       return "Emplacement d'am\xE9lioration 2";
        case Id::WorkbenchSlotWeapon3:       return "Emplacement d'am\xE9lioration 3";
        case Id::WorkbenchSlotSaberCrystal1: return "Emplacement de cristal 1";
        case Id::WorkbenchSlotSaberCrystal2: return "Emplacement de cristal 2";
        case Id::WorkbenchSlotSaberCrystal3: return "Emplacement de cristal 3";
        case Id::WorkbenchSlotSaberCrystal4: return "Emplacement de cristal 4";
        case Id::WorkbenchItemsEmpty:        return "Aucun objet am\xE9liorable dans cette cat\xE9gorie";
        case Id::WorkbenchUpgradesEmpty:     return "Aucune am\xE9lioration compatible dans l'inventaire";
        case Id::WorkbenchSlotInstalled:     return "Am\xE9lioration install\xE9""e";
        case Id::WorkbenchSlotRemoved:       return "Am\xE9lioration retir\xE9""e";
        case Id::WorkbenchSlotNoMatch:       return "Aucune am\xE9lioration correspondante dans l'inventaire";
        case Id::WorkbenchSlotFilled:        return "occup\xE9";
        case Id::WorkbenchSlotPeekEmpty:     return "Emplacement vide, aucune am\xE9lioration install\xE9""e";
        case Id::WorkbenchFmtSlotItem:       return "%s, occup\xE9 par %s";

        case Id::SoundOptionsMovieVolume:    return "Volume des vid\xE9os";

        case Id::SwoopRaceStarted:
            // Terse opener — concatenated with SwoopRaceControls below
            // into one utterance by swoop_race.cpp::AnnounceEntry.
            return "Course de Swoop.";
        case Id::SwoopRaceControls:
            // Short cheat sheet. Full keymap (Enter/Mouse 1 also work,
            // Pause pauses the race, etc.) is in the manual — verbose
            // spoken intro got in the way of the start countdown.
            return "Espace pour acc\xE9l\xE9rer. A et D pour diriger.";
        case Id::SwoopRaceEnded:
            return "Course de Swoop termin\xE9""e.";
        case Id::SwoopRaceObstacleNear:
            return "Obstacle \xE0 %d m\xE8tres";
        case Id::FmtSwoopRaceGear:
            return "Vitesse %d";

        case Id::TurretGameStarted:
            // Terse opener — concatenated with TurretGameControls below
            // into one utterance by turret_game.cpp::AnnounceEntry.
            return "Tourelle.";
        case Id::TurretGameControls:
            return "Visez avec WASD. Espace pour tirer. Q et E choisissent les cibles.";
        case Id::TurretGameEnded:
            return "Tourelle termin\xE9""e.";
        case Id::FmtTurretTarget:
            return "Chasseur %d, %d m\xE8tres";
        case Id::FmtTurretDestroyed:
            return "Chasseur %d d\xE9truit.";  // détruit
        case Id::TurretNoTargets:
            return "Aucune cible.";
        case Id::TurretTargetLost:
            return "Cible perdue.";

        case Id::ModSettingsRootLabel:        return "Param\xE8tres du mod";
        case Id::ModSettingsOpened:           return "Param\xE8tres du mod ouverts";
        case Id::ModSettingsClosed:           return "Param\xE8tres du mod ferm\xE9s";
        case Id::ModSettingExtendedCycling:   return "S\xE9lection d'objets sur toute la carte";
        case Id::ModSettingRoomShapes:        return "Descriptions de la forme des salles";
        case Id::ModSettingWallSounds:        return "Sons de mur";
        case Id::ModSettingHumanSubtitles:    return "Lire les sous-titres des locuteurs doubl\xE9s";
        case Id::ModSettingTurretAutoAim:     return "Vis\xE9" "e automatique";
        case Id::ModSettingSkipIntros:        return "Ignorer les vid\xE9os d'introduction";
        case Id::ModSettingSkipIntrosOnNextLaunch: return "Les vid\xE9os d'introduction seront ignor\xE9""es au prochain lancement.";
        case Id::ModSettingPlayIntrosOnNextLaunch: return "Les vid\xE9os d'introduction seront jou\xE9""es au prochain lancement.";
        case Id::ModSettingSkipIntrosToggleFailed: return "Impossible de basculer les vid\xE9os d'introduction. Les fichiers ont peut-\xEAtre \xE9t\xE9 supprim\xE9""s.";
        case Id::ModSettingStateOn:           return "activ\xE9";
        case Id::ModSettingStateOff:          return "d\xE9sactiv\xE9";
        case Id::FmtModSettingOption:         return "%s : %s";
        case Id::ModSettingCueVolume:         return "Volume des indices sonores";
        case Id::FmtModSettingSlider:         return "%s : %d pour cent";
        case Id::ModSettingUrgentVolume:      return "Volume des annonces vocales";
        case Id::ModSettingUrgentVolumePreview: return "Exemple d'annonce";

        case Id::ModSettingAudioGlossary:           return "Glossaire audio";
        case Id::ModSettingsAudioGlossaryOpened:    return "Glossaire audio ouvert";
        case Id::GlossaryEntryDoorOpen:             return "Porte ouverte";
        case Id::GlossaryEntryDoorClosedMetal:      return "Porte en m\xE9tal ferm\xE9""e";
        case Id::GlossaryEntryDoorClosedWood:       return "Porte en bois ferm\xE9""e";
        case Id::GlossaryEntryDoorClosedStone:      return "Porte en pierre ferm\xE9""e";
        case Id::GlossaryEntryWall:                 return "Mur";
        case Id::GlossaryEntryHazard:               return "Danger";
        case Id::GlossaryEntryCollision:            return "Collision";
        case Id::GlossaryEntryBeaconActive:         return "Balise active";
        case Id::GlossaryEntryBeaconWaypoint:       return "Point interm\xE9""diaire atteint";
        case Id::GlossaryEntryBeaconDestination:    return "Destination atteinte";
        case Id::GlossaryEntrySwoopAccelTick:       return "Tic de la barre de vitesse Swoop";
        case Id::GlossaryEntrySwoopAccelpadBoost:   return "Plaque d'acc\xE9l\xE9ration Swoop";
        case Id::GlossaryEntrySwoopObstacleWarn:    return "Alerte d'obstacle Swoop";
        case Id::GlossaryEntrySwoopWallImpact:      return "Impact contre un mur Swoop";

        case Id::FmtUpdateAvailable:    return "Mise \xE0 jour disponible, version %s. Appuyez sur F5 depuis le menu principal pour installer.";
        case Id::UpdateDownloadStarting: return "D\xE9marrage du t\xE9l\xE9""chargement.";
        case Id::UpdateDownloading:     return "T\xE9l\xE9""chargement de la mise \xE0 jour.";
        case Id::UpdateDownloaded:      return "Mise \xE0 jour t\xE9l\xE9""charg\xE9""e. Fermeture du jeu pour installer.";
        case Id::UpdateFailed:          return "\xC9""chec du t\xE9l\xE9""chargement de la mise \xE0 jour. Appuyez sur F5 pour r\xE9""essayer.";
        case Id::FmtUpdateNotAvailable: return "Aucune mise \xE0 jour disponible. Vous \xEAtes en version %s.";
        case Id::UpdateNotInMenu:       return "Les mises \xE0 jour ne peuvent \xEAtre install\xE9""es que depuis le menu principal.";

        case Id::PanelTitleMainMenu:    return "Menu principal";
        case Id::LoadingPleaseWait:     return "Le jeu charge encore, veuillez patienter.";
        case Id::LoadingStuckWorkaround: return "Le menu ne r\xE9pond toujours pas. Appuyez sur Alt F4 et annulez la bo\xEEte de dialogue de fermeture pour le r\xE9veiller.";

        case Id::GamePaused:            return "Pause.";
        case Id::GameResumed:           return "Pause lev\xE9""e.";

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_fr
