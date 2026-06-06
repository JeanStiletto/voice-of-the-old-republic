# KOTOR 1 Game-Flow Reference

A lifecycle map for future Claude sessions. When designing or placing a new
accessibility hook, consult this doc first for context on what phase the
engine is in and which modules already own that territory.

Format: known / suspected / open (same as `accessibility-map.md`).

---

## Top-level state machine

The engine runs one process from DLL-inject to process-exit. The phases
below are sequential in normal play; save/load can re-enter Area Load from
any in-world phase.

---

### Phase 1 — DLL inject / process attach

- Boundary signal: `DllMain(DLL_PROCESS_ATTACH)`.
- All global engine pointers are already live (`kAddrAppManagerPtr`,
  `kAddrGuiManagerPtr`, etc.) — the Steam launcher waits for the real PE
  to decrypt before injecting.
- Tolk init is deferred to first hook fire (COM + driver DLLs are unsafe
  under the loader lock).
- Active panel: none (engine constructs its own GUI before our first hook
  fires, but the main menu may not have painted yet).
- Status: known — DLL attach + Tolk lazy-init is the established pattern
  (`Accessibility.cpp::DllMain`).

---

### Phase 2 — Main menu (CSWGuiMainMenu)

- Panel kind: `PanelKind::Unknown` (title screen routes through its own
  path, not CGuiInGame). Options sub-screen is `PanelKind::MainMenuOptions`
  (vtable `0x00758838`), identified structurally after slot-table miss.
- Focus signal: `CSWGuiPanel::SetActiveControl @0x0040a630` — fires once
  per focus change, gives `param_1 = new control` (ESI in the mid-function
  hook at `+0x8`). Covers every sub-screen including Options tabs.
- Input handling: `CSWGuiMainMenu::HandleInputEvent @0x0067b380`, hooked
  at `0x0067b395` — handles letter / F-key shortcuts only; arrow nav goes
  through SetActiveControl.
- Control text: vtable downcasts — `vtable[22](ctrl)` → `AsButton` → text
  at `+0x16c`; `vtable[20](ctrl)` → `AsLabel` → text at `+0xe8`.
- Modules: `menus.cpp` (focus drain, panel monitors), `menus_credits.h`
  (credits screen).
- Status: known — verified live; German TLK labels confirm correct
  extraction ("Neues Spiel", "Optionen", etc.).

---

### Phase 3 — Character creation (chargen)

Panels (in order): portrait/class picker → abilities slider → skills →
feats (`SkillInfoBox` overlay for ShowGranted) → name editbox → play.

- Panel kinds: no dedicated CGuiInGame slot for chargen panels; they are
  heap-allocated and identified by vtable or GUI-id signature.
  `PanelKind::SkillInfoBox` is the ShowGranted overlay (vtable-identified).
  `PanelKind::PowersLevelUp` hosts both chargen and level-up force-power
  picking (same class, two contexts).
- Focus signal: same SetActiveControl hook as Phase 2.
- Listbox row nav: `CSWGuiListBox::SetActiveControl @0x0041c160` — separate
  hook, fires for row changes within a listbox (race/class/portrait picker).
- Attribute sliders: slider value labels read as button text (e.g. `"8"`).
- Name editbox: `CSWGuiEditbox` — append-only, no caret state anywhere in
  the `0x160`-byte struct. Up/Down re-reads; Left/Right unbound.
  Engine handles submit natively on Enter — never synthesise an OK-button
  activate on editbox Enter (double-activate race, see MEMORY).
- Chargen → world transient: `GetPlayerPosition` returns false during
  module load. Engine accessors that walk the PC creature's stats block
  fault in this window. Gate ALL per-tick engine getters on
  `GetPlayerPosition`. The `transitions::IsModuleLoadPending` flag covers
  the earlier SetMoveToModuleString window.
- Modules: `menus_chargen_attr.h`, `menus_chargen_feats.h`,
  `menus_chargen_skills.h`, `menus_powers_levelup.h`, `menus_editbox.h`.
- Status: known (focus extraction); suspected (full round-trip coverage of
  every chargen screen not exhaustively tested).

---

### Phase 4 — World load / area transition

Two sub-cases: initial game load and mid-game area transition.

Boundary sequence (same for both):
1. `CClientExoApp::SetMoveToModuleString` → engine sets destination
   module resref. Our detour `OnSetMoveToModuleString @0x004aecd0` fires.
   `transitions::AnnouncePreLoadDestination` speaks "Loading: <module>".
   `transitions::g_module_load_pending` latches to block unsafe probes.
