# menus_listbox.cpp (1691 lines)

> Updated 2026-07-29 (Phase-2 candidate 23, narrowed). The armed-picker STATE
> and the two picker monitors moved to `menus_listbox_picker.cpp`; all
> thirteen specs stayed here. Line numbers below predate that move and are
> stale by roughly -50 from the EquipPicker spec onward — the declaration
> list is still accurate as an inventory.

Spec-table dispatcher for every "arrow-drives-a-listbox, Enter-confirms, Esc-backs-out" panel: Container (loot), SaveLoad, EquipPicker, SkillInfoBox (chargen feat-granted popup + in-world level-up hint), InGameMessages (combat log), 4 dialogue-reply variants (Cinematic/CinematicCopy/Computer/ComputerCamera — cursor-drive only, speech owned by a separate monitor), WorkbenchItems, WorkbenchUpgrade, Examine, ScriptSelect (character-sheet AI-behaviour picker). Each panel is one `ListBoxPanelSpec` value (matches/armed/findListBox/announce/enrichRow/onEnter/onEsc/titleOverride/emptyStateId/alwaysReturnFromHandler/minSelFn/useEngineSelect callbacks); `TryHandleInput` walks the table generically. Also owns 3 per-tick "subsystem-paired" monitors co-located with their spec's state: `MonitorContainerSelection`, `MonitorEquipPickerSelection` (+ `ParkPickerCursorOffList` for both equip and workbench pickers), `MonitorWorkbenchUpgradePicker`, `PollContainerGiveModeKey`. Talks to `menus_extract` (FromControl row text), `menus_pending` (deferred button activation), `menus_internal` (DriveListBoxSelection[Engine], FindControlById), `engine_levelup`, `transitions` (external-load latch on SaveLoad-load).

## Declarations (in source order)

- L55-61 — `constexpr int kContainerBtnOkId/GiveId/CancelId, kSaveLoadLbGamesId/BtnBackId/BtnSaveLoadId` (anonymous ns)
- (picker state + accessors moved to `menus_listbox_picker.cpp`)
- L133 — `struct ListBoxPanelSpec` — the 16-field spec (see header note); `useEngineSelect` gates raw vs engine-native SetSelectedControl drive
- L250-334 — Container spec: `ContainerMatches/FindLb/Announce/OnEnter/OnEsc`, `kContainerSpec`
  note: per-item take is UNRESOLVED (row FireActivate and click-sim both fail); Enter dispatches BTN_OK (take-all) unconditionally
- L340-425 — SaveLoad spec: `SaveLoadMatches/FindLb/Announce/LogExtra/OnEnter/OnEsc`, `kSaveLoadSpec`
  note: OnEnter arms the module-load latch via NotifyExternalLoadStarting only when !GetPlayerPosition() (load-from-menu, not in-world)
- L433-571 — EquipPicker spec: `EquipPickerMatchesPanel/Armed/ResetStale/FindLb/Announce/LogExtra/OnEnter/OnEsc`, `kEquipPickerSpec`
  note: Enter routes to unequip (row-0 0x7f000000 empty entry) when the selected row's field6_0x394 bit 0x2 marks it already-equipped
- L596-802 — SkillInfoBox spec (chargen "ShowGranted" feats popup / in-world level-up hint): `FindFeatsCharGenPanel`, `ResolveFeatIdFromRowStrref` (reverse feat-name-strref lookup against Rules->feats[]), `SkillInfoBoxAnnounce/EnrichRow/OnEnter/TitleOverride`, `kSkillInfoBoxSpec`
- L817-859 — InGameMessages spec (combat log, Phase 1C): `InGameMessagesMatches/FindLb/Announce/TitleOverride`, `kInGameMessagesSpec`
- L868-960 — Dialog reply specs (Phase 1D, cursor-drive only, no announce — MonitorDialogReplies in menus_monitors.cpp is sole speaker): `DialogCinematicMatches/CopyMatches/ComputerMatches/ComputerCameraMatches`, `DialogFindRepliesLb`, 4 specs
- L970-1026 — WorkbenchItems spec (per-category item picker): `WorkbenchItemsMatches/FindLb/Announce/OnEnter/OnEsc`, `kWorkbenchItemsSpec`
- L1048-1227 — WorkbenchUpgrade spec (slot detail): `WorkbenchUpgradeMatches/Armed/ResetStale/FindLb/Announce/MinSel/OnEnter/OnEsc/TitleOverride`, `kWorkbenchUpgradeSpec`
  note: dynamic minSel hides the row-0 remove entry on power slots but not the colour slot (GetWorkbenchPickerInfo); remove-gesture routes Enter on the installed row to that hidden row
- L1249-1287 — Examine spec (Ö key panel): `ExamineMatches/FindLb/Announce`, `kExamineSpec`
- L1310-1426 — ScriptSelect spec (charsheet "Kurzbefehle" AI-behaviour picker): `ScriptSelectMatches/FindLb/Announce/EnrichRow/OnEnter/OnEsc`, `kScriptSelectSpec`
  note: EnrichRow resolves DESCRIPTION_STRREF from the panel's option table by row control-id, bypassing the engine's hover-only description refresh
- L1440 — `constexpr ListBoxPanelSpec* kSpecs[13]` — probe order table
- L1466 — `bool DispatchKeyDownEdge(spec, panel, param_1)` — routes Up/Down/Home/End to DriveListBoxSelection[Engine] + announce/enrichRow/logExtra, Enter/Esc to spec callbacks
- L1529 — `void LogStandard(n, thisPtr, param_1, param_2, consumed)`
- L1548 — `bool acc::menus::listbox::TryHandleInput(n, thisPtr, activePanel, param_1, param_2, outRv)` — walks kSpecs, honours armed()/resetStale/alwaysReturnFromHandler
- `struct ContainerSelState { listBox, lastSelection }`
- `void MonitorContainerSelection()` — per-tick row-change announce + first-arm empty/one/N-item speech; finds its panel via `acc::engine::FindPanelByKind`
- (`ParkPickerCursorOffList`, `MonitorEquipPickerSelection`, `MonitorWorkbenchUpgradePicker` moved to `menus_listbox_picker.cpp`)
- `void PollContainerGiveModeKey()` — Win32-polls the give-mode hotkey since the engine's player-control layer eats Tab before menu dispatch
- `void acc::menus::listbox::TickListboxMonitors()` — container monitor, `TickPickerMonitors()`, give-mode poll
- L1971 — `const char* acc::menus::listbox::GetTitleOverride(void* panel)`
