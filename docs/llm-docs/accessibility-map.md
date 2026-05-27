# Accessibility Map

A discovery doc, not a reference. Each entry is one of:

- **known** — hook target identified and verified (we've called it from a hook and seen expected behavior)
- **suspected** — function name from Lane's RE looks right, but unvalidated
- **open** — we know we need this; no candidate yet

Source for "suspected" entries unless noted: `third_party/Kotor-Patch-Manager/AddressDatabases/kotor1_0_3.db`. To probe: `sqlite3 <db> "SELECT class_name, function_name FROM functions WHERE LOWER(function_name) LIKE '%KEYWORD%' ORDER BY class_name;"`. The DB also has `offsets` (struct member offsets) and `global_pointers` tables.

Two cross-cutting categories below: **player-facing surface area** (what we expose to the screen reader) and **engine internals** (where in the binary we hook to drive it). A third category — **configuration** — was added 2026-04-29; it covers our own settings, not engine state.

## Player-facing surface area

### Dialog
- suspected: `CGuiInGame::HandleDialogEntry` — fires on entering a dialog node; likely the right place to read & speak the NPC line
- suspected: `CGuiInGame::AppendToDialogBuffer` — the buffer text accumulator; mirror to TTS
- suspected: `CGuiInGame::HandleDialogReplies` — replies presented to player; enumerate for screen reader
- suspected: `CGuiInGame::HandleDialogReplyChosen` — player commits to a reply; announce confirmation
- suspected: `CGuiInGame::CloseDialog` — leaving dialog mode; emit a context-change event
- open: separating bark / floaty bubble dialog from screen dialog

### Damage and combat events
- suspected: `CSWSCombatAttackData::GetTotalDamage`, `GetBaseDamage`, `AddDamage` — per-attack damage breakdown
- suspected: `CSWCMessage::HandleServerToPlayerCombatRound` — round resolution; high-level summary trigger
- suspected: `CSWSCreature::BroadcastFloatyData` — likely the source of floaty damage numbers; intercept here to get the value before render
- open: damage type (slashing, energy, etc.) — needs deeper RE

### Object examination and tooltips
- suspected: `CSWGuiManager::DisplayToolTip`, `ChangeToolTipText` — tooltip text path
- suspected: `CSWGuiToolTipPanel::SetToolTipText` — leaf where text is set
- suspected: `CGuiInGame::ShowExamineBox` / `HideExamineBox` — detailed examine UI; capture content on show
- suspected: `CSWGuiControl::DisplayToolTip` — base GUI control tooltip; covers buttons, inventory, etc.

### Zone, area, and module
- suspected: `CClientExoApp::CreateModule` — area transition entry
- suspected: `CClientExoApp::SetLoadScreenByModuleName` — module name available here
- suspected: `CClientExoApp::ShowLoadScreen` / `HideLoadScreen` — transition boundaries
- suspected: `CClientExoApp::SetLoadScreenHint`, `GetNextLoadScreenHintSTRREF` — hint text displayed during load; speak it
- known (DLZ-Tool): `OFFSET_LOAD_DIRECTION = 0xc8` — entering vs leaving direction byte

### Inventory and equipment
- suspected: `CSWGuiInGameEquip::EquipItem`, `UnequipItem`, `ShowCantEquipMessage` — equip flow
- suspected: `CSWGuiInGameEquip::UpdateInventory` — inventory list updates
- suspected: `CSWGuiInGameInventory::CantEquip` — failed equip; speak the reason
- suspected: `CSWGuiContainer::GiveItem` — picking up from a container

### Menus and UI screens
- suspected: `CSWGuiCustomPanel::OnSelect*Button` family — character-creation panel buttons (Abilities, Feats, Skills, Portrait, Name, Play)
- suspected: `CGuiInGame::GetGuiInGameScreenPanel` — currently active panel accessor
- suspected: `CGuiInGame::GetMiniMapVisible`, `GetPartyAccessPanelUp` — UI visibility state
- open: pause / settings menus — not yet probed

### Journal and quests
- suspected: `CSWCMessage::HandleServerToPlayerJournalMessage_AddWorldStrref` — journal entry added
- suspected: `CSWCWorldJournal::AddEntryStrRef`, `DeleteEntryStrRef` — server-side journal mutation
- open: full journal read-out command

### Message buffer (in-game text feed)
- suspected: `CGuiInGame::AppendToMsgBuffer`, `GetMessageBuffer`, `GetMessageBufferSize` — running text feed (combat results, system messages)
- Strong candidate for the primary verbose-output channel

### Floaty text (world-space labels)
- suspected: `CGuiInGame::AddFloatyText`, `CSWGuiMainInterface::AddFloatyText`
- suspected: `CGuiInGame::RemoveFloatyText` — lifecycle pair

### TLK string resolution
- known: `TLK_TABLE_PTR` global at `0x7a3a08`
- suspected: `CExoLocString::GetString` family — resolves StrRef → text
- This is the canonical way to convert game text references into renderable strings

## Engine internals

### Input events (main menu / title screen)
- known: `CSWGuiMainMenu::HandleInputEvent` at `0x0067b380`, hooked mid-function at `0x0067b395`. Signature: `void __thiscall(int param_1, int param_2)`. `param_1` is an `InputIndices` value (key/button code), `param_2` is press state. Confirmed firing 2026-04-30; key codes decode against the 132-entry InputIndices enum. Hooking after the entry-time `JZ` early-out filters key-release events for free.
- known: arrows are `KEYBOARD_LEFT_ARROW=7, RIGHT=8, UP=9, DOWN=10`; full enum mirrored in `patches/Accessibility/Accessibility.cpp:InputIndexName`.

### Focus and selection
- known: `CSWGuiControl::HandleFocusChange` at `0x00418960` exists and is hookable at `0x0041896b` (mid-function) — but **does NOT fire on the main menu** (the title screen routes through its own `HandleInputEvent` exclusively, see `memory/project_main_menu_input_path.md`). Validated firing once on a non-menu control 2026-04-30; in-game / chargen validation still pending.
- suspected: `CSWGuiControl::GetIsSelectable`, `GetSelectableParent` — focus-traversal predicates
- suspected: `CSWGuiButtonToggle::SetSelected` — selection state on buttons

### Targeting
- suspected: `CClientExoApp::SetLastTarget`, `GetLastTarget` — currently-targeted in-world object
- suspected: `CGuiInGame::SetMainInterfaceTarget` — main interface target indicator
- suspected: `CClientExoAppInternal::SelectNearestObject` — keyboard-friendly target cycling
- suspected: `CClientExoAppInternal::DoPassiveSelection` — ambient target highlight

### Timers and tick points
- suspected: `CSWGuiManager::ResetToolTipTimer` — tooltip dwell timing
- suspected: `CClientExoApp::SetLoadScreenHintUpdateTimer` — load screen rotation timer
- open: main game loop tick — `CServerExoApp` has 121 functions; candidate for the per-frame hook
- open: input event entry point — needs deeper RE

### Object structure (verified twice — DB and DLZ-Tool agree)
- known: `OFFSET_GAME_OBJECT_TYPE = 0x8`
- known: `OFFSET_CSWSOBJECT_TAG = 0x18`
- known: `OFFSET_CSWSOBJECT_AREA_ID = 0x8c`
- known: `OFFSET_CSWSOBJECT_X_POS = 0x90`
- known: `GAME_OBJECT_TYPES` enum: AREA=4, CREATURE=5, ITEM=6, TRIGGER=7, PROJECTILE=8, PLACEABLE=9, DOOR=10, AREAOFEFFECT=11, WAYPOINT=12, ENCOUNTER=13, STORE=14, SOUND=16

### Globals (live-state entry points)
- known: `APP_MANAGER_PTR = 0x7a39fc` — top-level `CClientExoApp` (matches DLZ-Tool)
- known: `GUI_MANAGER_PTR = 0x7a39f4` — top-level `CSWGuiManager`
- known: `TLK_TABLE_PTR = 0x7a3a08` — string table
- known: `RULES_PTR = 0x7a3a28` — game rules
- known: `VIRTUAL_MACHINE_PTR = 0x7a3a00` — NWScript VM
- known: `SCREEN_WIDTH = 0x78d1d4`, `SCREEN_HEIGHT = 0x78d1d8`
- known: `AURORA_PTR = 0x7a39f8`, `EXO_RESOURCE_MANAGER_PTR = 0x7a39e8`

### Navigation data
- known (DLZ-Tool): `CSWSTrigger` geometry — count at `0x284`, geometry at `0x288`
- known (DLZ-Tool): `CSWSDoor` corners at `0x350`, linked-to-flags at `0x384`, linked-to-module at `0x390`
- open: pathfinding / walkmesh access — needs investigation

## Configuration (our own state, not engine state)

- open: accessibility command keybindings — likely needs its own input layer parallel to the game's
- open: verbosity setting (terse / standard / verbose)
- open: screen-reader output backend — NVDA via Tolk planned per `docs/tools.md`
- open: persistence — save with player profile, or in our own ini next to the install

## How to grow this map

When a new accessibility need surfaces:

1. Probe the DB: `sqlite3 third_party/Kotor-Patch-Manager/AddressDatabases/kotor1_0_3.db "SELECT class_name, function_name FROM functions WHERE LOWER(function_name) LIKE '%KEYWORD%' ORDER BY class_name;"`
2. Add candidates here as **suspected** with a one-line reason
3. Promote to **known** once we've hooked the function and observed the expected behavior in-game
4. If the DB has nothing, the symbol genuinely isn't named yet — that's a Ghidra investigation against Lane's `.gzf` (deferred download)

Cross-references:
- Address constants and helper structs: `third_party/DLZ-Tool/KotorAdresses.h`, `third_party/DLZ-Tool/types.h`
- File-format access (2DA, GFF, TLK): `third_party/KotOR_IO/`
- Earlier accessibility experiment by Lane: `third_party/` not cloned yet — `LaneDibello/KeyMouseAccessibilityTest`