2. Load screen active — `CClientExoApp::ShowLoadScreen` / `HideLoadScreen`
   (suspected, not hooked). Hint text: `SetLoadScreenHint` /
   `GetNextLoadScreenHintSTRREF` (suspected).
3. Area pointer in `engine_player::GetPlayerArea` changes — first fresh
   area pointer clears `g_module_load_pending` in `transitions::Tick`.
4. `transitions::Tick` observes the area delta and speaks the area
   display name (`engine_area::GetAreaDisplayName` via
   `CSWSArea.name CExoLocString @+0x150`).
5. Player anchor: `GetPlayerPosition` returns true → per-tick probes re-
   enable.

- Panel during load: `PanelKind::AreaTransition` (the panel that captures
  input and shows the loading bar). `IsForegroundUiBlocking` returns true
  during this phase.
- Engine surfaces used:
  - `engine_player::GetPlayerArea` → opaque `CSWSArea*`.
  - `engine_area::GetAreaDisplayName`.
  - `transitions::IsModuleLoadPending` — short-circuit gate for all callers
    that reach into player/leader/area structs.
- Walkmesh rebuild: `spatial_change_detector::Tick` detects area-pointer
  delta and calls `engine_area::BuildAreaWallCache` for the new area.
  `wall_topology` recomputes perceptual-region clusters from the wall
  cache.
- Landmark cache: `transitions::FindLandmarkNear` rebuilds per area change
  by scanning `CSWSWaypoint` objects with `map_note_enabled`.
- Status: known (area-change announce verified live); suspected
  (ShowLoadScreen / hint-text surfaces not yet hooked).

---

### Phase 5 — In-world play (per-tick loop)

The main gameplay loop. The per-tick hook is the spine of the entire mod.

- Hook: `CSWGuiManager::Update detour @0x40ce76`. Fires every frame,
  post-input. Entrypoint: `OnUpdate` (in `core_tick.cpp`) → `acc::tick::Dispatch`.
- Dispatch order (load-bearing — see `core_tick.cpp`):
  1. `hotkeys::BeginTick` — snapshot rising-edge state.
  2. `menus::ValidatePanels` / `TickMonitors` / `PollHomeEndKeys`.
  3. `cycle_input::PollWin32` / `announce_degrees::PollWin32` — engine
     drops unbound scancodes; we poll Win32 directly.
  4. Probe subsystems (mouselook, pathfind, audio frame, camera state,
     view mode, etc.).
  5. Autowalk watchdog + beacon driver.
  6. `engine::TickPlayerInputRestore` — auto-restore `SetPlayerInputEnabled`
     after guidance disables.
  7. `passive_narrate::Tick` — drain deferred Q/E re-announce.
  8. `party_leader_announce::Tick`.
  9. Order-load-bearing block: `camera_announce` → `camera_orient` →
     `spatial::change_detector` → `swoop_race` → `transitions` → `view_mode`.
  10. `map_ui_cursor::Tick`.
  11. `audio::footstep_suppress::Tick`.
  12. `radial_menu::Tick`.
  13. Combat block: mode, log, attack resolutions, saves, leader announce,
      examine panel, queue, examine view, specials.
  14. `dialog_speech::Tick`.
  15. `interact::PollHotkey`.
  16. `engine::TickInputClassReassert` (MessageBoxModal close cleanup).
  17. `menus::modsettings::Tick`.
  18. `menus::TickPendingOps` — drain deferred queued actions LAST.
  19. `hotkeys::EndTick`.
- Active panel during play: `PanelKind::MainInterface` (HUD always present)
  + whatever sub-screen is open.
- Player chain: `CClientExoApp::GetPlayerCreature @0x5ED540` →
  `CSWCCreature*` → `server_object @+0xf8` → `CSWSObject*` →
  position `@+0x90`, orientation `@+0x9c`.
- Leader (Tab): `engine_player::GetClientLeader` — may differ from PC.
  `GetActiveLeaderName` follows a three-path chain; MUST be gated on
  `GetPlayerPosition`.
- Target signal: `CClientExoAppInternal::ShowObject @0x005f9c60` (hook
  cut `@0x005f9c8e`). `passive_narrate::OnEngineShowObject` fires on every
  engine-driven target change. LastTarget is also written by combat AI and
  is not a clean signal for ambient narration.
- Status: known — full tick running live.

---

### Phase 6 — Dialog

Two topologies:

