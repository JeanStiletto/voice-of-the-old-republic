# strings.h (2003 lines)

Central user-facing string table (i18n/ layer) sitting between every speech
path and Prism. One giant `enum class Id : int` (~700 entries, comment-grouped
by feature) plus `Get(Id)` dispatch and the `Lang` enum (En/De/Fr/It/Es/Ru).
Logs always stay English (developer log lines never route through here); only
things the player actually hears use `Get()`. Encoding: source files use
7-bit ASCII (English) or Windows-1252 hex escapes (German/French/Italian/
Spanish) so literal bytes match Prism's `CP_ACP` ANSI overload; Russian is the
one exception, pinned to Windows-1251 regardless of the running Windows locale
(`CodepageFor`). Format-string IDs keep a stable printf argument order across
languages — wording/idiom changes, not argument order. Default active language
is German (`SetLanguage`/`GetLanguage`, set once at init; a runtime options-UI
toggle is deferred).

## Id enum section groupings (comment-header order; ~700 ids total)

- Cycle vocabulary: category names (Category*), map-hint override, empty-category phrases (Empty*), CycleNoTarget
- Map-pin activation + saved user markers (MapPin*, FmtSavedMarker*, SavedMarkerFailed)
- Shipped curated map hints + note renames (Hint*, MapNote*)
- Per-item announce templates (FmtAnnounceWithClock/NoClock, FmtCategoryItem)
- Trap warnings (FmtTrapDetected, MineNoun) — see mine-trap-model.md
- Pillar 4 guidance binding + cancel + dialog walk-then-talk blocked (FmtGuidingTo/Failed, GuidanceNoFocus, GuidingToPoint, MovementCancelled, InteractWayBlocked, FmtInteractWayBlockedTarget)
- Endar Spire spectator-battle cue (SpectatorBattleDoomed) — see spectator_scene.cpp
- Pillar 3 Mode B beacon + route description (FmtBeacon*, BeaconCancelled, FmtRoute*, RouteJoinSeparator, RouteOneTransition, RouteNoTransition)
- Lay-off 9b combined autowalk+interact (FmtInteractTalk/Open/Take/Failed, FmtInteractEngine, FmtInteractRadial, FmtInteractNoActions*)
- Endar Spire sealed-door + room-5 softlock hints (DoorSealedNoOpen, EndarDoorBattleHint, EndarStuckReloadHint)
- Player action bar / Aktionsmenü submenu (FmtActionBar*, FmtFireAtPosition, FmtFireQueueFull, ActionMenuClosed)
- Unified action menu categories + announce formats (MenuCat*, FmtMenuCat*, FmtMenuPlainMulti, FmtMenuCategoryEmpty)
- Generic tooltip fallback (NoTooltipAvailable)
- Container loot panel + empty-container/state-override name-tag suffixes (Container*, PlaceableState*, FmtItemStackSuffix, FmtItemChargeSuffix)
- Equipment screen (EquipSlot*, FmtEquipSlotItem/Empty, EquipUnequipped, FmtEquipVitality/Defense/Attack*/Damage*)
- Pillar 2 transitions — area/room announce, resref-style room fallback, pre-load destination (FmtTransition*)
- Door state suffixes (DoorOpen, DoorLocked, DoorCosmetic)
- Compass directions + stuck-direction probe + exact-heading announce (Dir*, StuckFreeDirsPrefix, StuckAllBlocked, FmtCompassDegrees)
- Map-frame and world-frame orientation announce (FmtMapState*, FmtWorldState*)
- Mouse Look / view-mode toggles (MouseLookOn/Off, ViewModeOn/Off)
- Save/Load panel rows (FmtSaveLoadRow, FmtSaveLoadRowNoLoc)
- Level-up hotkey + step ordering + screen hint (LevelUp*, FmtLevelUpDoStepFirst, LevelUpStepLocked, LevelUpScreenHint)
- Chargen portrait selection + description composition (Portrait*, FmtPortrait*)
- Party-selection portrait status + full-party refusal (FmtPartyPortrait*, PartySelectionFull)
- Generic disabled/toggle/slider suffixes (DisabledSuffix, ToggleOn/Off, FmtSliderValue*)
- Equipment screen name fallback (EquipMenuName)
- Character sheet opener composition (FmtCharSheet*)
- Chargen Attributes / Skills / Feats panels (FmtChargenAttr*, FmtChargenSkill*, ChargenFeat*, FmtChargenFeatChartCell)
- Editbox role/empty/end cues (Editbox*)
- Keyboard-mapping screen rows (FmtKeyBinding, KeyBindingFixed, FmtKeyBindCapture, KeyBindNotChangeable)
- Combat mode transitions + leader-at-peace + no-character fallback (CombatBegins/Ends, CombatLeaderAtPeace, PcStatNoCharacter)
- Q/E target combat brief + faction words + enrichment clauses (FmtTargetCombatBrief, Faction*, TargetIsDead, FmtBrief*)
- Bare-H self status (FmtSelfStatusHp, FmtSelfStatusHpOf, FmtSelfStatusFpOf)
- Examine hotkey (Ö) — open/close/no-target + full navigable row set incl. easy-wins extension (Examine*, FmtExamineRow*, DamageLevel0..5)
- Action queue submenu + verb words (FmtQueue*, Queue*, QueueVerb*)
- Per-attack resolved callouts + saving-throw callout (FmtAttack*, FmtSavingThrow*, SaveType*)
- Dialog reply-availability suffix (DialogReplyUnavailable, FmtDialogReplyUnavailableRow)
- Messages-panel review titles (MessagesTitleCombatLog/DialogLog)
- In-game map UI: prev/next note, cursor unexplored/waypoint/terrain vocabulary, nav-graph corridor/junction/door/area labels (Map*, FmtMapCursor*, Axis*, AreaNoun*, FmtArea*)
- Store/trading panel + per-trade speech (FmtStorePrice*, StoreMode*, Store*, FmtStoreSoldFor/BoughtFor/NotEnoughCredits)
- Pazaak minigame (Pazaak* — board, hand, deck builder, wager popup)
- Journal quest-items button label, virtual credits row (JournalQuestItemsButton, FmtCredits)
- Workbench (upgrade/upgradeitems/upgradesel panels): slot labels, empty states, install/remove outcomes, occupancy peek (Workbench*)
- Sound options movie-volume label fix-up (SoundOptionsMovieVolume)
- Swoop racing + turret minigames (SwoopRace*, FmtSwoopRace*, TurretGame*, FmtTurret*, TurretNoTargets, TurretTargetLost)
- Mod-settings virtual submenu incl. cue/urgent volume sliders, skip-intros toggle, audio glossary (ModSetting*, FmtModSetting*, ModSettingAudioGlossary, GlossaryEntry*)
- Auto-updater (F5) announcements (FmtUpdateAvailable/NotAvailable, UpdateDownload*, UpdateFailed, UpdateNotInMenu)
- Panel-title override, bringup-phase nag, pause transition cues (PanelTitleMainMenu, Loading*, GamePaused/Resumed)
- Galaxy map title (GalaxyMapTitle)
- Help system (F1 list + Ctrl+F1 context): group headers, per-key phrases, framing (Help*, FmtHelp*)
- Steam Big Picture input-blocked warning (InputBlockedBigPicture)
- Mod keybind configurator (Tastenbelegung) chrome + per-action display names (Keybind*, FmtKeybind*, KbName*)
- Endar Spire + Taris tutorial keyboard hints, Surface 1/2/3 (TutHint*, TutTrask*, TutStealthMode, TutLevelUp, DialogRepeatLineHint)
- Rakatan temple floor-plate puzzle (unk_m44ab) — intro, plate names, deltas, solved (FloorPuzzle*, FmtFloor*, FmtPlate*, PlateCenterWord, PlateResetName, PlatesAllDark)
- Terminal sentinel: `Count_`

## Other declarations

- L1963 — `enum class Lang : int { En, De, Fr, It, Es, Ru }`
  note: Ru is content-detected, not TLK LanguageID-detected (Allard's tlk declares English ID 0 but is Windows-1251 Cyrillic)
- L1978 — `unsigned CodepageFor(Lang l)`
- L1983 — `void SetLanguage(Lang l)` / L1984 `Lang GetLanguage()`
  note: default is German per user direction; no locking (speech path is single-threaded)
- L1989 — `const char* Get(Id id)`
  note: never returns nullptr — out-of-range/Count_ resolves to ""
- L1996-2001 — `namespace lang_en/lang_de/lang_fr/lang_it/lang_es/lang_ru { const char* Get(Id id); }`
  note: each defined in the matching strings_xx.cpp; strings.cpp's dispatcher picks one
