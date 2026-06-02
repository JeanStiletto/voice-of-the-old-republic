// Spanish string table.
//
// Encoding: Windows-1252 hex escapes for non-ASCII (\xE1=á, \xE9=é,
// \xED=í, \xF3=ó, \xFA=ú, \xF1=ñ, \xFC=ü, \xC1=Á, \xC9=É, \xCD=Í,
// \xD3=Ó, \xDA=Ú, \xD1=Ñ, \xBF=¿, \xA1=¡). Prism's ANSI overload
// converts from CP_ACP via MultiByteToWideChar; on a Spanish Windows
// install CP_ACP = Windows-1252, so the literal bytes pass through
// unchanged.
//
// Combat speech is fed by combat_strings.cpp::kEs (engine anchors
// extracted from dialog_es.tlk); this table covers the Id::* speech
// path.

#include "strings.h"

namespace acc::strings::lang_es {

const char* Get(Id id) {
    switch (id) {
        case Id::CategoryDoor:        return "Puerta";
        case Id::CategoryNpc:         return "PNJ";
        case Id::CategoryContainer:   return "Contenedor";
        case Id::CategoryItem:        return "Objeto";
        case Id::CategoryLandmark:    return "Lugar";
        case Id::CategoryTransition:  return "Transici\xF3n";
        case Id::CategoryMapHint:     return "Indicaci\xF3n";

        case Id::EmptyDoors:          return "No hay puertas al alcance";
        case Id::EmptyNpcs:           return "No hay PNJ al alcance";
        case Id::EmptyContainers:     return "No hay contenedores al alcance";
        case Id::EmptyItems:          return "No hay objetos al alcance";
        case Id::EmptyLandmarks:      return "No hay lugares al alcance";
        case Id::EmptyTransitions:    return "No hay transiciones al alcance";
        case Id::EmptyMapHints:       return "No hay indicaciones en este mapa";
        case Id::EmptyAll:            return "No hay objetos al alcance";
        case Id::CycleNoTarget:       return "Sin objetivo";

        case Id::MapPinNoText:        return "Marcador";
        case Id::MapPinShiftDashHint: return "El marcador no se puede alcanzar directamente. Ctrl+Gui\xF3n para baliza.";
        case Id::MapPinAltDashUnsupported: return "Marcador: Alt+Gui\xF3n no soportado";
        case Id::MapPinInteractHint:  return "Marcador. Ctrl+Gui\xF3n para baliza.";

        case Id::FmtSavedMarkerAutoNumber:   return "Marcador %d";
        case Id::FmtSavedMarkerAutoWithRoom: return "%s - Marcador %d";
        case Id::FmtSavedMarkerPlaced:       return "Marcador guardado: %s";
        case Id::SavedMarkerFailed:          return "No se pudo guardar el marcador";

        case Id::FmtAnnounceWithClock: return "%s, a las %d en punto, %d metros";
        case Id::FmtAnnounceNoClock:   return "%s, %d metros";
        case Id::FmtCategoryItem:      return "%s. %s";

        case Id::FmtGuidingTo:         return "Guiando hacia %s";
        case Id::FmtGuidingFailed:     return "Guiado hacia %s fallido";
        case Id::GuidanceNoFocus:      return "Ning\xFAn objeto seleccionado";
        case Id::GuidingToPoint:       return "Caminando hacia el punto";

        case Id::MovementCancelled:    return "Movimiento cancelado";

        // Beacon (Ctrl+-).
        case Id::FmtBeaconStarted:     return "Baliza hacia %s";
        case Id::BeaconCancelled:      return "Baliza cancelada";
        case Id::FmtBeaconNoPath:      return "No hay ruta hacia %s";
        case Id::BeaconAlreadyAtDest:  return "Ya en el destino";
        // Route description.
        // Example: "Ruta hacia Puerta (25 metros): 5 metros Norte,
        //           4 metros Noreste, 6 metros Este. Sin transici\xF3n."
        case Id::FmtRouteHeader:       return "Ruta hacia %s (%d metros): %s. %s.";
        case Id::FmtRouteSegment:      return "%d metros %s";
        case Id::RouteJoinSeparator:   return ", ";
        case Id::RouteOneTransition:   return "Una transici\xF3n";
        case Id::RouteNoTransition:    return "Sin transici\xF3n";
        case Id::FmtBeaconNextSegment: return "Contin\xFA""a %d metros %s";

        case Id::FmtInteractTalk:      return "Hablar con %s";
        case Id::FmtInteractOpen:      return "Usar %s";
        case Id::FmtInteractTake:      return "Recoger %s";
        case Id::FmtInteractFailed:    return "Interacci\xF3n con %s fallida";
        case Id::FmtInteractEngine:    return "%s %s";
        case Id::FmtInteractRadial:    return "Men\xFA de acciones, %s";
        case Id::FmtInteractNoActionsRedirect: return "No hay acciones disponibles para %s. Pulsa Intro para activar.";
        case Id::FmtInteractNoActions: return "No hay acciones disponibles para %s.";

        case Id::FmtActionBarOpened:      return "Barra de acciones columna %d: %s, %d opciones";
        case Id::FmtActionBarColumnEmpty: return "La columna %d est\xE1 vac\xED""a";
        case Id::ActionBarColumnEmpty:    return "Columna vac\xED""a";
        case Id::FmtActionBarFired:       return "%s usado";
        case Id::FmtFireAtPosition:       return "%s, posici\xF3n %d";
        case Id::FmtFireQueueFull:        return "%s, cola llena";
        case Id::ActionBarCancelled:      return "Cancelado";

        case Id::NoTooltipAvailable:   return "Sin descripci\xF3n disponible";

        case Id::ContainerEmpty:       return "Vac\xEDo";
        case Id::ContainerOneItem:     return "1 objeto";
        case Id::FmtContainerItems:    return "%d objetos";
        case Id::FmtContainerItemAt:   return "%s, %d de %d";
        case Id::FmtItemStackSuffix:   return "%d en la pila";

        case Id::EquipSlotHead:        return "Cabeza";
        case Id::EquipSlotImplant:     return "Implante";
        case Id::EquipSlotBody:        return "Cuerpo";
        case Id::EquipSlotArmL:        return "Brazo izquierdo";
        case Id::EquipSlotArmR:        return "Brazo derecho";
        case Id::EquipSlotWeapL:       return "Arma izquierda";
        case Id::EquipSlotWeapR:       return "Arma derecha";
        case Id::EquipSlotBelt:        return "Cintur\xF3n";
        case Id::EquipSlotHands:       return "Manos";

        case Id::FmtEquipSlotItem:     return "%s, %s";
        case Id::FmtEquipSlotEmpty:    return "%s, vac\xEDo";
        case Id::FmtEquipVitality:     return "Vitalidad %s";
        case Id::FmtEquipDefense:      return "Defensa %s";
        case Id::FmtEquipAttack:       return "Ataque %s";
        case Id::FmtEquipAttackDual:   return "Ataque izquierdo %s, derecho %s";
        case Id::FmtEquipDamage:       return "Da\xF1o %s";
        case Id::FmtEquipDamageDual:   return "Da\xF1o izquierdo %s, derecho %s";

        case Id::FmtTransitionArea:    return "\xC1rea: %s";
        case Id::FmtTransitionRoom:    return "Habitaci\xF3n: %s";
        case Id::FmtTransitionRoomIndex: return "Habitaci\xF3n %d";
        case Id::FmtTransitionLoading: return "Cargando: %s";

        case Id::DoorOpen:             return "abierta";
        case Id::DoorLocked:           return "cerrada con llave";

        case Id::DirNorth:             return "Norte";
        case Id::DirNortheast:         return "Noreste";
        case Id::DirEast:              return "Este";
        case Id::DirSoutheast:         return "Sureste";
        case Id::DirSouth:             return "Sur";
        case Id::DirSouthwest:         return "Suroeste";
        case Id::DirWest:              return "Oeste";
        case Id::DirNorthwest:         return "Noroeste";

        case Id::StuckFreeDirsPrefix:  return "Libre";
        case Id::StuckAllBlocked:      return "Todo bloqueado";

        case Id::FmtCompassDegrees:    return "%d grados";

        case Id::FmtMapStateOriented:    return "%s. Orientado a %d grados en el mapa, %s.";
        case Id::FmtMapStateUnknownRoom: return "Orientado a %d grados en el mapa, %s.";

        case Id::FmtWorldStateOriented:       return "%s. %s.";
        case Id::FmtWorldStateUnknownCluster: return "%s.";

        case Id::MouseLookOn:          return "Vista con rat\xF3n activada";
        case Id::MouseLookOff:         return "Vista con rat\xF3n desactivada";

        case Id::ViewModeOn:           return "Modo observaci\xF3n activado";
        case Id::ViewModeOff:          return "Modo observaci\xF3n desactivado";

        case Id::FmtSaveLoadRow:       return "%s, %s, %s, %d de %d";
        case Id::FmtSaveLoadRowNoLoc:  return "%s, %d de %d";

        case Id::LevelUpOpen:          return "Subir de nivel";
        case Id::LevelUpFailed:        return "Subir de nivel fallido";
        case Id::LevelUpAlreadyOpen:   return "Subir de nivel ya abierto";

        case Id::PortraitLabel:        return "Retrato";
        case Id::PortraitArrowPrev:    return "Retrato anterior";
        case Id::PortraitArrowNext:    return "Retrato siguiente";
        case Id::FmtPortraitArrow:     return "%s: %s";
        case Id::FmtPortraitArrowId:   return "%s %d";
        case Id::PortraitGenderFemale: return "femenino";
        case Id::PortraitGenderMale:   return "masculino";
        case Id::PortraitRaceAsian:    return "asi\xE1tico";
        case Id::PortraitRaceDark:     return "de piel oscura";
        case Id::PortraitRaceLight:    return "de piel clara";
        case Id::FmtPortraitDescription: return "%s %s %d";

        case Id::FmtPartyPortraitInTeam:    return "%s, en el grupo";
        case Id::FmtPartyPortraitAvailable: return "%s, disponible";

        case Id::DisabledSuffix:       return ", no disponible";

        case Id::FmtCharSheetClass:    return "%s. ";
        case Id::FmtCharSheetLevel:    return "Nivel %s. ";
        case Id::FmtCharSheetXp:       return "Experiencia %s de %s. ";
        case Id::FmtCharSheetHp:       return "Puntos de vida %s. ";
        case Id::FmtCharSheetFp:       return "Puntos de Fuerza %s. ";
        case Id::FmtCharSheetStr:      return "Fuerza %s%s%s. ";
        case Id::FmtCharSheetDex:      return "Destreza %s%s%s. ";
        case Id::FmtCharSheetCon:      return "Constituci\xF3n %s%s%s. ";
        case Id::FmtCharSheetInt:      return "Inteligencia %s%s%s. ";
        case Id::FmtCharSheetWis:      return "Sabidur\xED""a %s%s%s. ";
        case Id::FmtCharSheetCha:      return "Carisma %s%s%s. ";
        case Id::FmtCharSheetAlignment: return "Alineamiento %u de %u.";

        case Id::FmtChargenAttrInfoSuffix:               return "Modificador %s, Coste %s";
        case Id::FmtChargenAttrValueChangeBare:          return "%s, puntos restantes %s";
        case Id::FmtChargenAttrValueChangeWithMod:       return "%s, Modificador %s, puntos restantes %s";
        case Id::FmtChargenAttrValueChangeWithCost:      return "%s, puntos restantes %s, Coste %s";
        case Id::FmtChargenAttrValueChangeWithModAndCost: return "%s, Modificador %s, puntos restantes %s, Coste %s";

        case Id::FmtChargenSkillInfoSuffix:  return "Coste %s";
        case Id::FmtChargenSkillValueChange: return "%s, puntos restantes %s";

        case Id::ChargenFeatGrantedTitle:    return "Recibes estos talentos";
        case Id::FmtChargenFeatGrantedRow:   return "%s, %d de %d";

        case Id::FmtChargenFeatChartCell:    return "%s, %s";
        case Id::ChargenFeatStatusAvailable: return "disponible";
        case Id::ChargenFeatStatusExisting:  return "ya aprendido";
        case Id::ChargenFeatStatusGranted:   return "otorgado autom\xE1ticamente";
        case Id::ChargenFeatStatusLocked:    return "falta requisito previo";
        case Id::ChargenFeatStatusChosen:    return "elegido";

        case Id::EditboxRole:                return "campo de texto";
        case Id::EditboxEmpty:               return "vac\xEDo";
        case Id::EditboxEnd:                 return "fin";

        case Id::CombatBegins:               return "Comienza el combate";
        case Id::CombatEnds:                 return "Termina el combate";

        case Id::PcStatNoCharacter:          return "No hay estado del personaje disponible.";

        // Brief is composed in BuildTargetCombatBrief: name, then optional
        // condition / distance / effects / weapons clauses each with a
        // leading space and trailing period.
        case Id::FmtTargetCombatBrief:       return "%s.";
        case Id::FactionHostile:             return "hostil";
        case Id::FactionFriendly:            return "amistoso";
        case Id::FactionNeutral:             return "neutral";
        case Id::TargetIsDead:               return "muerto";

        case Id::FmtBriefCondition:          return " %s.";
        case Id::FmtBriefDistanceMeters:     return " %d metros.";
        case Id::FmtBriefEffects:            return " %s.";
        case Id::FmtBriefWielding:           return " %s.";
        case Id::FmtBriefOffHand:            return " mano secundaria %s.";
        case Id::FmtBriefEffectsCount:       return " %d efectos activos.";
        case Id::FmtBriefFeatsCount:         return " %d talentos.";
        case Id::FmtSelfStatusHp:            return "%d puntos de vida.";
        case Id::FmtSelfStatusHpOf:          return "%d de %d puntos de vida.";

        case Id::ExamineOpened:              return "Examinar.";
        case Id::ExamineNoTarget:            return "Sin objetivo para examinar.";
        case Id::ExamineFailed:              return "Examinar fallido.";

        case Id::FmtExamineOpened:           return "Examinar: %s. %d entradas.";
        case Id::FmtExamineRowOf:            return "%s. %d de %d.";
        case Id::ExamineViewClosed:          return "Examinar cerrado.";
        case Id::FmtExamineRowName:          return "Nombre: %s";
        case Id::FmtExamineRowFaction:       return "Disposici\xF3n: %s";
        case Id::FmtExamineRowHp:            return "Puntos de vida: %d";
        case Id::FmtExamineRowDistance:      return "Distancia: %d metros";
        case Id::FmtExamineRowWeapon:        return "Mano principal: %s";
        case Id::ExamineRowWeaponNone:       return "Mano principal: ninguna";
        case Id::FmtExamineRowEffect:        return "Efecto: %s";
        case Id::FmtExamineRowFeat:          return "Talento: %s";
        case Id::FmtExamineRowEffectUnknown: return "Efecto n.\xBA %d";
        case Id::FmtExamineRowFeatUnknown:   return "Talento n.\xBA %d";
        case Id::ExamineRowNoEffects:        return "Sin efectos activos";
        case Id::ExamineRowNoFeats:          return "Sin talentos";

        case Id::FmtExamineRowHpFull:        return "Puntos de vida: %d de %d";
        case Id::FmtExamineRowLevel:         return "Nivel: %d";
        case Id::FmtExamineRowCondition:     return "Estado: %s";
        case Id::DamageLevel0Healthy:        return "ileso";
        case Id::DamageLevel1Light:          return "levemente herido";
        case Id::DamageLevel2Wounded:        return "herido";
        case Id::DamageLevel3Badly:          return "gravemente herido";
        case Id::DamageLevel4Dying:          return "agonizando";
        case Id::DamageLevel5Dead:           return "muerto";
        case Id::FmtExamineRowOffHand:       return "Mano secundaria: %s";
        case Id::FmtExamineRowHead:          return "Cabeza: %s";
        case Id::FmtExamineRowTorso:         return "Armadura: %s";
        case Id::FmtExamineRowHands:         return "Manos: %s";
        case Id::ExamineRowStatusInvisible:  return "Invisible";
        case Id::ExamineRowStatusBlind:      return "Ciego";

        case Id::FmtQueueOpen:               return "Cola de acciones, %d acciones.";
        case Id::QueueEmpty:                 return "La cola de acciones est\xE1 vac\xED""a.";
        case Id::FmtQueueRow:                return "%s: %s %s, %d de %d.";
        case Id::FmtQueueRemoved:            return "Eliminado: %s.";
        case Id::QueueCleared:               return "Cola vaciada.";
        case Id::QueueClosed:                return "Cola cerrada.";
        case Id::QueueRemoveFailed:          return "No se puede eliminar esta acci\xF3n.";
        case Id::QueueVerbAttack:            return "Ataque";
        case Id::QueueVerbCastForce:         return "Lanzar poder de la Fuerza";
        case Id::QueueVerbItemCast:          return "Usar objeto";
        case Id::QueueVerbEquip:             return "Equipar";
        case Id::QueueVerbUnequip:           return "Desequipar";
        case Id::QueueVerbMove:              return "Mover";
        case Id::QueueVerbHeal:              return "Curar";
        case Id::QueueVerbUseTalent:         return "Usar talento";
        case Id::QueueVerbCutscene:          return "Cinem\xE1tica";
        case Id::QueueVerbUnknown:           return "Acci\xF3n";

        // Skeleton: max-HP is not yet read safely (suspected engine
        // accessor), so the hit / crit messages omit the "X of Y hp"
        // tail. Args reduced to (attacker, target, damage).
        case Id::FmtAttackHit:               return "%s acierta a %s por %d de da\xF1o.";
        case Id::FmtAttackMiss:              return "%s falla a %s.";
        case Id::FmtAttackCrit:              return "\xA1""Cr\xEDtico! %s golpea a %s por %d de da\xF1o.";
        case Id::FmtAttackDeflected:         return "El ataque de %s sobre %s es desviado.";

        case Id::FmtSavingThrowSucceeded:    return "%s supera la tirada: %s %d contra %d.";
        case Id::FmtSavingThrowFailed:       return "%s falla la tirada: %s %d contra %d.";
        case Id::SaveTypeFort:               return "Fortaleza";
        case Id::SaveTypeReflex:             return "Reflejos";
        case Id::SaveTypeWill:               return "Voluntad";

        case Id::FmtDialogReplies:           return "%d respuestas disponibles.";
        case Id::DialogReplyUnavailable:     return "no disponible";
        case Id::FmtDialogReplyUnavailableRow: return "%s, %s, %d de %d";

        case Id::MessagesTitleCombatLog:     return "Registro de combate.";
        case Id::MessagesTitleDialogLog:     return "Registro de di\xE1logo.";

        case Id::MapPrevNote:                return "Nota anterior";
        case Id::MapNextNote:                return "Nota siguiente";

        case Id::MapCursorUnexplored:        return "Inexplorado";
        case Id::MapCursorWaypointPOI:       return "Punto de inter\xE9s";
        case Id::MapCursorOpenArea:          return "\xC1rea abierta";
        case Id::MapCursorJunction:          return "Cruce";
        case Id::MapCursorOffPath:           return "Pared";
        case Id::FmtMapCursorCorridor:       return "%s, %.0f metros";
        case Id::FmtMapCursorDeadEnd:        return "Callej\xF3n sin salida, %s";
        case Id::FmtMapCursorJunctionDirs:   return "Cruce, %s";
        case Id::FmtMapCursorCorridorDir:    return "%s";
        case Id::MapCursorDoorNoun:          return "Puerta";
        case Id::FmtMapCursorDoor:           return "%s %s";
        case Id::FmtMapCursorDoorTransition: return "%s %s hacia %s";
        case Id::FmtMapCursorDoorLandmark:   return "%s %s, %s";
        case Id::MapCursorTransitionDoor:    return "Umbral";
        case Id::FmtMapCursorJunctionDeadEndExit: return "callej\xF3n sin salida %s";
        case Id::FmtMapCursorPlazaDirs:      return "Plaza, %s";
        case Id::AxisNorthSouth:             return "norte-sur";
        case Id::AxisEastWest:               return "este-oeste";

        case Id::FmtStorePriceBuyFinite:    return "Precio %d cr\xE9""ditos, existencias %d";
        case Id::FmtStorePriceBuyUnlimited: return "Precio %d cr\xE9""ditos, existencias ilimitadas";
        case Id::FmtStorePriceSell:         return "Precio %d cr\xE9""ditos, tienes %d";
        case Id::StoreModeBuy:              return "Modo compra";
        case Id::StoreModeSell:             return "Modo venta";
        case Id::StoreSold:                 return "Vendido";
        case Id::StoreBought:               return "Comprado";
        case Id::StoreCannotSell:           return "No se puede vender";
        case Id::StoreCannotBuy:            return "No se puede comprar";
        case Id::FmtStoreSoldFor:           return "Vendido por %d cr\xE9""ditos";
        case Id::FmtStoreBoughtFor:         return "Comprado por %d cr\xE9""ditos";

        // ----- Pazaak -----
        case Id::PazaakStart:            return "Pazaak. Pulsa C para tu mano, T para la mesa.";
        case Id::PazaakEmpty:            return "vac\xED""o";
        case Id::PazaakFaceDown:         return "boca abajo";
        case Id::PazaakBoardEmpty:       return "vac\xED""a";
        case Id::PazaakFmtPlus:          return "m\xE1s %d";
        case Id::PazaakFmtMinus:         return "menos %d";
        case Id::PazaakFmtPlain:         return "%d";
        case Id::PazaakFmtFlipBoth:      return "m\xE1s o menos %d";
        case Id::PazaakFmtFlipCurrently: return "%s, actualmente %s";
        case Id::PazaakFmtYouDrew:       return "Robas %s. Tu total %d.";
        case Id::PazaakOverTwenty:       return "M\xE1s de veinte.";
        case Id::PazaakFmtYouPlayed:     return "Jugado %s. Tu total %d.";
        case Id::PazaakYourTurn:         return "Tu turno.";
        case Id::PazaakTurnEnded:        return "Turno terminado.";
        case Id::PazaakFmtOppDrew:       return "El rival roba %s. Total %d.";
        case Id::PazaakFmtOppPlayed:     return "El rival juega %s. Total %d.";
        case Id::PazaakFmtOppStands:     return "El rival se planta en %d.";
        case Id::PazaakFmtYouStand:      return "Te plantas en %d.";
        case Id::PazaakFmtWinSet:        return "Ganas la ronda. %d a %d.";
        case Id::PazaakFmtLoseSet:       return "Pierdes la ronda. %d a %d.";
        case Id::PazaakWinMatch:         return "\xA1""Ganas la partida!";
        case Id::PazaakLoseMatch:        return "Pierdes la partida.";
        case Id::PazaakTieReplay:        return "Empate. Se repite la ronda.";
        case Id::PazaakFmtHand:          return "Mano: %s";
        case Id::PazaakHandEmpty:        return "Mano vac\xED""a.";
        case Id::PazaakFmtYourBoard:     return "Tu mesa: %s, total %d.";
        case Id::PazaakFmtOppBoard:      return "Mesa rival: %s, total %d.";
        case Id::PazaakNoPlayable:       return "No hay cartas para jugar.";
        case Id::PazaakNotYourTurn:      return "No es tu turno.";
        case Id::PazaakSelectCardFirst:  return "Elige una carta primero.";
        case Id::PazaakChooseSign:       return "Elige el signo. Izquierda o derecha para cambiar, Enter para jugar.";
        case Id::PazaakCancelled:        return "Cancelado.";
        case Id::PazaakDeckAvailable:    return "%s, %d disponibles";
        case Id::PazaakDeckNoneLeft:     return "%s, no quedan";
        case Id::PazaakDeckSlotFilled:   return "Ranura %d: %s";
        case Id::PazaakDeckSlotEmpty:    return "Ranura %d: vac\xED""a";
        case Id::PazaakDeckPlay:         return "Jugar, %d de 10 en la baraja";
        case Id::PazaakDeckAdded:        return "A\xF1""adido %s. %d de 10.";
        case Id::PazaakDeckRemoved:      return "Eliminado %s.";
        case Id::PazaakDeckFull:         return "Baraja completa.";
        case Id::PazaakFmtOppHand:       return "El rival tiene %d cartas en la mano.";
        case Id::PazaakStandLabel:       return "Plantarse";
        case Id::PazaakEndTurnLabel:     return "Terminar ronda";
        case Id::PazaakWagerLess:        return "Reducir apuesta";
        case Id::PazaakWagerMore:        return "Aumentar apuesta";
        case Id::PazaakFmtWager:         return "Apuesta %d de %d m\xE1ximo.";
        case Id::PazaakFmtWagerRow:      return "Apuesta %d. %s";
        case Id::FmtStoreNotEnoughCredits:  return "Cr\xE9""ditos insuficientes, faltan %d, tienes %d";

        case Id::FmtCredits:                return "Cr\xE9""ditos: %s";

        case Id::WorkbenchSlotWeapon1:       return "Ranura de mejora 1";
        case Id::WorkbenchSlotWeapon2:       return "Ranura de mejora 2";
        case Id::WorkbenchSlotWeapon3:       return "Ranura de mejora 3";
        case Id::WorkbenchSlotSaberCrystal1: return "Ranura de cristal 1";
        case Id::WorkbenchSlotSaberCrystal2: return "Ranura de cristal 2";
        case Id::WorkbenchSlotSaberCrystal3: return "Ranura de cristal 3";
        case Id::WorkbenchSlotSaberCrystal4: return "Ranura de cristal 4";
        case Id::WorkbenchItemsEmpty:        return "No hay objetos mejorables en esta categor\xED""a";
        case Id::WorkbenchUpgradesEmpty:     return "No hay mejoras compatibles en el inventario";
        case Id::WorkbenchSlotInstalled:     return "Mejora instalada";
        case Id::WorkbenchSlotRemoved:       return "Mejora eliminada";
        case Id::WorkbenchSlotNoMatch:       return "No hay mejora correspondiente en el inventario";

        case Id::SoundOptionsMovieVolume:    return "Volumen de v\xED""deo";

        case Id::SwoopRaceStarted:
            // Terse opener — concatenated with SwoopRaceControls below
            // into one utterance by swoop_race.cpp::AnnounceEntry.
            return "Carrera de Swoop.";
        case Id::SwoopRaceControls:
            // Short cheat sheet. Full keymap (Enter/Mouse 1 also work,
            // Pause pauses the race, etc.) is in the manual — verbose
            // spoken intro got in the way of the start countdown.
            return "Espacio para acelerar. A y D para girar.";
        case Id::SwoopRaceEnded:
            return "Carrera de Swoop terminada.";
        case Id::SwoopRaceObstacleNear:
            return "Obst\xE1""culo a %d metros";
        case Id::FmtSwoopRaceGear:
            return "Marcha %d";

        case Id::TurretGameStarted:
            // Terse opener — concatenated with TurretGameControls below
            // into one utterance by turret_game.cpp::AnnounceEntry.
            return "Torreta.";
        case Id::TurretGameControls:
            return "Apunta con WASD. Espacio para disparar.";
        case Id::TurretGameEnded:
            return "Torreta terminada.";

        case Id::ModSettingsRootLabel:        return "Ajustes del mod";
        case Id::ModSettingsOpened:           return "Ajustes del mod abiertos";
        case Id::ModSettingsClosed:           return "Ajustes del mod cerrados";
        case Id::ModSettingExtendedCycling:   return "Selecci\xF3n de objetos en todo el mapa";
        case Id::ModSettingRoomShapes:        return "Descripciones de la forma de las habitaciones";
        case Id::ModSettingWallSounds:        return "Sonidos de pared";
        case Id::ModSettingHumanSubtitles:    return "Leer subt\xEDtulos de hablantes humanos";
        case Id::ModSettingSkipIntros:        return "Omitir v\xED" "deos de introducci\xF3n";
        case Id::ModSettingSkipIntrosOnNextLaunch: return "Los v\xED" "deos de introducci\xF3n se omitir\xE1n en el pr\xF3ximo inicio.";
        case Id::ModSettingPlayIntrosOnNextLaunch: return "Los v\xED" "deos de introducci\xF3n se reproducir\xE1n en el pr\xF3ximo inicio.";
        case Id::ModSettingSkipIntrosToggleFailed: return "No se pudo cambiar los v\xED" "deos de introducci\xF3n. Los archivos pueden haber sido eliminados.";
        case Id::ModSettingStateOn:           return "activado";
        case Id::ModSettingStateOff:          return "desactivado";
        case Id::FmtModSettingOption:         return "%s: %s";
        case Id::ModSettingCueVolume:         return "Volumen de los sonidos de aviso";
        case Id::FmtModSettingSlider:         return "%s: %d por ciento";

        case Id::ModSettingAudioGlossary:           return "Glosario de audio";
        case Id::ModSettingsAudioGlossaryOpened:    return "Glosario de audio abierto";
        case Id::GlossaryEntryDoorOpen:             return "Puerta abierta";
        case Id::GlossaryEntryDoorClosedMetal:      return "Puerta de metal cerrada";
        case Id::GlossaryEntryDoorClosedWood:       return "Puerta de madera cerrada";
        case Id::GlossaryEntryDoorClosedStone:      return "Puerta de piedra cerrada";
        case Id::GlossaryEntryWall:                 return "Pared";
        case Id::GlossaryEntryHazard:               return "Peligro";
        case Id::GlossaryEntryCollision:            return "Colisi\xF3n";
        case Id::GlossaryEntryBeaconActive:         return "Baliza activa";
        case Id::GlossaryEntryBeaconWaypoint:       return "Punto intermedio alcanzado";
        case Id::GlossaryEntryBeaconDestination:    return "Destino alcanzado";
        case Id::GlossaryEntrySwoopAccelTick:       return "Tic de la barra de marchas Swoop";
        case Id::GlossaryEntrySwoopAccelpadBoost:   return "Panel acelerador Swoop";
        case Id::GlossaryEntrySwoopObstacleWarn:    return "Aviso de obst\xE1""culo Swoop";
        case Id::GlossaryEntrySwoopWallImpact:      return "Impacto contra pared Swoop";

        case Id::FmtUpdateAvailable:    return "Actualizaci\xF3n disponible, versi\xF3n %s. Pulsa F5 desde el men\xFA principal para instalar.";
        case Id::UpdateDownloading:     return "Descargando actualizaci\xF3n.";
        case Id::UpdateDownloaded:      return "Actualizaci\xF3n descargada. Cerrando juego para instalar.";
        case Id::UpdateFailed:          return "Error al descargar la actualizaci\xF3n.";
        case Id::FmtUpdateNotAvailable: return "No hay actualizaci\xF3n disponible. Est\xE1s en la versi\xF3n %s.";
        case Id::UpdateNotInMenu:       return "Las actualizaciones solo se pueden instalar desde el men\xFA principal.";

        case Id::PanelTitleMainMenu:    return "Men\xFA principal";
        case Id::LoadingPleaseWait:     return "El juego sigue cargando, por favor espera.";
        case Id::LoadingStuckWorkaround: return "El men\xFA sigue sin responder. Pulsa Alt F4 y cancela el di\xE1logo de salida para despertarlo.";

        case Id::GamePaused:            return "Pausa.";
        case Id::GameResumed:           return "Pausa quitada.";

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_es