**Interactive dialog (NPC-driven conversation)**
- Panels: `PanelKind::DialogCinematic`, `PanelKind::DialogCinematicCopy`,
  `PanelKind::DialogComputer`, `PanelKind::DialogComputerCamera`.
  Auxiliary routing panels: `PanelKind::DialogMessagesAux` (`CGuiInGame @+0xf8`),
  `PanelKind::DialogMessages` (`@+0xfc`).
  `engine_panels::HasActiveDialogPanel` scans panels[] (not foreground —
  the engine swaps in a Fade overlay during reply turns).
- NPC line: `CSWGuiDialog.message_label @+0x1ca4`. `dialog_speech::Tick`
  edge-speaks on change.
- Replies: `CSWGuiDialog.replies_listbox @+0x19c4`. On 0→N grow, speaks
  "N replies available". Per-row nav via `ListBoxPanelSpec` in
  `menus_listbox.cpp`; inactive rows get `(unavailable)` suffix via
  `is_active @+0x4c`.
- Engine-side hook candidates (suspected, not yet wired):
  `SetDialogMessage @0x6a7010`, `SetReplies @0x6a86a0`,
  `SetBark @0x6a9920`. Current implementation is poll-based; switching is
  a one-line wiring change.
- `IsForegroundUiBlocking` returns true when any `CSWGuiDialog*` is in
  `panels[]` — gates in-world ambient narration.

**Bark bubble**
- Panel: `PanelKind::BarkBubble`.
- Text: `CSWGuiBarkBubble` text field. `dialog_speech::Tick` edge-speaks
  on change.

**Cinematic / end transitions**
- Dialog exit: `CloseDialog` (suspected; `accessibility-map.md`) — context-
  change event.
- open: separating bark / floaty text from screen dialog is not yet
  implemented.

- Status: known (interactive dialog poll working, replies navigable);
  suspected (hook-based path exists but poll used); open (bark vs. floaty
  text separation).

---

### Phase 7 — Combat

Entry/exit detection: `CClientExoApp::GetCombatMode @0x005EDE70` polled
each tick in `combat::TickCombatMode`. Stability-debounce pattern (same as
`camera_announce.cpp`) prevents mode oscillation from misfiring. Speaks
"Kampf beginnt" / "Kampf beendet".

**Combat log (live)**
- Hook: `CGuiInGame::AppendToMsgBuffer @0x0062b5c0`. Every engine combat-
  feedback string flows here — attack lines, damage breakdowns, XP, loot.
  Ring buffer at `CGuiInGame @+0xF8`, write index `@+0x100` (64 slots,
  16-byte stride). Handler in `combat::OnAppendToMsgBuffer` pushes
  `CExoString*` argument straight to TTS.
- `CSWGuiInGameMessages.messages_listbox` fills lazily only when the review
  panel mounts — not a live source. The listbox poll in `TickCombatLog`
  is a log-only sanity check, not the speech path.

**Attack resolution**
- Poll: player creature's `combat_round.attacks_list[7]`, edge-detected
  on `(target, result, baseDamage)` tuple. Speech currently OFF (the
  AppendToMsgBuffer hook delivers richer text). Log entry kept as a
  per-attack cross-check.
- Player creature chain: `GetPlayerServerCreature` → `CSWSCreature`.

**Saving throws**
- Skeleton / coarse field-diff heuristic. Real path needs hook on
  `SavingThrowRoll @0x5b92b0` or `BroadcastSavingThrowData @0x4ec760`.
  Status: open.

**Examine view (Ö)**
- Synthetic in-DLL listbox (`examine_view.h`). Rows rebuild per step for
  live HP / distance. Surfaces: `CSWSObject::GetDamageLevel @0x4cb020`,
  `CSWSCreature::GetMaxHitPoints @0x4ed310`, `effects @+0x124`, inventory
  handles via `CSWInventory.right_weapon @+0x14` + `left_weapon @+0x18`.
- `OnControlEntered` gate: `is_active @+0x4c` must be forced to 1 around
  the call for equipment-row rows that stay at 0 in mouse-driven play.

**Action queue submenu (Shift+H)**
- `combat_queue.h`. Walks `combat_round.actions` linked list; filters
  `type=0xFF` placeholder. Action-type byte enum mapping disabled
  (inferred values were wrong); all entries render as "Aktion" until
  probed.

**Specials heartbeat**
- `combat_special_watch.h`. Monitors special-move state fields.

**Victory / defeat**
- open: no hook candidate identified yet for game-over / area-respawn
  transitions following PC death.

