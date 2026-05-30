# strings.h (1178 lines)

User-facing string table. Centralises every spoken string. Logs stay English; only speech uses Get(). Source files use Windows-1252 hex escapes for German non-ASCII so literal bytes match Prism's CP_ACP ANSI overload.

## Declarations (in source order)

- L25 — `namespace acc::strings`
- L27 — `enum class Id : int`
  note: StringId enum; names only (values omitted per task rules). Full list:
  CategoryDoor, CategoryNpc, CategoryContainer, CategoryItem, CategoryLandmark, CategoryTransition, CategoryMapHint,
  EmptyDoors, EmptyNpcs, EmptyContainers, EmptyItems, EmptyLandmarks, EmptyTransitions, EmptyMapHints, EmptyAll, CycleNoTarget,
  MapPinNoText, MapPinShiftDashHint, MapPinAltDashUnsupported, MapPinInteractHint,
  FmtSavedMarkerAutoNumber, FmtSavedMarkerAutoWithRoom, FmtSavedMarkerPlaced, SavedMarkerFailed,
  FmtAnnounceWithClock, FmtAnnounceNoClock, FmtCategoryItem,
  FmtGuidingTo, FmtGuidingFailed, GuidanceNoFocus, GuidingToPoint,
  MovementCancelled,
  FmtBeaconStarted, BeaconCancelled, FmtBeaconNoPath, BeaconAlreadyAtDest, FmtRouteHeader, FmtRouteSegment, RouteJoinSeparator, RouteOneTransition, RouteNoTransition, FmtBeaconNextSegment,
  FmtInteractTalk, FmtInteractOpen, FmtInteractTake, FmtInteractFailed,
  FmtInteractEngine, FmtInteractRadial,
  FmtActionBarOpened, FmtActionBarColumnEmpty, ActionBarColumnEmpty, FmtActionBarFired, ActionBarCancelled,
  NoTooltipAvailable,
  ContainerEmpty, ContainerOneItem, FmtContainerItems, FmtContainerItemAt, FmtItemStackSuffix,
  EquipSlotHead, EquipSlotImplant, EquipSlotBody, EquipSlotArmL, EquipSlotArmR, EquipSlotWeapL, EquipSlotWeapR, EquipSlotBelt, EquipSlotHands,
  FmtEquipSlotItem, FmtEquipSlotEmpty,
  FmtEquipVitality, FmtEquipDefense, FmtEquipAttack, FmtEquipAttackDual, FmtEquipDamage, FmtEquipDamageDual,
  FmtTransitionArea, FmtTransitionRoom, FmtTransitionRoomIndex, FmtTransitionLoading,
  DoorOpen, DoorLocked,
  DirNorth, DirNortheast, DirEast, DirSoutheast, DirSouth, DirSouthwest, DirWest, DirNorthwest,
  StuckFreeDirsPrefix, StuckAllBlocked,
  FmtCompassDegrees,
  FmtMapStateOriented, FmtMapStateUnknownRoom,
  FmtWorldStateOriented, FmtWorldStateUnknownCluster,
  MouseLookOn, MouseLookOff,
  ViewModeOn, ViewModeOff,
  FmtSaveLoadRow, FmtSaveLoadRowNoLoc,
  LevelUpOpen, LevelUpFailed,
  PortraitLabel, PortraitArrowPrev, PortraitArrowNext, FmtPortraitArrow, FmtPortraitArrowId,
  PortraitGenderFemale, PortraitGenderMale, PortraitRaceAsian, PortraitRaceDark, PortraitRaceLight, FmtPortraitDescription,
  FmtPartyPortraitInTeam, FmtPartyPortraitAvailable,
  DisabledSuffix,
  FmtCharSheetClass, FmtCharSheetLevel, FmtCharSheetXp, FmtCharSheetHp, FmtCharSheetFp, FmtCharSheetStr, FmtCharSheetDex, FmtCharSheetCon, FmtCharSheetInt, FmtCharSheetWis, FmtCharSheetCha, FmtCharSheetAlignment,
  FmtChargenAttrInfoSuffix, FmtChargenAttrValueChangeBare, FmtChargenAttrValueChangeWithMod, FmtChargenAttrValueChangeWithCost, FmtChargenAttrValueChangeWithModAndCost,
  FmtChargenSkillInfoSuffix, FmtChargenSkillValueChange,
  ChargenFeatGrantedTitle, FmtChargenFeatGrantedRow,
  FmtChargenFeatChartCell, ChargenFeatStatusAvailable, ChargenFeatStatusExisting, ChargenFeatStatusGranted, ChargenFeatStatusLocked, ChargenFeatStatusChosen,
  EditboxRole, EditboxEmpty, EditboxEnd,
  CombatBegins, CombatEnds,
  PcStatNoCharacter,
  FmtTargetCombatBrief, FactionHostile, FactionFriendly, FactionNeutral, TargetIsDead,
  FmtBriefCondition, FmtBriefDistanceMeters, FmtBriefEffects, FmtBriefWielding, FmtBriefOffHand, FmtBriefEffectsCount, FmtBriefFeatsCount,
  FmtSelfStatusHp, FmtSelfStatusHpOf,
  ExamineOpened, ExamineNoTarget, ExamineFailed,
  FmtExamineOpened, FmtExamineRowOf, ExamineViewClosed, FmtExamineRowName, FmtExamineRowFaction, FmtExamineRowHp, FmtExamineRowDistance, FmtExamineRowWeapon, ExamineRowWeaponNone, FmtExamineRowEffect, FmtExamineRowFeat, FmtExamineRowEffectUnknown, FmtExamineRowFeatUnknown, ExamineRowNoEffects, ExamineRowNoFeats,
  FmtExamineRowHpFull, FmtExamineRowLevel, FmtExamineRowCondition, DamageLevel0Healthy, DamageLevel1Light, DamageLevel2Wounded, DamageLevel3Badly, DamageLevel4Dying, DamageLevel5Dead, FmtExamineRowOffHand, FmtExamineRowHead, FmtExamineRowTorso, FmtExamineRowHands, ExamineRowStatusInvisible, ExamineRowStatusBlind,
  FmtQueueOpen, QueueEmpty, FmtQueueRow, FmtQueueRemoved, QueueCleared, QueueClosed, QueueRemoveFailed, QueueVerbAttack, QueueVerbCastForce, QueueVerbItemCast, QueueVerbEquip, QueueVerbUnequip, QueueVerbMove, QueueVerbHeal, QueueVerbUseTalent, QueueVerbCutscene, QueueVerbUnknown,
  FmtAttackHit, FmtAttackMiss, FmtAttackCrit, FmtAttackDeflected,
  FmtSavingThrowSucceeded, FmtSavingThrowFailed, SaveTypeFort, SaveTypeReflex, SaveTypeWill,
  FmtDialogReplies, DialogReplyUnavailable, FmtDialogReplyUnavailableRow,
  MessagesTitleCombatLog, MessagesTitleDialogLog,
  MapPrevNote, MapNextNote, MapCursorUnexplored, MapCursorWaypointPOI,
  MapCursorOpenArea, MapCursorJunction, MapCursorOffPath, FmtMapCursorCorridor, FmtMapCursorDeadEnd, FmtMapCursorJunctionDirs, FmtMapCursorCorridorDir, MapCursorDoorNoun, FmtMapCursorDoor, FmtMapCursorDoorTransition, FmtMapCursorDoorLandmark, MapCursorTransitionDoor, FmtMapCursorJunctionDeadEndExit, FmtMapCursorPlazaDirs,
  AxisNorthSouth, AxisEastWest,
  FmtStorePriceBuyFinite, FmtStorePriceBuyUnlimited, FmtStorePriceSell, StoreModeBuy, StoreModeSell, StoreSold, StoreBought, StoreCannotSell, StoreCannotBuy, FmtStoreSoldFor, FmtStoreBoughtFor, FmtStoreNotEnoughCredits,
  FmtCredits,
  WorkbenchSlotWeapon1, WorkbenchSlotWeapon2, WorkbenchSlotWeapon3, WorkbenchSlotSaberCrystal1, WorkbenchSlotSaberCrystal2, WorkbenchSlotSaberCrystal3, WorkbenchSlotSaberCrystal4, WorkbenchItemsEmpty, WorkbenchUpgradesEmpty, WorkbenchSlotInstalled, WorkbenchSlotRemoved, WorkbenchSlotNoMatch,
  SoundOptionsMovieVolume,
  SwoopRaceStarted, SwoopRaceControls, SwoopRaceEnded, SwoopRaceObstacleNear, FmtSwoopRaceGear,
  ModSettingsRootLabel, ModSettingsOpened, ModSettingsClosed, ModSettingExtendedCycling, ModSettingRoomShapes, ModSettingWallSounds, ModSettingStateOn, ModSettingStateOff, FmtModSettingOption,
  ModSettingAudioGlossary, ModSettingsAudioGlossaryOpened, GlossaryEntryDoorOpen, GlossaryEntryDoorClosedMetal, GlossaryEntryDoorClosedWood, GlossaryEntryDoorClosedStone, GlossaryEntryWall, GlossaryEntryHazard, GlossaryEntryCollision, GlossaryEntryBeaconActive, GlossaryEntryBeaconWaypoint, GlossaryEntryBeaconDestination,
  Count_

- L1157 — `enum class Lang : int`
  note: En, De
- L1165 — `void SetLanguage(Lang l)`
- L1166 — `Lang GetLanguage()`
- L1171 — `const char* Get(Id id)`
  note: never returns nullptr; out-of-range resolves to ""
- L1175 — `namespace lang_en { const char* Get(Id id); }`
- L1176 — `namespace lang_de { const char* Get(Id id); }`
