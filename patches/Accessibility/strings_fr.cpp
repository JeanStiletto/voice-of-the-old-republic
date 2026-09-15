// French string table.
//
// Encoding: Windows-1252 hex escapes for non-ASCII (\xE9=?, \xE8=?,
// \xEA=?, \xE0=?, \xE2=?, \xE7=?, \xEE=?, \xEF=?, \xF4=?, \xF9=?,
// \xFB=?, \xFC=?). Prism's ANSI overload converts from CP_ACP via
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
        case Id::MapPinAltDashUnsupported: return "Marqueur : Alt+Tiret non pris en charge";
        case Id::MapPinInteractHint:  return "Marqueur. Ctrl+Tiret pour activer la balise.";

        case Id::FmtSavedMarkerAutoNumber:   return "Marqueur %d";
        case Id::FmtSavedMarkerAutoWithRoom: return "%s - Marqueur %d";
        case Id::FmtSavedMarkerPlaced:       return "Marqueur enregistr\xE9 : %s";
        case Id::SavedMarkerFailed:          return "Impossible d'enregistrer le marqueur";

        case Id::HintBackEntrance:    return "Entr\xE9""e arri\xE8re";
        case Id::HintRebelCorpse:     return "Rebelle mort";
        case Id::HintBanthaHerd:      return "Troupeau de banthas";
        case Id::MapNoteFrontExit:    return "Sortie avant";
        case Id::MapNoteBackExit:     return "Sortie arri\xE8re";

        case Id::FmtAnnounceWithClock: return "%s, \xE0 %d heures, %d m\xE8tres";
        case Id::FmtAnnounceNoClock:   return "%s, %d m\xE8tres";
        case Id::FmtCategoryItem:      return "%s. %s";
        case Id::FmtTrapDetected:      return "Pi\xE8ge d\xE9tect\xE9 : %s";
        case Id::MineNoun:             return "Mine";

        case Id::FmtGuidingTo:         return "Guidage vers %s";
        case Id::FmtGuidingFailed:     return "Guidage vers %s \xE9""chou\xE9";
        case Id::GuidanceNoFocus:      return "Aucun objet s\xE9lectionn\xE9";
        case Id::GuidingToPoint:       return "Marche vers le point";

        case Id::MovementCancelled:    return "D\xE9placement annul\xE9";
        case Id::InteractWayBlocked:   return "D\xE9placement annul\xE9, chemin bloqu\xE9";
        case Id::FmtInteractWayBlockedTarget: return "D\xE9placement annul\xE9, chemin bloqu\xE9. %s, %d m\xE8tres, %s";
        case Id::SpectatorBattleDoomed: return "Les soldats de la R\xE9publique sont perdus. Vous ne pouvez plus les aider. Le chemin est bloqu\xE9. H\xE2tez-vous vers la passerelle\xA0!";

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
        case Id::FmtMedPickerApplyTo:  return "Appliquer \xE0 : %s";
        case Id::FmtInteractNoActionsRedirect: return "Aucune action disponible pour %s. Appuyez sur Entr\xE9""e pour activer.";
        case Id::FmtInteractNoActions: return "Aucune action disponible pour %s.";
        case Id::DoorSealedNoOpen: return "Scell\xE9""e. Elle ne s'ouvre pas.";
        case Id::EndarDoorBattleHint: return "Cette porte reste verrouill\xE9""e jusqu'\xE0 la fin du combat dans cette zone.";
        case Id::EndarStuckReloadHint: return "S'il ne reste aucun ennemi \xE0 proximit\xE9, le tutoriel est peut-\xEAtre bloqu\xE9. Chargez une sauvegarde ant\xE9rieure \xE0 cette salle.";

        case Id::FmtActionBarColumnEmpty: return "La colonne %d est vide";
        case Id::FmtFireAtPosition:       return "%s, position %d";
        case Id::ActionRefused:           return "Impossible.";
        case Id::FmtFireQueueFull:        return "%s, file pleine";
        case Id::ActionMenuClosed:        return "Menu d'action ferm?.";

        case Id::MenuCatAttacks:       return "Attaques";
        case Id::MenuCatForcePowers:   return "Pouvoirs de Force";
        case Id::MenuCatItems:         return "Objets";
        case Id::MenuCatSelfPowers:    return "Pouvoirs personnels";
        case Id::MenuCatMedical:       return "M\xE9""dical";           // M?dical
        case Id::MenuCatMisc:          return "Divers";
        case Id::MenuCatExplosives:    return "Explosifs";
        case Id::MenuCatCombatBehaviour: return "Comportement au combat";
        case Id::FmtMenuCatMulti:      return "%s : %s, %d options";
        case Id::FmtMenuCatSingle:     return "%s : %s";
        case Id::FmtMenuPlainMulti:    return "%s, %d options";
        case Id::FmtMenuCategoryEmpty: return "%s : vide";

        case Id::NoTooltipAvailable:   return "Aucune description disponible";

        case Id::ContainerEmpty:       return "Vide";
        case Id::ContainerOneItem:     return "1 objet";
        case Id::FmtContainerItems:    return "%d objets";
        case Id::FmtContainerItemAt:   return "%s, %d sur %d";
        case Id::ContainerEmptySuffix: return "vide";
        case Id::PlaceableStateCaptive:     return "captif";
        case Id::PlaceableStateFreed:       return "lib\xE9r\xE9";
        case Id::PlaceableStateActive:      return "actif";
        case Id::PlaceableStateDeactivated: return "d\xE9sactiv\xE9";
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
        case Id::FmtEquipStillEquipped: return "Objet toujours \xE9quip\xE9 : %s";
        case Id::FmtWeaponSwitchedOne: return "Armes chang\xE9""es : %s";
        case Id::FmtWeaponSwitchedTwo: return "Armes chang\xE9""es : %s et %s";
        case Id::WeaponSwitchedBare:   return "Armes chang\xE9""es";
        case Id::EquipSetActive:       return "active";
        case Id::EquipSetSecondary:    return "secondaire";
        case Id::FmtEquipVitality:     return "Vitalit\xE9 %s";
        case Id::FmtEquipDefense:      return "D\xE9""fense %s";
        case Id::FmtEquipAttack:       return "Attaque %s";
        case Id::FmtEquipAttackDual:   return "Attaque gauche %s, droite %s";
        case Id::FmtEquipDamage:       return "D\xE9g\xE2ts %s";
        case Id::FmtEquipDamageDual:   return "D\xE9g\xE2ts gauche %s, droite %s";

        case Id::FmtTransitionArea:    return "Zone : %s";
        case Id::FmtTransitionRoomIndex: return "Salle %d";
        case Id::FmtTransitionLoading: return "Chargement : %s";

        case Id::DoorOpen:             return "ouverte";
        case Id::DoorLocked:           return "verrouill\xE9""e";
        case Id::DoorUnlocked:         return "d\xE9""verrouill\xE9""e";
        case Id::DoorCosmetic:         return "d\xE9""corative";

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


        case Id::FmtMapStateOriented:    return "%s. Orient\xE9 \xE0 %d degr\xE9s sur la carte, %s.";
        case Id::FmtMapStateUnknownRoom: return "Orient\xE9 \xE0 %d degr\xE9s sur la carte, %s.";

        case Id::FmtWorldStateOriented:       return "Direction %s.\n%s.";
        case Id::FmtWorldStateUnknownCluster: return "Direction %s.";

        case Id::MouseLookOn:          return "Vue souris activ\xE9""e";
        case Id::MouseLookOff:         return "Vue souris d\xE9sactiv\xE9""e";

        case Id::ViewModeOn:           return "Mode observation activ\xE9";
        case Id::ViewModeOff:          return "Mode observation d\xE9sactiv\xE9";

        case Id::FmtSaveLoadRow:       return "%s, %s, %s, %d sur %d";
        case Id::FmtSaveLoadRowNoLoc:  return "%s, %d sur %d";
        case Id::FmtSaveLoadCharacter: return "%s, sauvegardes : %d";

        case Id::LevelUpOpen:          return "Mont\xE9""e de niveau";
        case Id::LevelUpFailed:        return "Mont\xE9""e de niveau \xE9""chou\xE9""e";
        case Id::LevelUpAlreadyOpen:   return "Mont\xE9""e de niveau d\xE9j\xE0 ouverte";
        case Id::LevelUpNotReady:      return "Pas encore assez d'exp\xE9rience pour monter de niveau"; // exp?rience
        case Id::LevelUpScreenHint:    return "Utilisez les fl\xE8""ches pour choisir une cat\xE9gorie et appuyez sur Entr\xE9""e pour l'ouvrir. R\xE9partissez vos points, puis choisissez Accepter pour terminer."; // fleches categorie Entree Repartissez
        case Id::LevelUpGrantedPopupHint: return "Appuyez sur Entr\xE9""e pour confirmer."; // Entree

        case Id::PortraitLabel:        return "Portrait";
        case Id::FmtPortraitArrow:     return "%s : %s";
        case Id::FmtPortraitArrowId:   return "%s %d";
        case Id::PortraitGenderFemale: return "f\xE9minin";
        case Id::PortraitGenderMale:   return "masculin";
        case Id::PortraitRaceAsian:    return "asiatique";
        case Id::PortraitRaceDark:     return "\xE0 peau fonc\xE9""e";
        case Id::PortraitRaceLight:    return "\xE0 peau claire";
        case Id::PortraitRaceTypeH:    return "type H";
        case Id::FmtPortraitDescription: return "%s %s %d";
        case Id::FmtClassNameWithDescription: return "%s. %s";

        case Id::FmtPartyPortraitInTeam:    return "%s, dans l'\xE9quipe";
        case Id::FmtPartyPortraitAvailable: return "%s, disponible";
        case Id::PartySelectionFull:        return "\xC9quipe compl\xE8te";

        case Id::DisabledSuffix:       return ", indisponible";
        case Id::ToggleOn:             return ", activ\xE9";
        case Id::ToggleOff:            return ", d\xE9sactiv\xE9";
        case Id::FmtSliderValueLabeled: return "%s %u sur %u";
        case Id::FmtSliderValue:       return "%u sur %u";
        case Id::EquipMenuName:        return "\xC9quipement";
        case Id::FmtLevelUpDoStepFirst: return "Terminez d'abord %s.";
        case Id::LevelUpStepLocked:    return "Pas encore disponible.";

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
        case Id::FmtKeyBinding:              return "%s : %s";
        case Id::KeyBindingFixed:            return " (non modifiable)";
        case Id::FmtKeyBindCapture:          return "Appuyez sur la nouvelle touche pour %s";
        case Id::KeyBindNotChangeable:       return "Cette affectation ne peut pas \xeatre modifi\xe9""e";

        case Id::CombatBegins:               return "D\xE9""but du combat";
        case Id::CombatEnds:                 return "Fin du combat";
        case Id::CombatLeaderAtPeace:        return "Hors combat";

        case Id::PcStatNoCharacter:          return "Aucun statut disponible.";

        // Brief is composed in BuildTargetCombatBrief: name, then optional
        // condition / distance / effects / weapons clauses each with a
        // leading space and trailing period.
        case Id::FmtTargetCombatBrief:       return "%s.";
        case Id::FactionHostile:             return "hostile";
        case Id::FactionFriendly:            return "amical";
        case Id::FactionNeutral:             return "neutre";

        case Id::FmtBriefCondition:          return " %s.";
        case Id::FmtBriefDistanceMeters:     return " %d m\xE8tres.";
        case Id::FmtBriefEffects:            return " %s.";
        case Id::FmtBriefWielding:           return " %s.";
        case Id::FmtBriefOffHand:            return " main secondaire %s.";
        case Id::FmtSelfStatusHp:            return "%d points de vie.";
        case Id::FmtSelfStatusHpOf:          return "%d sur %d points de vie.";
        case Id::FmtSelfStatusFpOf:          return "%d sur %d points de Force.";

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
        case Id::QueueVerbUseTalent:         return "Utiliser un talent";
        case Id::QueueVerbUnknown:           return "Action";

        // Skeleton: max-HP is not yet read safely (suspected engine
        // accessor), so the hit / crit messages omit the "X of Y hp"
        // tail. Args reduced to (attacker, target, damage).



        case Id::DialogNoReplyOptions:          return "Aucune option de r\xE9ponse";  // Aucune option de réponse
        case Id::MessagesTitleCombatLog:     return "Journal de combat.";

        case Id::MapPrevNote:                return "Note pr\xE9""c\xE9""dente";
        case Id::MapNextNote:                return "Note suivante";

        case Id::MapCursorUnexplored:        return "Inexplor\xE9";
        case Id::MapCursorWaypointPOI:       return "Point d'int\xE9r\xEAt";
        case Id::MapCursorJunction:          return "Carrefour";
        case Id::FmtMapCursorDeadEnd:        return "Cul-de-sac, %s";
        case Id::FmtMapCursorJunctionDirs:   return "Carrefour, %s";
        case Id::FmtMapCursorCorridorDir:    return "%s";
        case Id::MapCursorDoorNoun:          return "Porte";
        case Id::FmtMapCursorDoor:           return "%s %s";
        case Id::FmtMapCursorDoorTransition: return "%s %s vers %s";
        case Id::FmtMapCursorDoorLandmark:   return "%s %s, %s";
        case Id::FmtMapCursorJunctionDeadEndExit: return "cul-de-sac %s";
        case Id::FmtRampExit:                return "rampe %s";
        case Id::AxisNorthSouth:             return "nord-sud";
        case Id::AxisEastWest:               return "est-ouest";
        case Id::AreaNoun:                   return "Zone";
        case Id::AreaNounLarge:              return "Grande zone";
        case Id::FmtAreaAxisExits:           return "%s %s. Sorties: %s";
        case Id::FmtAreaExits:               return "%s. Sorties: %s";
        case Id::FmtAreaAxisOnly:            return "%s %s";
        case Id::FarAtNorth:                 return "loin au nord";
        case Id::FarAtSouth:                 return "loin au sud";
        case Id::FarAtEast:                  return "loin \xE0 l'est";
        case Id::FarAtWest:                  return "loin \xE0 l'ouest";

        case Id::FmtStorePriceBuyFinite:    return "Prix %d cr\xE9""dits, stock %d";
        case Id::FmtStorePriceBuyUnlimited: return "Prix %d cr\xE9""dits, stock illimit\xE9";
        case Id::FmtStorePriceSell:         return "Prix %d cr\xE9""dits, vous en avez %d";
        case Id::StoreModeBuy:              return "Mode achat";
        case Id::StoreModeSell:             return "Mode vente";
        case Id::FmtCraftCost:              return "Co\xFBt %d";
        case Id::FmtCraftYield:             return "Rapporte %d";
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
        case Id::PazaakFmtCardTiebreaker: return "%s, carte de d\xE9""partage";
        case Id::PazaakCardDouble:       return "carte double";
        case Id::PazaakCardTwoFour:      return "2 ou 4";
        case Id::PazaakCardThreeSix:     return "3 ou 6";
        case Id::PazaakCardValue:        return "carte de valeur";
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
        case Id::PazaakChooseSign:       return "Choisissez le signe. Gauche ou droite pour changer, Entr\xE9""e pour jouer.";
        case Id::PazaakChooseSignAndValue:
            return "Choisissez le signe et la valeur. Gauche ou droite pour le signe, haut ou bas pour la valeur, Entr\xE9""e pour jouer.";
        case Id::PazaakCancelled:        return "Annul\xE9.";
        case Id::PazaakDeckAvailable:    return "%s, %d disponibles";
        case Id::PazaakDeckNoneLeft:     return "%s, plus aucune";
        case Id::PazaakDeckSlotFilled:   return "Emplacement %d : %s";
        case Id::PazaakDeckSlotEmpty:    return "Emplacement %d : vide";
        case Id::PazaakDeckPlay:         return "Jouer, %d sur 10 dans le deck";
        case Id::PazaakDeckCleared:      return "Deck vid\xE9.";
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
        case Id::WorkbenchSlotSaberCrystal5: return "Emplacement de cristal 5";
        case Id::WorkbenchSlotSaberCrystal6: return "Emplacement de cristal 6";
        case Id::WorkbenchItemsEmpty:        return "Aucun objet am\xE9liorable dans cette cat\xE9gorie";
        case Id::WorkbenchUpgradesEmpty:     return "Aucune am\xE9lioration compatible dans l'inventaire";
        case Id::WorkbenchSlotInstalled:     return "Am\xE9lioration install\xE9""e";
        case Id::WorkbenchSlotRemoved:       return "Am\xE9lioration retir\xE9""e";
        case Id::WorkbenchSlotNoMatch:       return "Aucune am\xE9lioration correspondante dans l'inventaire";
        case Id::WorkbenchSlotFilled:        return "occup\xE9";
        case Id::WorkbenchSlotPeekEmpty:     return "Emplacement vide, aucune am\xE9lioration install\xE9""e";
        case Id::WorkbenchFmtSlotItem:       return "%s, occup\xE9 par %s";
        case Id::WorkbenchPickerInstalled:   return "install\xE9";
        case Id::WorkbenchPickerNone:        return "Aucun objet";
        case Id::WorkbenchPickerUnnamed:     return "Am\xE9lioration";
        case Id::WorkbenchUpgradeFailed:     return "Am\xE9lioration impossible";
        case Id::WorkbenchModeCreate:        return "Mode fabrication";
        case Id::WorkbenchModeBreakdown:     return "Mode d\xE9montage";
        case Id::FmtWorkbenchUpgradeTitle:   return "Am\xE9lioration : %s";

        case Id::SoundOptionsMovieVolume:    return "Volume des vid\xE9os";

        case Id::SwoopRaceStarted:
            // Terse opener ? concatenated with SwoopRaceControls below
            // into one utterance by swoop_race.cpp::AnnounceEntry.
            return "Course de Swoop.";
        case Id::SwoopRaceControls:
            // Short cheat sheet. Full keymap (Enter/Mouse 1 also work,
            // Pause pauses the race, etc.) is in the manual ? verbose
            // spoken intro got in the way of the start countdown.
            return "Espace pour monter une vitesse au signal sonore. Dirigez avec A et D. \xC9vitez les obstacles et touchez les plaques d'acc\xE9l\xE9ration pour battre le temps.";
        case Id::SwoopRaceControlsK2:
            return "W pour monter une vitesse au signal sonore, Espace pour sauter un obstacle. Dirigez avec A et D. \xC9vitez les obstacles et touchez les plaques d'acc\xE9l\xE9ration pour battre le temps.";
        case Id::SwoopRaceStuckHint:
            return "Bloqu\xE9. D\xE9portez-vous avec A ou D, puis appuyez sur Espace et W ensemble.";
        case Id::SwoopRaceWrecked:
            return "Swoop d\xE9truit. Course termin\xE9""e.";
        case Id::SwoopRaceEnded:
            return "Course de Swoop termin\xE9""e.";
        case Id::FmtSwoopRaceGear:
            return "Vitesse %d";
        case Id::FmtSwoopRaceTime:
            return "Temps : %d,%02d secondes. Course de Swoop termin\xE9""e.";

        case Id::TurretGameStarted:
            // Terse opener ? concatenated with TurretGameControls below
            // into one utterance by turret_game.cpp::AnnounceEntry.
            return "Tourelle.";
        case Id::TurretGameControls:
            return "Visez avec WASD. Espace pour tirer. Q et E choisissent les cibles.";
        case Id::TurretGameEnded:
            return "Tourelle termin\xE9""e.";
        case Id::FmtTurretTarget:
            return "Chasseur %d, %d m\xE8tres";
        case Id::FmtTurretDestroyed:
            return "Chasseur %d d\xE9truit.";  // d?truit
        case Id::TurretNoTargets:
            return "Aucune cible.";

        case Id::ModSettingsRootLabel:        return "Param\xE8tres du mod";
        case Id::ModSettingsOpened:           return "Param\xE8tres du mod ouverts";
        case Id::ModSettingsClosed:           return "Param\xE8tres du mod ferm\xE9s";
        case Id::ModSettingExtendedCycling:   return "S\xE9lection d'objets sur toute la carte";
        case Id::ModSettingRoomShapes:        return "Descriptions de la forme des salles";
        case Id::ModSettingWallSounds:        return "Sons de mur";
        case Id::ModSettingHumanSubtitles:    return "Lire les sous-titres des locuteurs doubl\xE9s";
        case Id::ModSettingSubtitleSapi:      return "Lire les sous-titres avec la voix des annonces";
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
        case Id::ModSettingUrgentRate:        return "Vitesse des annonces vocales";
        case Id::ModSettingUrgentVoice:       return "Voix des annonces";
        case Id::ModSettingUrgentVoiceUnavailable: return "non disponible";
        case Id::ModSettingUrgentVolumePreview: return "Exemple d'annonce";
        case Id::ModSettingVoiceSettings:           return "R\xE9glages de la voix des annonces";
        case Id::ModSettingsVoiceSettingsOpened:    return "R\xE9glages de la voix des annonces ouverts";
        case Id::ModSettingSupportModder:           return "Soutenir le moddeur";
        case Id::ModSettingLatestChanges:           return "Derniers changements";
        case Id::ModSettingOpenReadme:              return "Ouvrir le guide";
        case Id::ModSettingLinkOpened:              return "Ouverture dans le navigateur.";
        case Id::ModSettingLinkFailed:              return "Impossible d'ouvrir le lien.";

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
        case Id::GlossaryEntrySwoopAccelpadBoost:   return "Plaque d'acc\xE9l\xE9ration Swoop";
        case Id::GlossaryEntrySwoopObstacleWarn:    return "Alerte d'obstacle Swoop";
        case Id::GlossaryEntrySwoopWallImpact:      return "Impact contre un mur Swoop";
        case Id::GlossaryEntrySwoopAligned:         return "Swoop sur la trajectoire";
        case Id::GlossaryEntrySwoopShiftReady:      return "Swoop changement de vitesse pr\xEAt";
        case Id::GlossaryEntrySwoopJumpNow:         return "Swoop sauter maintenant";

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

        case Id::GalaxyMapTitle:        return "Carte de la galaxie";

        // ---- Help system ----
        case Id::HelpGroupGeneral:      return "Navigation";
        case Id::HelpGroupMovement:     return "D\xE9placement et cam\xE9ra";
        case Id::HelpGroupInteraction:  return "Cibles et interaction";
        case Id::HelpGroupCombat:       return "Combat et actions";
        case Id::HelpGroupExploration:  return "Exploration et orientation";
        case Id::HelpGroupScreens:      return "\xC9""crans";
        case Id::HelpGroupMap:          return "Carte";
        case Id::HelpGroupPazaak:              return "Pazaak";
        case Id::HelpGroupMod:          return "Fonctions du mod";
        case Id::HelpGroupController:    return "Manette";

        case Id::HelpKeyUpDown:          return "Fl\xE8""ches haut et bas : parcourir les listes et les entr\xE9""es de menu";
        case Id::HelpKeyLeftRight:       return "Fl\xE8""ches gauche et droite : changer de cat\xE9gorie ou modifier une valeur";
        case Id::HelpKeyHomeEnd:         return "D\xE9""but et Fin : aller \xE0 la premi\xE8re ou derni\xE8re entr\xE9""e";
        case Id::HelpKeyEnter:           return "Entr\xE9""e : activer l'\xE9l\xE9ment s\xE9lectionn\xE9";
        case Id::HelpKeyEsc:             return "\xC9""chap : fermer l'\xE9""cran ou revenir";
        case Id::HelpKeyReadDescription: return "Maj plus une fl\xE8""che : lire la description compl\xE8te sans se d\xE9placer";
        case Id::HelpKeySwitchWindows:   return "Q et E : changer de fen\xEAtre ou d'onglet, d'\xE9""cran de menu, et de mode dans les boutiques et conteneurs";
        case Id::HelpKeyF1:              return "F1 : ouvrir ou fermer cette liste de touches";
        case Id::HelpKeyCtrlF1:          return "Ctrl plus F1 : lire les touches de l'\xE9""cran actuel";

        case Id::HelpKeyWalk:           return "W et S : avancer et reculer";
        case Id::HelpKeyCameraRotate:   return "Z et C : tourner la cam\xE9ra \xE0 gauche et \xE0 droite";
        case Id::HelpKeyStrafe:         return "A et D : pas de c\xF4t\xE9 \xE0 gauche et \xE0 droite";
        case Id::HelpKeyPause:          return "Espace : mettre en pause et reprendre";
        case Id::HelpKeyViewMode:       return "B : mode observation, rester sur place en tournant la cam\xE9ra";
        case Id::HelpKeySwitchLeader:   return "Tab : changer de membre du groupe contr\xF4l\xE9";

        case Id::HelpKeyCycleTargets:   return "Q et E : faire d\xE9""filer les objets actuellement visibles";
        case Id::HelpKeyInteract:       return "Entr\xE9""e : interagir avec la cible vis\xE9""e ou l'attaquer";
        case Id::HelpKeyOpenActionMenu: return "Maj plus Entr\xE9""e : ouvrir le menu d'action de la cible vis\xE9""e";
        case Id::HelpKeySelfStatus:     return "H : annoncer votre sant\xE9, vos effets et votre arme";
        case Id::HelpKeyAnnounceFocus:  return "Moins : annoncer l'objet point\xE9";
        case Id::HelpKeyWalkToFocus:    return "Maj plus moins : marcher vers l'objet point\xE9";
        case Id::HelpKeyBeacon:         return "Ctrl plus moins : lancer un guide vers l'objet point\xE9";
        case Id::HelpKeyDialogRepeat:   return "R : r\xE9p\xE9ter la r\xE9plique en cours";

        case Id::HelpKeyCycleObjects:   return "Virgule et point : faire d\xE9""filer les objets d\xE9j\xE0 d\xE9""couverts de la cat\xE9gorie actuelle";
        case Id::HelpKeyCycleCategory:  return "Maj plus virgule ou point : cat\xE9gorie pr\xE9""c\xE9""dente ou suivante";
        case Id::HelpKeyCycleEnds:      return "Ctrl plus virgule ou point : aller \xE0 l'objet le plus proche ou le plus \xE9loign\xE9";
        case Id::HelpKeyHeading:        return "Alt droite : annoncer votre cap exact en degr\xE9s";
        case Id::HelpKeyCameraOrient:   return "N : tourner la cam\xE9ra vers la direction suivante, ou faire face au prochain point de passage";
        case Id::HelpKeyDropMarker:     return "Maj plus N : poser un marqueur de carte \xE0 votre position";

        case Id::FmtHelpNumberActions:   return "1 \xE0 7 : utiliser la derni\xE8re action d'une cat\xE9gorie. 1 %s, 2 %s, 3 %s, 4 %s, 5 %s, 6 %s, 7 %s";
        case Id::HelpKeyOpenCategory:    return "Maj plus 1 \xE0 7 : ouvrir cette cat\xE9gorie pour choisir une action";
        case Id::FmtHelpNumberActions8:  return "8 : appliquer le %s actuel. Maj plus 8 ouvre la cat\xE9gorie pour en choisir un autre";
        case Id::HelpKeyActionQueue:     return "Maj plus H : ouvrir la file d'actions";
        case Id::HelpKeyLevelUp:         return "Maj plus L : ouvrir l'\xE9""cran de niveau";
        case Id::HelpKeyCancelCombat:    return "F : annuler le combat";

        case Id::HelpKeyScreenMap:       return "M : ouvrir la carte";
        case Id::HelpKeyScreenMessages:  return "J : messages et retours";
        case Id::HelpKeyScreenQuests:    return "L : qu\xEAtes";
        case Id::HelpKeyScreenAbilities: return "K : comp\xE9tences, dons et pouvoirs de Force";
        case Id::HelpKeyScreenCharacter: return "P : fiche de personnage";
        case Id::HelpKeyScreenInventory: return "I : inventaire du groupe";
        case Id::HelpKeyScreenEquip:     return "U : \xE9quiper le personnage";
        case Id::HelpKeyScreenOptions:   return "O : options";

        case Id::HelpKeyMapCursor:       return "W, A, S et D : d\xE9placer le curseur de carte pour lire le terrain et les marqueurs";
        case Id::HelpKeyMapPosition:     return "Alt droite : annoncer votre position et orientation sur la carte";
        case Id::HelpKeyPazaakNav:             return "Fl\xE8""ches : passer de votre main aux deux tables et aux actions. Entr\xE9""e joue la carte s\xE9lectionn\xE9""e";
        case Id::HelpKeyPazaakSign:            return "Sur une carte plus-moins : gauche ou droite choisit le signe, Entr\xE9""e la joue, \xC9""chap annule";
        case Id::HelpKeyPazaakReview:          return "C : votre main. T : les deux tables. Maj et C : combien de cartes l'adversaire garde en main";
        case Id::HelpKeyPazaakTurn:            return "S : rester. E : terminer le tour";


        case Id::HelpKeyModSettings:     return "Les r\xE9glages du mod sont dans les Options, en bas de la liste";
        case Id::HelpKeyCopyLastSpoken:  return "Ctrl plus R : copier le dernier texte lu dans le presse-papiers";
        case Id::HelpKeyPadMenuNav:          return "Dans les menus : croix directionnelle ou stick gauche pour se d\xE9placer, A valide, B revient en arri\xE8re";
        case Id::HelpKeyPadTooltip:          return "Maintenir Y et appuyer sur la croix directionnelle vers le haut ou le bas : lire toute la description sans changer d'entr\xE9" "e";
        case Id::HelpKeyPadInteract:          return "A : action par d\xE9" "faut sur la cible actuelle (attaquer, ouvrir, parler, ramasser)";
        case Id::HelpKeyPadCycleObjects:     return "Croix gauche et droite : objet pr\xE9" "c\xE9" "dent et suivant";
        case Id::HelpKeyPadCycleCategory:    return "Croix haut et bas : cat\xE9gorie pr\xE9" "c\xE9" "dente et suivante";
        case Id::HelpKeyPadMapHints:         return "\xC9" "cran de carte, croix gauche et droite : indication pr\xE9" "c\xE9" "dente et suivante";
        case Id::HelpKeyPadActionMenuToggle: return "G\xE2" "chette gauche : ouvre le menu d'actions ; appuyer de nouveau le ferme";
        case Id::HelpKeyPadActionMenuNav:    return "Menu d'actions, croix : gauche et droite changent de cat\xE9gorie, haut et bas d'entr\xE9" "e, A d\xE9" "clenche, B ou la g\xE2" "chette gauche ferme";
        case Id::HelpKeyPadSelfStatus:       return "G\xE2" "chette gauche avec X : votre propre \xE9tat";
        case Id::HelpKeyPadActionQueue:      return "G\xE2" "chette gauche avec B : la file d'actions. La croix haut et bas parcourt les entr\xE9" "es, A retire la derni\xE8re mise en file, Y vide toute la file, B ferme";
        case Id::HelpKeyPadWalkToFocus:      return "G\xE2" "chette droite avec bouton de tranche droit : marcher jusqu'\xE0 l'objet cibl\xE9";
        case Id::HelpKeyPadBeacon:           return "G\xE2" "chette gauche avec bouton de tranche gauche : balise sonore vers l'objet cibl\xE9";
        case Id::HelpKeyPadDegrees:          return "G\xE2" "chette droite : annoncer l'orientation en degr\xE9s";
        case Id::HelpKeyPadCameraOrient:     return "Pression du stick droit : tourner la cam\xE9ra vers le prochain point de la balise, sinon vers le point cardinal suivant";
        case Id::HelpKeyPadHelp:             return "Les deux g\xE2" "chettes : touches de cet \xE9" "cran. L'entr\xE9" "e Aide du menu rapide ouvre cette liste";
        case Id::HelpKeyPadQuickMenu:        return "Y : menu rapide";
        case Id::HelpKeyPadCycleTargets:     return "Boutons de tranche gauche et droit : changer de cible";
        case Id::HelpKeyPadTradeMode:        return "Conteneur et boutique, boutons de tranche : changer de mode";
        case Id::HelpKeyPadOptions:          return "Bouton Retour : menu des options";
        case Id::HelpKeyPadPause:            return "Start : pause";
        case Id::HelpKeyPadSwitchLeader:     return "X : changer de chef de groupe";

        // Controller rows added with the Pazaak board bindings, the D-Pad's
        // hold-to-jump gesture and the map's marker press.
        case Id::HelpKeyPadCycleEnds:
            return "Maintenir la croix \xE0 gauche ou \xE0 droite : aller \xE0 l'objet le plus proche ou le plus \xE9loign\xE9";
        case Id::HelpKeyPadListEnds:
            return "Maintenir la croix en haut ou en bas : aller \xE0 la premi\xE8re ou \xE0 la derni\xE8re entr\xE9" "e d'une liste";
        case Id::HelpKeyPadDropMarker:
            return "\xC9" "cran de carte, pression du stick droit : poser un rep\xE8re \xE0 la position du curseur";
        case Id::HelpKeyPadDialogRepeat:
            return "En dialogue, les deux g\xE2" "chettes : r\xE9p\xE9ter la ligne en cours";
        case Id::HelpKeyPadPazaakNav:
            return "Plateau de Pazaak, croix : haut et bas passent entre votre main, les deux plateaux et les actions ; gauche et droite s'y d\xE9placent. A joue la carte, B annule le choix du signe";
        case Id::HelpKeyPadPazaakSign:
            return "Plateau de Pazaak, sur une carte plus-moins : gauche et droite choisissent le signe, A la joue, B annule";
        case Id::HelpKeyPadPazaakTurn:
            return "Plateau de Pazaak, Y : Rester. X : Finir le tour";

        // Controller rows that close the last of the keyboard-only gaps: the
        // stick pair, the two routes into the game's screens, and the face-button
        // chord grid.
        case Id::HelpKeyPadMove:
            return "Stick gauche : marcher et faire un pas de c\xF4t\xE9. Stick droit : tourner la cam\xE9ra et, avec elle, votre direction de marche";
        case Id::HelpKeyPadTabs:
            return "Boutons de tranche gauche et droit : changer d'onglet et passer d'un \xE9" "cran de jeu \xE0 l'autre";
        case Id::HelpKeyPadScreens:
            return "Y, entr\xE9" "e Menus : ouvrir les \xE9" "crans de jeu. Les boutons de tranche passent ensuite entre carte, messages, qu\xEAtes, aptitudes, personnage, inventaire et \xE9quipement";
        case Id::HelpKeyPadMapCursor:
            return "\xC9" "cran de carte, stick gauche : d\xE9placer le curseur pour lire le terrain et les rep\xE8res";
        case Id::HelpKeyPadAnnounceFocus:
            return "G\xE2" "chette gauche avec A : annoncer l'objet cibl\xE9";
        case Id::HelpKeyPadViewMode:
            return "G\xE2" "chette droite avec A : mode observation, rester sur place pendant que vous tournez la cam\xE9ra";
        case Id::HelpKeyPadLevelUp:
            return "G\xE2" "chette droite avec Y : ouvrir l'\xE9" "cran de niveau sup\xE9rieur";
        case Id::HelpKeyPadCancelCombat:
            return "G\xE2" "chette gauche avec Y : annuler le combat";
        case Id::HelpKeyPadCopyLastSpoken:
            return "G\xE2" "chette droite avec B : copier le dernier texte lu dans le presse-papiers";
        case Id::HelpKeyPadPazaakReview:
            return "Plateau de Pazaak, bouton de tranche gauche : votre main. Bouton de tranche droit : les deux plateaux. Pression du stick gauche : combien de cartes l'adversaire garde en main";

        case Id::PadQuickMenuOpened:
            return "Menu rapide. Haut et bas pour choisir, A confirme, "
                   "B ferme.";
        case Id::PadQuickMenuClosed:         return "Menu rapide ferm\xE9.";
        case Id::PadQuickMenus:              return "Menus";
        case Id::PadQuickPartyLeader:        return "Chef de groupe";
        case Id::PadQuickSoloMode:           return "Mode solo";
        case Id::PadQuickStealth:            return "Discr\xE9tion";
        case Id::PadQuickSave:               return "Sauvegarde rapide";
        case Id::PadQuickHelp:               return "Aide";

        case Id::HelpMenuOpened:    return "Aide des touches. Haut et bas pour lire, \xC9""chap pour fermer.";
        case Id::HelpMenuClosed:    return "Aide des touches ferm\xE9""e.";
        case Id::FmtHelpRowOf:      return "%s. %d sur %d";
        case Id::FmtHelpGroupHeader: return "Section : %s";

        case Id::HelpContextNothing: return "Aucune touche sp\xE9""ciale pour cet \xE9""cran.";
        case Id::FmtHelpContextLine: return "%s %s.";
        case Id::HelpContextWorld:       return "Dans le monde.";
        case Id::HelpContextMenu:        return "Menu.";
        case Id::HelpContextMap:         return "Carte.";
        case Id::HelpContextActionMenu:  return "Menu d'action.";
        case Id::HelpContextDialog:      return "Conversation.";
        case Id::HelpContextContainer:   return "Conteneur.";
        case Id::HelpContextStore:       return "Boutique.";
        case Id::HelpContextPazaak:            return "Pazaak.";

        case Id::InputBlockedBigPicture:
            return "Le jeu ne peut pas recevoir vos touches, car le mode "
                   "Steam Big Picture est au premier plan.";

        case Id::InputKeyboardLost:
            return "Le jeu a perdu le clavier. Veuillez red\xE9marrer le jeu.";

        // ---- Tastenbelegung (mod keybind configurator) ----
        case Id::KeybindsRootLabel:       return "Raccourcis clavier";
        case Id::KeybindsOpened:          return "Raccourcis clavier ouverts";
        case Id::KeybindCatWorld:         return "Monde et actions";
        case Id::KeybindCatExploration:   return "Exploration et cam\xE9ra";
        case Id::KeybindCatMenus:         return "Menus et saisie";
        case Id::KeybindCatMinigames:     return "Mini-jeux";
        case Id::KeybindCatGeneral:       return "G\xE9n\xE9ral";
        case Id::KeybindResetAll:         return "R\xE9tablir les valeurs par d\xE9""faut";
        case Id::KeybindResetDone:        return "Raccourcis clavier r\xE9initialis\xE9s";
        case Id::FmtKeybindCapturePrompt: return "Appuyez sur la nouvelle touche pour %s. \xC9""chap annule.";
        case Id::FmtKeybindRebound:       return "%s r\xE9""affect\xE9 \xE0 %s";
        case Id::FmtKeybindConflictMod:   return "D\xE9j\xE0 affect\xE9 \xE0 %s. Appuyez sur une autre touche.";
        case Id::KeybindConflictEngine:   return "Utilis\xE9 par le jeu. Appuyez sur une autre touche.";
        case Id::KeybindCaptureCancelled: return "Annul\xE9";
        case Id::FmtKeymapModConflict:    return "Attention : le mod utilise cette touche pour %s";
        // World & actions
        case Id::KbNameInteractTarget:      return "Interagir";
        case Id::KbNameInteractForceRadial: return "Forcer le menu radial";
        case Id::KbNameInteractForceRadialSecondary: return "Forcer le menu radial (secondaire)";
        case Id::KbNameTargetKey1:          return "Touche de cible 1";
        case Id::KbNameTargetKey2:          return "Touche de cible 2";
        case Id::KbNameTargetKey3:          return "Touche de cible 3";
        case Id::KbNamePersonalKey1:        return "Action personnelle 1";
        case Id::KbNamePersonalKey2:        return "Action personnelle 2";
        case Id::KbNamePersonalKey3:        return "Action personnelle 3";
        case Id::KbNamePersonalKey4:        return "Action personnelle 4";
        case Id::KbNamePersonalKey5:        return "Action personnelle 5";
        case Id::KbNameActionBarOpen1:      return "Ouvrir la barre d'action 1";
        case Id::KbNameActionBarOpen2:      return "Ouvrir la barre d'action 2";
        case Id::KbNameActionBarOpen3:      return "Ouvrir la barre d'action 3";
        case Id::KbNameActionBarOpen4:      return "Ouvrir la barre d'action 4";
        case Id::KbNameActionBarOpen5:      return "Ouvrir la barre d'action 5";
        case Id::KbNameTargetActionOpen1:   return "Ouvrir l'action de cible 1";
        case Id::KbNameTargetActionOpen2:   return "Ouvrir l'action de cible 2";
        case Id::KbNameTargetActionOpen3:   return "Ouvrir l'action de cible 3";
        case Id::KbNameLevelUpOpen:         return "Mont\xE9""e de niveau";
        case Id::KbNameExamineOpen:         return "Examiner";
        case Id::KbNameCombatQueueOpen:     return "File d'actions";
        case Id::KbNameSelfStatusAnnounce:  return "\xC9tat personnel";
        // Exploration & camera
        case Id::KbNameCycleItemPrev:       return "Objet pr\xE9""c\xE9""dent";
        case Id::KbNameCycleCategoryPrev:   return "Cat\xE9gorie pr\xE9""c\xE9""dente";
        case Id::KbNameCycleItemNext:       return "Objet suivant";
        case Id::KbNameCycleCategoryNext:   return "Cat\xE9gorie suivante";
        case Id::KbNameCycleItemFirst:      return "Premier objet";
        case Id::KbNameCycleItemLast:       return "Dernier objet";
        case Id::KbNameAnnounceFocus:       return "Annoncer la cible";
        case Id::KbNamePathfindFocus:       return "Aller \xE0 la cible";
        case Id::KbNamePathfindFocusForce:  return "Aller \xE0 la cible, forcer";
        case Id::KbNameBeaconFocus:         return "Balise vers la cible";
        case Id::KbNameAnnounceDegrees:     return "Orientation en degr\xE9s";
        case Id::KbNamePartyLeaderAnnounce: return "Annoncer le chef de groupe";
        case Id::KbNameCameraOrient:        return "Orienter la cam\xE9ra";
        case Id::KbNameSaveMarkerAtCursor:  return "Poser un marqueur";
        case Id::KbNameViewModeToggle:      return "Mode observation";
        // Menus & input
        case Id::KbNameNavUp:               return "Menu haut";
        case Id::KbNameNavDown:             return "Menu bas";
        case Id::KbNameNavLeft:             return "Menu gauche";
        case Id::KbNameNavRight:            return "Menu droite";
        case Id::KbNameNavHome:             return "Au d\xE9""but";
        case Id::KbNameNavEnd:              return "\xC0 la fin";
        case Id::KbNameSubmenuEsc:          return "Fermer le menu";
        case Id::KbNameQueueClearAll:       return "Vider la file";
        case Id::KbNameContainerGiveMode:   return "Conteneur mode donner";
        case Id::KbNameStoreModeToggle:     return "Boutique acheter ou vendre";
        case Id::KbNameCraftViewToggle:     return "\xC9tabli cr\xE9""er ou d\xE9""composer";
        case Id::KbNameEditboxReReadUp:     return "Relire le champ vers le haut";
        case Id::KbNameEditboxReReadDown:   return "Relire le champ vers le bas";
        case Id::KbNameEditboxSubmit:       return "Valider la saisie";
        case Id::KbNameEditboxCancel:       return "Annuler la saisie";
        // Minigames
        case Id::KbNamePazaakStand:         return "Pazaak : Rester";
        case Id::KbNamePazaakEndTurn:       return "Pazaak : Fin du tour";
        case Id::KbNamePazaakReviewHand:    return "Pazaak : Main";
        case Id::KbNamePazaakReviewTable:   return "Pazaak : Table";
        case Id::KbNamePazaakNextCard:      return "Pazaak : Carte suivante";
        case Id::KbNamePazaakPrevCard:      return "Pazaak : Carte pr\xE9""c\xE9""dente";
        case Id::KbNamePazaakPlay:          return "Pazaak : Jouer la carte";
        case Id::KbNamePazaakOptLeft:       return "Pazaak : Option gauche";
        case Id::KbNamePazaakOptRight:      return "Pazaak : Option droite";
        case Id::KbNamePazaakCancel:        return "Pazaak : Annuler";
        case Id::KbNamePazaakOppHand:       return "Pazaak : Main de l'adversaire";
        case Id::KbNameTurretCyclePrev:     return "Tourelle : Cible pr\xE9""c\xE9""dente";
        case Id::KbNameTurretCycleNext:     return "Tourelle : Cible suivante";
        case Id::KbNameSwoopShift:           "Swoop : Monter une vitesse";
        // General
        case Id::KbNameHelpMenuOpen:        return "Aide des touches";
        case Id::KbNameHelpContext:         return "Aide contextuelle";
        case Id::KbNameCheckForUpdate:      return "Rechercher une mise \xE0 jour";
        case Id::KbNameDialogRepeatLine:    return "R\xE9p\xE9ter la ligne de dialogue";
        case Id::KbNameCopyLastSpoken:      return "Copier le dernier texte lu";

        // ---- Presse-papiers ----
        case Id::ClipboardCopied:        return "Copi\xE9 dans le presse-papiers";
        case Id::ClipboardNothingToCopy: return "Rien \xE0 copier";
        case Id::ClipboardFailed:        return "Presse-papiers indisponible";

        // ---- Endar Spire tutorial keyboard hints (Surface 1: popups) ----
        case Id::TutHintCombatFeat:
            return "Les dons de combat sont dans le menu d'action. Ouvrez les Attaques avec Maj et 1 et activez avec Entr\xE9""e.";
        case Id::TutHintGrenade:
            return "Lancez des grenades depuis le menu d'action. Ouvrez les Explosifs avec Maj et 7 et activez avec Entr\xE9""e.";
        case Id::TutHintMine:
            return "Posez des mines depuis le menu d'action. Ouvrez les Explosifs avec Maj et 7 et activez avec Entr\xE9""e.";
        case Id::TutHintFriendlyPower:
            return "Les pouvoirs de la Force b\xE9n\xE9""fiques sont dans le menu d'action. Ouvrez les pouvoirs sur vous avec Maj et 4 et activez avec Entr\xE9""e.";
        case Id::TutHintHostilePower:
            return "Les pouvoirs de la Force hostiles sont dans le menu d'action. Ouvrez les pouvoirs sur la cible avec Maj et 2 et activez avec Entr\xE9""e.";
        case Id::TutHintActionMenuScroll:
            return "Dans le menu d'action, d\xE9placez-vous entre les entr\xE9""es avec haut et bas, et entre les cat\xE9gories avec gauche et droite.";
        case Id::TutHintOpenScreens:
            return "Ouvrez les \xE9""crans de jeu directement : M carte, J messages, L qu\xEAtes, K aptitudes, P personnage, I inventaire, U \xE9quipement, O options. \xC9""chap revient.";
        case Id::TutHintCycleTargets:
            return "Choisissez les cibles avec Q et E. Faites d\xE9""filer les objets avec virgule et point ; la touche moins annonce l'objet s\xE9lectionn\xE9. En combat, vous ne pouvez cibler que des ennemis.";
        case Id::TutHintEquipSlot:
            return "Sur l'\xE9""cran d'\xE9quipement, choisissez un emplacement avec les fl\xE8""ches et Entr\xE9""e, puis choisissez un objet dans la liste. Lisez la description d'un objet avec Maj et une fl\xE8""che.";
        case Id::TutHintEnemyNear:
            return "Un ennemi est proche. Faites d\xE9""filer les cibles avec Q et E et attaquez avec Entr\xE9""e.";
        case Id::TutHintBash:
            return "Pour forcer, s\xE9lectionnez la porte ou le conteneur, ouvrez le menu d'action avec Maj et Entr\xE9""e, et choisissez Forcer.";
        case Id::TutHintAttack:
            return "S\xE9lectionnez un ennemi avec Q et E et attaquez avec Entr\xE9""e. Une seule pression suffit ; votre personnage attaque ensuite automatiquement \xE0 chaque tour jusqu'\xE0 ce que vous choisissiez une autre cible.";
        case Id::TutHintAttackAuto:
            return "Rappel : une seule pression sur Entr\xE9""e attaque automatiquement \xE0 chaque tour. Seule une nouvelle cible change cela.";
        case Id::TutHintMovement:
            return "Appuyez sur F1 \xE0 tout moment pour le menu d'aide, une liste de toutes les touches, et Ctrl et F1 pour les touches de l'\xE9""cran actuel. Pour vous d\xE9placer, appuyez sur W et S pour avancer et reculer, A et D pour faire un pas de c\xF4t\xE9 \xE0 gauche et \xE0 droite, et Z et C pour tourner \xE0 gauche et \xE0 droite. Z et C font pivoter la cam\xE9ra et, avec elle, votre direction de marche, si bien que les deux pointent toujours dans la m\xEAme direction. Des sons vous avertissent quand vous approchez d'un mur, et pendant que vous vous d\xE9placez, la forme de la pi\xE8""ce autour de vous est d\xE9""crite. Appuyez sur Alt droit pour r\xE9p\xE9ter votre orientation et la pi\xE8""ce actuelle.";
        case Id::TutHintMapScreen:
            return "La carte montre chaque zone explor\xE9""e d'un niveau. Les zones importantes sont indiqu\xE9""es, et certaines zones, comme les villes, sont cartographi\xE9""es avant que vous les exploriez. D\xE9placez-vous sur la carte avec W, A, S et D. Changez d'indice de carte avec virgule et point. Appuyez sur Maj et N pour poser votre propre indice de carte \xE0 votre position. Appuyez sur Ctrl et moins pour lancer un signal sonore qui vous guide vers l'indice de carte, ou Maj et moins pour marcher automatiquement vers l'indice s\xE9lectionn\xE9.";
        case Id::TutHintJournal:
            return "L'\xE9""cran des qu\xEAtes montre votre progression et des indices sur la suite. Ouvrez-le avec L. Choisissez une entr\xE9""e avec les fl\xE8""ches et lisez ses d\xE9tails avec Maj et une fl\xE8""che.";
        case Id::TutHintInventory:
            return "L'inventaire liste les objets de votre groupe, partag\xE9s entre tous les membres. Ouvrez-le avec I. Parcourez les objets avec les fl\xE8""ches et lisez la description d'un objet avec Maj et une fl\xE8""che.";
        case Id::TutHintMessages:
            return "L'\xE9""cran des messages affiche des valeurs comme les points d'exp\xE9rience, les jets d'attaque et de d\xE9g\xE2ts, et les changements de votre personnage. Ouvrez-le avec J.";
        case Id::TutHintPartyDies:
            return "Un membre du groupe est tomb\xE9. Les membres mis \xE0 terre au combat sont r\xE9""anim\xE9s apr\xE8s le combat, mais si tous les membres meurent, la partie est termin\xE9""e. Appuyez sur Tab pour contr\xF4ler un autre membre.";

        // ---- Endar Spire tutorial keyboard hints (Surface 2: Trask / pop) ----
        case Id::TutTraskGettingStarted:
            return "Vous avez maintenant le contr\xF4le. Les bases : appuyez sur F1 \xE0 tout moment pour la liste compl\xE8te des touches, ou Ctrl et F1 pour les touches de l'\xE9""cran actuel. W et S avancent et reculent, A et D vous font faire un pas de c\xF4t\xE9 \xE0 gauche et \xE0 droite, et Z et C vous font tourner \xE0 gauche et \xE0 droite. Z et C font pivoter la cam\xE9ra et, avec elle, votre direction de marche, si bien que les deux pointent toujours dans la m\xEAme direction. Q et E basculent entre cibles et objets, et Entr\xE9""e interagit avec l'objet s\xE9lectionn\xE9.";
        case Id::TutTraskEquipOpen:
            return "Sur l'\xE9""cran d'\xE9quipement, vous choisissez armure et armes. Ouvrez-le avec U.";
        case Id::TutTraskEquipBrowse:
            return "Parcourez les emplacements d'\xE9quipement avec les fl\xE8""ches ; chaque emplacement contient un objet que vous pouvez assigner. Lisez la description de l'objet avec Maj et une fl\xE8""che.";
        case Id::TutTraskEquipWeapon:
            return "S\xE9lectionnez l'emplacement de l'arme et appuyez sur Entr\xE9""e, puis choisissez une arme dans la liste.";
        case Id::TutTraskCamera:
        case Id::TutTraskFootlocker:
            return "Vous changez la cam\xE9ra, et avec elle votre direction de marche, avec Z et C. N vous tourne de 90 degr\xE9s. Q et E font d\xE9""filer tous les objets visibles. Virgule et point parcourent une liste de tous les objets que vous avez d\xE9j\xE0 vus. Maj avec virgule ou point change de cat\xE9gorie d'objets. Appuyez sur Entr\xE9""e pour marcher vers un objet s\xE9lectionn\xE9 et l'activer.";
        case Id::TutTraskPickItem:
            return "Choisissez un emplacement avec les fl\xE8""ches et Entr\xE9""e, parcourez la liste et confirmez l'objet avec Entr\xE9""e.";
        case Id::TutTraskLeader:
            return "Vous contr\xF4lez le chef de groupe. Appuyez sur Tab pour changer le membre que vous contr\xF4lez.";
        case Id::TutTraskMakeLeader:
            return "Appuyez sur Tab pour faire de moi le chef, puis ouvrez la porte.";
        case Id::TutTraskMenus:
            return "Ouvrez les \xE9""crans de jeu directement avec les touches : L qu\xEAtes, M carte, et plus encore.";
        case Id::TutTraskTabs:
            return "Changez d'onglet avec Q et E.";
        case Id::TutTraskActionMenu:
            return "Ouvrez le menu d'action avec Maj et Entr\xE9""e pour votre cible. Un chiffre de 1 \xE0 7 d\xE9""clenche la derni\xE8""re action utilis\xE9""e de cette cat\xE9gorie ; Maj et un chiffre de 1 \xE0 7 ouvre la cat\xE9gorie pour choisir.";
        case Id::TutTraskMedkit:
            return "Utilisez des kits m\xE9""dicaux depuis le menu d'action. Ouvrez M\xE9""dical avec Maj et 5 et activez avec Entr\xE9""e.";
        case Id::TutTraskOpenDoor:
            return "S\xE9lectionnez la porte avec virgule et point et ouvrez-la avec Entr\xE9""e.";
        case Id::TutTraskActivateEntry:
            return "Dans le menu d'action, choisissez une entr\xE9""e avec haut et bas et activez-la avec Entr\xE9""e.";
        case Id::TutTraskTargetMenu:
            return "S\xE9lectionnez une cible avec Q et E, ou des objets avec virgule et point. Appuyez sur Entr\xE9""e pour l'action par d\xE9""faut, ou Maj et Entr\xE9""e pour ouvrir le menu d'action.";
        case Id::TutTraskConfirmEntry:
            return "Mettez l'entr\xE9""e en surbrillance avec haut et bas et confirmez avec Entr\xE9""e.";
        case Id::TutTraskPaused:
            return "M\xEAme en pause, choisissez les cibles avec Q et E et changez le membre contr\xF4l\xE9 avec Tab.";
        case Id::TutTraskWalkTo:
            return "S\xE9lectionnez le casier avec Q et E et ouvrez-le avec Entr\xE9""e.";
        case Id::TutTraskSecurity:
            return "S\xE9lectionnez la porte, ouvrez le menu d'action avec Maj et Entr\xE9""e, et choisissez S\xE9""curit\xE9.";
        case Id::TutTraskHealWounded:
            return "Passez au personnage bless\xE9 avec Tab, ouvrez M\xE9""dical avec Maj et 5, et choisissez le kit m\xE9""dical.";
        case Id::TutLevelUp:
            return "Pour monter de niveau, appuyez sur Maj et L et suivez les instructions.";
        case Id::TutStealthMode:
            return "Appuyez sur G pour activer ou d\xE9sactiver le mode furtif. Vous devez avoir un g\xE9n\xE9rateur de champ furtif \xE9quip\xE9 pour l'utiliser.";

        case Id::FloorPuzzleIntro:
            return "\xC9nigme des dalles : neuf dalles en grille de trois sur trois, "
                   "dalle de r\xE9initialisation au sud.";
        case Id::FmtFloorReadHint:
            return "Appuie sur %s pour lire l'\xE9tat des dalles.";
        case Id::FloorPuzzleStoryHint:
            return "Tu devrais avoir trouv\xE9 un indice de l'histoire qui t'aidera \xE0 r\xE9soudre l'\xE9nigme.";
        case Id::FmtFloorSoloHint:
            return "Conseil : activez le mode solo, touche %s, sinon les compagnons d\xE9""clenchent aussi les dalles.";
        case Id::FloorPartyToggled:  return "Un compagnon a modifi\xE9 des dalles";
        case Id::FmtPlateName:       return "Dalle %s";
        case Id::PlateCenterWord:    return "Centre";
        case Id::PlateResetName:     return "Dalle de r\xE9initialisation";
        case Id::FmtPlateEntered:    return "Sur %s";
        case Id::FmtPlateLit:        return "%s s'allume";
        case Id::FmtPlateDark:       return "%s s'\xE9teint";
        case Id::FmtPlateLitCount:   return "%d sur 9 allum\xE9""es";
        case Id::PlatesAllDark:      return "Toutes les dalles \xE9teintes";
        case Id::FloorPuzzleSolved:
            return "\xC9nigme r\xE9solue. La porte massive s'ouvre.";

        case Id::DialogRepeatLineHint:
            return "Appuyez sur R pour r\xE9p\xE9ter la derni\xE8re ligne de dialogue.";
        case Id::CameraFeed:            return "Vue de la cam\xE9ra.";

        // ---- KOTOR 2 tutorial hints (tutorial_hints.cpp K2 tables). The
        //      shared rows 0-42 reuse the TutHint* wordings above; these
        //      cover KOTOR 2's own popups and the Ebon Hawk prologue lines.
        case Id::TutK2SwoopRace:
            return "Passez la vitesse sup\xE9rieure avec W au signal sonore et sautez les barres basses avec la barre d'espace. Dirigez avec A et D pour \xE9viter les obstacles et toucher les acc\xE9l\xE9rateurs.";
        case Id::TutK2ActionQueue:
            return "Appuyez sur Entr\xE9""e pour ajouter l'action par d\xE9""faut \xE0 la file de commandes ; les entr\xE9""es du menu d'action s'y ajoutent de la m\xEAme fa\xE7on. Plusieurs actions s'ex\xE9""cutent dans l'ordre d'ajout.";
        case Id::TutK2ActionRemove:
            return "Les actions ne peuvent \xEAtre retir\xE9""es de la file de commandes que pendant un combat ; la derni\xE8re action ajout\xE9""e est retir\xE9""e en premier.";
        case Id::TutK2PlasmaTorch:
            return "Les portes endommag\xE9""es se forcent au mieux avec un outil comme la torche \xE0 plasma. Ouvrez l'\xE9""cran d'\xE9quipement avec U et \xE9quipez-la. S\xE9lectionnez ensuite la porte, ouvrez le menu d'action avec Maj et Entr\xE9""e, et choisissez Forcer.";
        case Id::TutK2FirstPerson:
            return "Verr Maj bascule la vue \xE0 la premi\xE8re personne. Elle n'est pas n\xE9""cessaire avec un lecteur d'\xE9""cran.";
        case Id::TutK2SpaceSuit:
            return "S\xE9lectionnez la combinaison spatiale avec virgule et point et appuyez sur Entr\xE9""e pour l'enfiler ou la retirer.";
        case Id::TutK2CommRoom:
            return "Choisissez une console avec virgule et point et activez-la avec Entr\xE9""e. Pour utiliser les ordinateurs, il vous faut la sonde de programmation du conteneur du cockpit.";
        case Id::TutK2Mines:
            return "Vous d\xE9tectez mines et pi\xE8ges gr\xE2""ce \xE0 votre Vigilance en restant immobile. S\xE9lectionnez une mine d\xE9tect\xE9""e avec virgule et point, ouvrez le menu d'action avec Maj et Entr\xE9""e, et choisissez D\xE9samorcer ou R\xE9""cup\xE9rer. D\xE9samorcez ou r\xE9""cup\xE9rez maintenant trois fragments de torpille \xE0 protons autour de la roquette.";
        case Id::TutK2Vibrocutter:
            return "Le vibrocoupeur est une arme de m\xEAl\xE9""e, sup\xE9rieure aux blasters au corps \xE0 corps. Ouvrez l'\xE9""cran d'\xE9quipement avec U et placez-le dans un emplacement d'arme. Les emplacements secondaires forment une seconde configuration.";
        case Id::TutK2DialogueChoice:
            return "Choisissez vos r\xE9ponses avec les fl\xE8""ches haut et bas et confirmez avec Entr\xE9""e. Appuyez sur R pour r\xE9p\xE9ter la derni\xE8re ligne de dialogue.";
        case Id::TutK2Pause:
            return "La barre d'espace met le jeu en pause et le reprend. En pause, vous planifiez vos actions calmement.";
        case Id::TutK2EquipWeapon:
            return "Ouvrez l'\xE9""cran d'\xE9quipement avec U, choisissez un emplacement d'arme avec les fl\xE8""ches et Entr\xE9""e, puis choisissez l'arme dans la liste.";
        case Id::TutK2WeaponConfig:
            return "L'\xE9""cran d'\xE9quipement a deux configurations d'armes, active et secondaire, par exemple pour la m\xEAl\xE9""e et le tir. Remplissez les emplacements avec les fl\xE8""ches et Entr\xE9""e.";
        case Id::TutK2TakeItem:
            return "S\xE9lectionnez le conteneur avec virgule et point et ouvrez-le avec Entr\xE9""e. Prenez la sonde de programmation et utilisez-la sur le terminal informatique.";
        case Id::TutK2GalaxyMap:
            return "Mettez le cap sur Peragus : sur la carte de la galaxie, choisissez Peragus avec les fl\xE8""ches gauche et droite ; Entr\xE9""e lance le voyage.";
        case Id::TutK2SoloMode:
            return "Cette porte ne se franchit qu'en mode solo : appuyez sur V, et seul le personnage contr\xF4l\xE9 se d\xE9place. Amenez 3C-FD seul \xE0 l'ordinateur de s\xE9""curit\xE9 pour qu'il ouvre la porte.";
        case Id::TutK2RepairKit:
            return "Les kits de r\xE9paration soignent les dro\xEF""des. Utilisez-les depuis le menu d'action : ouvrez M\xE9""dical avec Maj et 5 et activez avec Entr\xE9""e.";
        case Id::TutK2SecuritySpike:
            return "Cette serrure d\xE9passe vos comp\xE9tences ; utilisez une sonde de s\xE9""curit\xE9. S\xE9lectionnez le conteneur, ouvrez le menu d'action avec Maj et Entr\xE9""e, et choisissez S\xE9""curit\xE9.";

        // ---- KOTOR 2 J/L swap: KOTOR 2's default keymap puts Quests on J
        //      and Messages on L, the mirror image of KOTOR 1 (its keymap.2da
        //      rows 13/14). Same texts as the TutHint* twins above with the two
        //      letters exchanged; the pad wordings name no letter keys, so
        //      kTwins points these at the existing *Pad ids.
        case Id::TutK2OpenScreens:
            return "Ouvrez les \xE9""crans de jeu directement : M carte, L messages, J qu\xEAtes, K aptitudes, P personnage, I inventaire, U \xE9quipement, O options. \xC9""chap revient.";
        case Id::TutK2Messages:
            return "L'\xE9""cran des messages affiche des valeurs comme les points d'exp\xE9rience, les jets d'attaque et de d\xE9g\xE2ts, et les changements de votre personnage. Ouvrez-le avec L.";
        case Id::TutK2Journal:
            return "L'\xE9""cran des qu\xEAtes montre votre progression et des indices sur la suite. Ouvrez-le avec J. Choisissez une entr\xE9""e avec les fl\xE8""ches et lisez ses d\xE9tails avec Maj et une fl\xE8""che.";

        // ---- Controller wording for the tutorial hints above. Selected by
        //      binding_hints::Text() when a gamepad is present; see strings.h.

        case Id::TutHintCombatFeatPad:
            return "Les dons de combat sont dans le menu d'actions. Tirez la g\xE2" "chette gauche pour l'ouvrir, allez sur Attaques avec la croix gauche et droite, puis appuyez sur A.";
        case Id::TutHintGrenadePad:
            return "Lancez les grenades depuis le menu d'actions. Tirez la g\xE2" "chette gauche, allez sur Explosifs avec la croix gauche et droite, puis appuyez sur A.";
        case Id::TutHintMinePad:
            return "Posez les mines depuis le menu d'actions. Tirez la g\xE2" "chette gauche, allez sur Explosifs avec la croix gauche et droite, puis appuyez sur A.";
        case Id::TutHintFriendlyPowerPad:
            return "Les pouvoirs de Force b\xE9n\xE9" "fiques sont dans le menu d'actions. Tirez la g\xE2" "chette gauche, allez sur vos propres pouvoirs avec la croix gauche et droite, puis appuyez sur A.";
        case Id::TutHintHostilePowerPad:
            return "Les pouvoirs de Force offensifs sont dans le menu d'actions. Tirez la g\xE2" "chette gauche, allez sur les pouvoirs de cible avec la croix gauche et droite, puis appuyez sur A.";
        case Id::TutHintActionMenuScrollPad:
            return "Dans le menu d'actions, la croix haut et bas change d'entr\xE9" "e, gauche et droite change de cat\xE9gorie.";
        case Id::TutHintOpenScreensPad:
            return "Appuyez sur Y pour le menu rapide et choisissez Menus pour ouvrir les \xE9" "crans de jeu. Les boutons de tranche gauche et droit passent ensuite entre carte, messages, qu\xEAtes, aptitudes, personnage, inventaire et \xE9quipement. B revient en arri\xE8re.";
        case Id::TutHintCycleTargetsPad:
            return "Choisissez vos cibles avec les boutons de tranche gauche et droit. La croix gauche et droite parcourt les objets d\xE9j\xE0 d\xE9" "couverts. En combat, vous ne pouvez viser que des ennemis.";
        case Id::TutHintEquipSlotPad:
            return "Sur l'\xE9" "cran d'\xE9quipement, choisissez un emplacement avec la croix et A, puis prenez un objet dans la liste. Maintenez Y et appuyez sur la croix haut ou bas pour lire la description d'un objet.";
        case Id::TutHintEnemyNearPad:
            return "Un ennemi est proche. Changez de cible avec les boutons de tranche gauche et droit et attaquez avec A.";
        case Id::TutHintBashPad:
            return "Pour forcer, ciblez la porte ou le conteneur, tirez la g\xE2" "chette gauche pour ouvrir le menu d'actions et choisissez Forcer.";
        case Id::TutHintAttackPad:
            return "Ciblez un ennemi avec les boutons de tranche gauche et droit et attaquez avec A. Une pression suffit : votre personnage attaque ensuite automatiquement \xE0 chaque round jusqu'\xE0 ce que vous changiez de cible.";
        case Id::TutHintAttackAutoPad:
            return "Rappelez-vous : une pression sur A attaque automatiquement \xE0 chaque round. Seule une nouvelle cible change cela.";
        case Id::TutHintMovementPad:
            return "Tirez les deux g\xE2" "chettes ensemble \xE0 tout moment pour les touches de l'\xE9" "cran en cours, et appuyez sur Y pour le menu rapide, entr\xE9" "e Aide, pour la liste compl\xE8te. Pour vous d\xE9placer, poussez le stick gauche dans n'importe quelle direction. Poussez le stick droit \xE0 gauche ou \xE0 droite pour tourner la cam\xE9ra et, avec elle, votre direction de marche, afin que les deux pointent toujours dans le m\xEAme sens. Des sons vous avertissent quand vous approchez d'un mur, et \xE0 mesure que vous avancez la forme de la pi\xE8" "ce autour de vous est d\xE9" "crite. Tirez la g\xE2" "chette droite pour r\xE9p\xE9ter votre orientation et la pi\xE8" "ce actuelle.";
        case Id::TutHintMapScreenPad:
            return "La carte montre chaque zone explor\xE9" "e d'un niveau. Les endroits importants sont marqu\xE9s, et certaines zones, comme les villes, sont cartographi\xE9" "es avant m\xEAme d'\xEAtre explor\xE9" "es. D\xE9placez-vous sur la carte avec le stick gauche. Changez de rep\xE8re avec la croix gauche et droite. Tirez la g\xE2" "chette gauche avec le bouton de tranche gauche pour lancer une balise sonore qui vous y guide, ou la g\xE2" "chette droite avec le bouton de tranche droit pour vous y rendre automatiquement.";
        case Id::TutHintJournalPad:
            return "L'\xE9" "cran des qu\xEAtes montre votre progression et ce qu'il reste \xE0 faire. Ouvrez-le depuis le menu rapide sur Y, entr\xE9" "e Menus, puis rejoignez-le avec les boutons de tranche. Choisissez une entr\xE9" "e avec la croix et lisez ses d\xE9tails en maintenant Y et en appuyant sur la croix haut ou bas.";
        case Id::TutHintMessagesPad:
            return "L'\xE9" "cran des messages affiche des valeurs comme les points d'exp\xE9rience, les jets d'attaque et de d\xE9g\xE2ts, et les changements de votre personnage. Ouvrez-le depuis le menu rapide sur Y, entr\xE9" "e Menus, puis rejoignez-le avec les boutons de tranche.";
        case Id::TutHintPartyDiesPad:
            return "Un membre du groupe est tomb\xE9. Les membres mis \xE0 terre en combat se rel\xE8vent apr\xE8s la bataille, mais si tous meurent, la partie est finie. Appuyez sur X pour contr\xF4ler un autre membre.";
        case Id::TutHintInventoryPad:
            return "L'inventaire liste les objets de votre groupe, partag\xE9s par tous les membres. Ouvrez-le depuis le menu rapide sur Y, entr\xE9" "e Menus, puis rejoignez-le avec les boutons de tranche. Parcourez les objets avec la croix et lisez une description en maintenant Y et en appuyant sur la croix haut ou bas.";
        case Id::TutTraskGettingStartedPad:
            return "Vous avez maintenant le contr\xF4le. L'essentiel : tirez les deux g\xE2" "chettes ensemble pour les touches de l'\xE9" "cran en cours, ou appuyez sur Y pour le menu rapide, entr\xE9" "e Aide, pour la liste compl\xE8te. Poussez le stick gauche pour marcher, et le stick droit \xE0 gauche ou \xE0 droite pour tourner la cam\xE9ra et, avec elle, votre direction de marche, afin que les deux pointent toujours dans le m\xEAme sens. Les boutons de tranche gauche et droit changent de cible, la croix gauche et droite parcourt les objets, et A interagit avec l'objet cibl\xE9.";
        case Id::TutTraskEquipOpenPad:
            return "L'\xE9" "cran d'\xE9quipement sert \xE0 choisir armures et armes. Ouvrez-le depuis le menu rapide sur Y, entr\xE9" "e Menus, puis rejoignez-le avec les boutons de tranche.";
        case Id::TutTraskEquipBrowsePad:
            return "Parcourez les emplacements d'\xE9quipement avec la croix ; chacun re\xE7oit un objet. Maintenez Y et appuyez sur la croix haut ou bas pour lire la description de l'objet.";
        case Id::TutTraskEquipWeaponPad:
            return "S\xE9lectionnez l'emplacement d'arme et appuyez sur A, puis choisissez une arme dans la liste.";
        case Id::TutTraskCameraPad:
        case Id::TutTraskFootlockerPad:
            return "Vous changez la cam\xE9ra, et avec elle votre direction de marche, avec le stick droit. Une pression du stick droit vous tourne vers le point cardinal suivant. Les boutons de tranche gauche et droit parcourent tous les objets visibles. La croix gauche et droite navigue dans la liste de tous les objets d\xE9j\xE0 vus, haut et bas change de cat\xE9gorie. Appuyez sur A pour marcher jusqu'\xE0 un objet s\xE9lectionn\xE9 et l'activer.";
        case Id::TutTraskPickItemPad:
            return "Choisissez un emplacement avec la croix et A, parcourez la liste, et confirmez l'objet avec A.";
        case Id::TutTraskLeaderPad:
            return "Vous contr\xF4lez le chef de groupe. Appuyez sur X pour changer de membre contr\xF4l\xE9.";
        case Id::TutTraskMakeLeaderPad:
            return "Appuyez sur X pour faire de moi le chef, puis ouvrez la porte.";
        case Id::TutTraskMenusPad:
            return "Ouvrez les \xE9" "crans de jeu depuis le menu rapide sur Y, entr\xE9" "e Menus. Les boutons de tranche passent ensuite de l'un \xE0 l'autre.";
        case Id::TutTraskTabsPad:
            return "Changez d'onglet avec les boutons de tranche gauche et droit.";
        case Id::TutTraskActionMenuPad:
            return "Tirez la g\xE2" "chette gauche pour ouvrir le menu d'actions sur votre cible. La croix gauche et droite change de cat\xE9gorie, haut et bas change d'entr\xE9" "e, et A lance l'entr\xE9" "e sur laquelle vous \xEAtes.";
        case Id::TutTraskMedkitPad:
            return "Utilisez les kits de soins depuis le menu d'actions. Tirez la g\xE2" "chette gauche, allez sur M\xE9" "dical avec la croix gauche et droite, puis appuyez sur A.";
        case Id::TutTraskOpenDoorPad:
            return "Ciblez la porte avec la croix gauche et droite et ouvrez-la avec A.";
        case Id::TutTraskActivateEntryPad:
            return "Dans le menu d'actions, choisissez une entr\xE9" "e avec la croix haut et bas et activez-la avec A.";
        case Id::TutTraskTargetMenuPad:
            return "Ciblez un ennemi avec les boutons de tranche gauche et droit, ou des objets avec la croix gauche et droite. Appuyez sur A pour l'action par d\xE9" "faut, ou tirez la g\xE2" "chette gauche pour ouvrir le menu d'actions.";
        case Id::TutTraskConfirmEntryPad:
            return "Mettez l'entr\xE9" "e en surbrillance avec la croix haut et bas et confirmez avec A.";
        case Id::TutTraskPausedPad:
            return "M\xEAme en pause, choisissez vos cibles avec les boutons de tranche gauche et droit et changez de membre contr\xF4l\xE9 avec X.";
        case Id::TutTraskWalkToPad:
            return "Ciblez le coffre avec les boutons de tranche gauche et droit et ouvrez-le avec A.";
        case Id::TutTraskSecurityPad:
            return "Ciblez la porte, tirez la g\xE2" "chette gauche pour ouvrir le menu d'actions, et choisissez S\xE9" "curit\xE9.";
        case Id::TutTraskHealWoundedPad:
            return "Passez au personnage bless\xE9 avec X, tirez la g\xE2" "chette gauche et allez sur M\xE9" "dical, puis choisissez le kit de soins.";
        case Id::TutLevelUpPad:
            return "Pour monter de niveau, tirez la g\xE2" "chette gauche et appuyez sur Y, puis suivez les indications.";
        case Id::TutStealthModePad:
            return "Appuyez sur Y pour le menu rapide et choisissez Camouflage pour activer ou d\xE9sactiver le mode furtif. Vous devez avoir un g\xE9n\xE9rateur de champ de camouflage \xE9quip\xE9.";
        case Id::DialogRepeatLineHintPad:
            return "Tirez les deux g\xE2" "chettes ensemble pour r\xE9p\xE9ter la derni\xE8re ligne de dialogue.";

        // ---- KOTOR 2 controller twins (deliberate omissions: see strings.h).
        case Id::TutK2SwoopRacePad:
            return "Appuyez sur B pour acc\xE9l\xE9rer et passer la vitesse au signal sonore, et sur A pour sauter les barres basses. \xC9vitez les obstacles et touchez les acc\xE9l\xE9rateurs avec le stick gauche.";
        case Id::TutK2ActionQueuePad:
            return "Appuyez sur A pour ajouter l'action par d\xE9""faut \xE0 la file de commandes ; les entr\xE9""es du menu d'actions s'y ajoutent de la m\xEAme fa\xE7on. Plusieurs actions s'ex\xE9""cutent dans l'ordre d'ajout.";
        case Id::TutK2PlasmaTorchPad:
            return "Les portes endommag\xE9""es se forcent avec un outil comme la torche \xE0 plasma. Ouvrez l'\xE9""cran d'\xE9quipement depuis le menu rapide sur Y, entr\xE9""e Menus, rejoignez-le avec les boutons de tranche et \xE9quipez-la. Ciblez ensuite la porte, tirez la g\xE2""chette gauche et choisissez Forcer.";
        case Id::TutK2SpaceSuitPad:
            return "Ciblez la combinaison spatiale avec la croix gauche et droite et appuyez sur A pour l'enfiler ou la retirer.";
        case Id::TutK2CommRoomPad:
            return "Ciblez une console avec la croix gauche et droite et activez-la avec A. Pour utiliser les ordinateurs, il vous faut la sonde de programmation du conteneur du cockpit.";
        case Id::TutK2MinesPad:
            return "Vous d\xE9tectez mines et pi\xE8ges gr\xE2""ce \xE0 votre Vigilance en restant immobile. Ciblez une mine d\xE9tect\xE9""e avec la croix gauche et droite, tirez la g\xE2""chette gauche pour le menu d'actions, et choisissez D\xE9samorcer ou R\xE9""cup\xE9rer. D\xE9samorcez ou r\xE9""cup\xE9rez maintenant trois fragments de torpille \xE0 protons autour de la roquette.";
        case Id::TutK2VibrocutterPad:
            return "Le vibrocoupeur est une arme de m\xEAl\xE9""e, sup\xE9rieure aux blasters au corps \xE0 corps. Ouvrez l'\xE9""cran d'\xE9quipement depuis le menu rapide sur Y, entr\xE9""e Menus, et placez-le dans un emplacement d'arme. Les emplacements secondaires forment une seconde configuration.";
        case Id::TutK2DialogueChoicePad:
            return "Choisissez vos r\xE9ponses avec la croix haut et bas et confirmez avec A. Tirez les deux g\xE2""chettes ensemble pour r\xE9p\xE9ter la derni\xE8re ligne de dialogue.";
        case Id::TutK2EquipWeaponPad:
            return "Ouvrez l'\xE9""cran d'\xE9quipement depuis le menu rapide sur Y, entr\xE9""e Menus, et rejoignez-le avec les boutons de tranche. Choisissez un emplacement d'arme avec la croix et A, puis choisissez l'arme dans la liste.";
        case Id::TutK2WeaponConfigPad:
            return "L'\xE9""cran d'\xE9quipement a deux configurations d'armes, active et secondaire, par exemple pour la m\xEAl\xE9""e et le tir. Remplissez les emplacements avec la croix et A.";
        case Id::TutK2TakeItemPad:
            return "Ciblez le conteneur avec la croix gauche et droite et ouvrez-le avec A. Prenez la sonde de programmation et utilisez-la sur le terminal informatique.";
        case Id::TutK2GalaxyMapPad:
            return "Mettez le cap sur Peragus : sur la carte de la galaxie, choisissez Peragus avec la croix gauche et droite ; A lance le voyage.";
        case Id::TutK2SoloModePad:
            return "Cette porte ne se franchit qu'en mode solo : activez-le depuis le menu rapide sur Y, entr\xE9""e mode solo ; seul le personnage contr\xF4l\xE9 se d\xE9place alors. Amenez 3C-FD seul \xE0 l'ordinateur de s\xE9""curit\xE9 pour qu'il ouvre la porte.";
        case Id::TutK2RepairKitPad:
            return "Les kits de r\xE9paration soignent les dro\xEF""des. Tirez la g\xE2""chette gauche, allez sur M\xE9""dical avec la croix gauche et droite, puis appuyez sur A.";
        case Id::TutK2SecuritySpikePad:
            return "Cette serrure d\xE9passe vos comp\xE9tences ; utilisez une sonde de s\xE9""curit\xE9. Ciblez le conteneur, tirez la g\xE2""chette gauche pour le menu d'actions, et choisissez S\xE9""curit\xE9.";

        case Id::FmtModLoadedVersion:   return "Voice of the Old Republic charg\xE9, version %s";
        case Id::FmtChargenFeatUnnamed: return "Don %u";
        case Id::ChargenBtnRecommended: return "Recommand\xE9";
        case Id::ChargenBtnAccept:      return "Accepter";
        case Id::ChargenBtnBack:        return "Retour";
        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_fr