---

### Phase 8 — Pause and MessageBoxModal

**Engine pause bits**
- Bit 2 (`SetPauseState(server, 2, 1)`) = menu-pause. Set by the engine
  on sub-screen open; cleared on `HideSWInGameGui` close. The
  MessageBoxModal close path (Alt+F4 quit-confirm, save-overwrite) skips
  this cleanup — `engine_subscreen::TickInputClassReassert` dispatches the
  missing `SetPauseState(2,0)` + `SetSoundMode(0)` on
  `modal_stack` non-zero → 0 edge.
- `CServerExoApp::SetPauseState` (server call, not UI flag).
- `CExoSoundInternal::SetSoundMode(ExoSound, 0)` un-mutes mixer; the
  server SetPauseState message does not reach this.
- DO NOT use Toggle/XOR — alternates instead of setting. Dispatch explicit
  0 / 1.

**MessageBoxModal**
- Panel kind: `PanelKind::MessageBoxModal`. Lives in `modal_stack`, not
  `panels[]`. `TickInputClassReassert` triggers on modal-stack → 0 edge.
- Controls: typically OK + Abbrechen. Focus via SetActiveControl hook.

**InGamePause**
- Panel kind: `PanelKind::InGamePause`. Lives in `panels[]`; modal_stack
  stays 0. `HideSWInGameGui` handles close natively.

---

### Phase 9 — In-game menu (HUD sub-screens)

The HUD strip (`PanelKind::InGameMenu`) stays in the foreground while a
sub-screen is drilled into. `engine_panels::HasActiveSubScreen` scans
`panels[]` (stale Fade overlays can block a whitelist; the blacklist approach
is used instead).

Sub-screen kinds and their modules:
- `PanelKind::InGameEquip` — `menus_equipstats.h`
- `PanelKind::InGameInventory` — `menus_listbox.h` (ListBoxPanelSpec)
- `PanelKind::InGameCharacter` — `menus_charsheet.h`
- `PanelKind::InGameAbilities` — `menus_powers_levelup.h`
- `PanelKind::InGameMessages` — `combat.h` / `menus_listbox.h` (review
  panel for the ring-buffer log)
- `PanelKind::InGameJournal` — `menus_journal.h`
- `PanelKind::InGameMap` — `map_ui_cursor.h`, `map_user_markers.h`
- `PanelKind::InGameOptions` — `engine_options.h` + `engine_panels::
  IsInGameOptionsSubScreen`. Vanilla Esc path on this panel triggers a
  stack-cookie smash; our hook routes through `QueueActivate(Schliess.)`.
- `PanelKind::InGameGalaxyMap` — galactic travel picker.

Opening: `CGuiInGame::SwitchToSWInGameGui @0x62cf10` (detour in
`engine_subscreen::OnSwitchToSWInGameGui`). If a sub-screen is already
open the detour calls `PrevSWInGameGui` first to avoid stale-panel
accumulation.

Closing: `CGuiInGame::PrevSWInGameGui @0x0062cdf0` (clean "back to
strip") or `CGuiInGame::HideSWInGameGui @0x0062cba0` (engine's "close
current sub-screen", used internally by Esc on save/load).

Drill mode: `menus::g_drilledIntoSubScreen` retargets arrow nav from the
strip to the sub-screen. Auto-armed by `TickMonitors` on any newly-
detected sub-screen.

Status: known (strip + drill nav working, Options Esc fix working);
suspected (InGameGalaxyMap extraction not implemented).

---

### Phase 10 — Save / Load

- Panel kind: `PanelKind::SaveLoad` (heap-allocated, no CGuiInGame slot).
  Identified structurally by vtable / `.gui`-id signature. ID-11 type
  disambiguates from `PanelKind::WorkbenchUpgrade` (Button = SaveLoad's
  BTN_DELETE, Label = workbench).
- `menus_listbox.h` ListBoxPanelSpec routes SaveLoad listboxes.
- Status: known (panel identified, listbox nav functional).

---

### Phase 11 — Level-up

- Panel kind: `PanelKind::InGameLevelUp` (CSWGuiLevelUpPanel,
  vtable `0x00759568`). Heap-allocated, no CGuiInGame slot.
- Step buttons gate on `is_active != 0` and silently drop disabled
  clicks — the extract path emits a disabled-state suffix.
- `PanelKind::PowersLevelUp` hosts force-power picking for both level-up
  and chargen flows.
- Accepting level-up: `Annehmen` button destroys the panel + button
  synchronously during `FireActivate`'s `vtable[15]` dispatch. Any
  `FromControl` that walks freed vtable after this triggers a GS cookie
  smash. Never access the button reference after firing activate on it.
- Status: known (panel identified, nav working); known issue (Annehmen
  stale-pointer crash design constraint documented).

---

### Phase 12 — Workbench (upgrade)

Three panels: `PanelKind::WorkbenchSelect` (category picker),
`PanelKind::WorkbenchItems` (item picker), `PanelKind::WorkbenchUpgrade`
(slot detail, 7 slot buttons at IDs 12..18). Engine surfaces:
`OnEnterSlot` / `OnSlotSelected` / `OnUpgradeSelected` / `OnAssemble`
(all in `engine_panels.h` docs; addresses in `patches/Accessibility/`
workbench-related headers).
- Status: known (panel identification and slot-pick nav tested).

---

### Phase 13 — Party selection

- Panel kind: `PanelKind::PartySelection` (vtable `0x756BB8`).
- Portrait layout: `+0x44c` partyId, `+0x450` NPC slot. `+0x448` flag
  word is unreliable (heap garbage on uninitialised slots) — gate on
  `PartyTableIsNPCAvailable` instead.
- `engine_player.h::GetServerPartyTable` + `PartyTableIsNPCAvailable /
  IsNPCSelectable`.
- Status: known.

---

### Phase 14 — Store

- Panel kind: `PanelKind::Store`. Module: `menus_store.h`.
- Status: suspected (panel identified; full buy/sell flow not yet
  exhaustively tested).

---

### Phase 15 — Swoop race minigame

- Detection: `CSWCArea.mini_game @+0x264` — null → non-null at module-load
  construction. `swoop_race::Tick` polls once per tick.
- Speaks entry opener + keybind cheat sheet on transition. Exit cue on
  inverse.
- `swoop_race::IsActive` for gating other subsystems.
- Status: known (entry/exit cues working); suspected (per-obstacle cues
  need obstacle-array offset confirmed).

---

### Phase 16 — Game-over / death

- open: no hook candidate identified. The engine presumably transitions
  back to an area load or shows a "return to main menu" modal.

---

## Cross-cutting concerns

### Focus signal — ShowObject vs LastTarget

- Use `ShowObject @0x005f9c60` (hook cut `@0x005f9c8e`), not
  `GetLastTarget` / `SetLastTarget`.
- Reason: `last_target` is overwritten by the combat AI
  (`CSWSCreature::CreateNewAttackActions`) on every queued creature every
  round. Polling it races combat-AI writes and produces spurious narration.
  `ShowObject` is only called by user-driven paths (DoPassiveSelection's
  mouse-hover auto-target, SelectNearestObject's Q/E cycle).
- Handler: `passive_narrate::OnEngineShowObject`.
- Q/E deferred re-announce: `passive_narrate::RequestQEReannounce` arms a
  one-tick deferred fire; if ShowObject fires in the same tick, it cancels
  the request.

### Foreground panel routing

- `CSWGuiManager.modal_stack @+0x94` (data ptr `@+0x94`, size `@+0x98`).
- `CSWGuiManager.panels @+0x88` (data ptr `@+0x88`, size `@+0x8c`).
- `engine_manager::GetForegroundPanel` = modal_stack top if non-empty,
  else last panels[] entry.
- `engine_panels::IsForegroundUiBlocking` uses a blacklist: known-blocking
  kinds + any CSWGuiDialog* in panels[]. Whitelist underblocks because
  stale Fade overlays linger in panels[] for seconds after close.
- Sub-screens hide under the InGameMenu strip (strip stays fg via
  SwitchToSWInGameGui's `keep_strip=true`). `HasActiveSubScreen` scans
  panels[], not foreground.

### Engine pause bits

- Bit 2 = menu-pause (`SetPauseState(server, 2, N)`).
- `HideSWInGameGui` handles normal sub-screen close including pause clear.
- MessageBoxModal close via Alt+F4-style path skips it; we patch with
  `TickInputClassReassert`.
- Auto-pause (combat round start) is a separate bit; not yet surfaced by
  the mod.

### Per-tick hook

- `CSWGuiManager::Update @0x0040ce76`, mid-function detour. Post-input,
  safe for deferred cursor moves. The full dispatch order is canonical in
  `core_tick.cpp`.
- All subsystem Tick() functions must be cheap and idle when their
  condition is false. No spin or polling loops inside Tick.

### Speech routing

- `prism.h`: primary channel. Two backends — NVDA via Tolk (direct
  screen-reader path), and SAPI backend (`SpeakUrgent(text, voiceId)`)
  for urgent cues that bypass NVDA's typed-char cancellation.
  Voice 1 reserved for compass/turn cues. Degrades silently if export
  missing.
- `audio_bus.h`, `audio_cue_player.h`: engine-side 3D one-shot audio
  (`Play3DOneShotSound`). Volume scalar > 1.0 amplifies cleanly. Silent
  no-op on missing resref — verify resref via
  `build/sounds-extracted-full/`.
- `strings.h`: `Get(Id)` — all user-facing strings localised here.
  Logs stay English; speech uses localised text. Never hardcode speech
  literals in handlers.
- `msg_router.h`: routes speech between Prism / audio / TTS depending on
  priority and context.

### Object handle namespaces

- Client-side handles: high bit `0x80000000` set. Resolve via
  `engine_area::ResolveClientObjectHandle` → `CSWCObject*` →
  `+0xf8 server_object`.
- Server-side handles: high bit clear. Resolve via
  `engine_area::ResolveServerObjectHandle` → `CGameObjectArray::
  GetGameObject`.
- AI-driven primitives and party-table accessors expect server-side IDs.
  Re-derive via `GetObjectHandle(obj)` after `ResolveClientObjectHandle`.
- `GetObjectDisplayNameByHandle` retries with `handle | 0x80000000` for
  server-side handles — the engine's `GetObjectName` only works on
  client-side.

---

## Known / suspected / open — summary

### Known (verified live)
- DLL inject, Tolk lazy-init, per-tick hook.
- Main menu focus extraction (SetActiveControl + vtable downcasts).
- Options tabs in both main menu and in-game options.
- Area-change announce, room-transition via wall_topology clusters.
- PreLoadDestination announce via OnSetMoveToModuleString.
- In-world per-tick: Q/E cycle, passive_narrate, camera announce,
  spatial change detector, transitions, view mode.
- Combat mode entry/exit, AppendToMsgBuffer live log narration.
- Attack-resolution tuple poll (silent but live).
- Dialog NPC line + reply nav (poll-based).
- Bark bubble edge-speak.
- InGameMenu sub-screen drill nav (all named sub-screens except GalaxyMap).
- SaveLoad, InGameLevelUp, Workbench, PartySelection panel identification
  and nav.
- ShowObject hook (passive_narrate).
- Swoop race entry/exit.
- MessageBoxModal close cleanup (TickInputClassReassert).
- Player character name via `GetPlayerCharacterName @0x5EDAB0`.
- Party-table accessors (`GetIsNPCAvailable`, `GetNPCSelectability`).

### Suspected (candidate identified, not fully validated)
- Load screen surfaces: `ShowLoadScreen` / `HideLoadScreen` /
  `SetLoadScreenHint`.
- Dialog hook-based path: `SetDialogMessage @0x6a7010`, `SetReplies
  @0x6a86a0`, `SetBark @0x6a9920`.
- `CloseDialog` context-change event.
- Store buy/sell flow coverage (`menus_store.h`).
- InGameGalaxyMap extraction.
- Per-obstacle cues in swoop race (obstacle-array offset unconfirmed).
- Saving-throw poll (field-diff heuristic only; hook candidate is
  `SavingThrowRoll @0x5b92b0`).

### Open (need identified, no entry point yet)
- Bark vs. floaty-text separation.
- Game-over / death / area-respawn signal.
- Auto-pause (combat round) bit surfacing.
- Damage type (slashing, energy, etc.) in attack breakdown.
- Journal-entry-added event.
- Full inventory / journal / character-sheet text dump command.
- Load-screen hint narration (hint text resolution chain not probed).

---

## What this doc does not cover

- File formats: KEY/BIF, ERF/RIM, GFF, 2DA, TLK — see project `CLAUDE.md`
  and `third_party/KotOR_IO/`.
- NWScript VM internals (`kAddrVirtualMachinePtr` is in
  `accessibility-map.md`; scripting hooks are not yet used by this mod).
- Save-file binary layout.
- Walkmesh internals and wall topology algorithm — see `engine_area.h`
  comments, `wall_topology.h`, and the archived investigation docs under
  `archiev/archived docs 27.05.2026/`.
- SARIF / DB query recipes — see `docs/llm-docs/sarif-cookbook.md` and
  `accessibility-map.md::How to grow this map`.
- Installer and distribution pipeline — see `docs/installer.md`.
