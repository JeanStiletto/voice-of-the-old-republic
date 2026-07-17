# In-game menu screens (RE reference)

Per-screen engine surfaces: workbench, map, options sub-screens, save/load, party select, galaxy map, abilities, charsheet, placeables, level-up.

> Migrated from the agent memory store on 2026-06-14 (memory-system cleanup).
> Each section below is one former memory note, preserved verbatim. Verify
> addresses/offsets against current code before relying.

## workbench_panel_layout
_upgrade.gui / upgradeitems.gui / upgradesel.gui control-ID map; ID 11 vtable disambiguates upgrade.gui from saveload.gui_

The workbench opens 3 heap-allocated panels in sequence: upgradesel.gui (category picker) → upgradeitems.gui (item picker) → upgrade.gui (slot detail). None has a CGuiInGame slot; all need structural detection.

**upgrade.gui** (29 controls) — slot detail panel:
- ID 0 LB_ITEMS (ListBox) — installable mods from inventory
- IDs 5-11 LBL_UPGRADE31..44 (LabelHilight) — slot icon labels
- IDs 12-18 BTN_UPGRADE31/32/33/41/42/43/44 (Button) — slot buttons, empty inline text
- ID 20 LBL_SLOTNAME (Label) — dynamic; shows currently-selected slot's category word ("Vibrationszelle")
- ID 22 LBL_UPGRADES (strref 42026 "Upgrades:")
- ID 24 BTN_ASSEMBLE (strref 42021 "Zusammenbauen")
- ID 27 LB_DESC, ID 28 BTN_BACK (strref 1581 "Abbrechen")

**upgradeitems.gui** (5 controls):
- ID 0 LB_ITEMS, ID 2 LB_DESCRIPTION, ID 3 LBL_TITLE
- ID 4 BTN_UPGRADEITEM (strref 42294 "Aufwerten")
- ID 5 BTN_BACK (strref 1582 "Schliess.")

**upgradesel.gui** (11 controls): four category Button + ProtoItem pairs at IDs 0..7, LBL_TITLE@8, BTN_UPGRADEITEMS@9, BTN_BACK@10.

**Why:** PanelKind classifies these via structural detectors in engine_panels.cpp. Crucial: SaveLoad's signature was {ID 0 ListBox, IDs 11/12/14 Button} — upgrade.gui matched it by coincidence (Esc→ID 12 = slot button instead of BTN_BACK, breaking the panel). Fixed by requiring saveload's ID 11 to be a Button; upgrade.gui's ID 11 is LabelHilight, so the false-match dies.

**How to apply:** When adding workbench features, route Esc/Enter via the explicit ID constants above — the generic FindCancelButton picks the first-by-ID button which is BTN_UPGRADE31 (id 12), not BTN_BACK. Slot buttons (IDs 12-18) have empty text — labels are synthesised by menus_extract.cpp section 9b3 ("Aufwertungssteckplatz N" / "Kristall-Steckplatz N"). LBL_SLOTNAME at ID 20 is dynamic but only reflects the *focused* slot, so it doesn't help with announcing the other six slot buttons.

---

## workbench_engine_surfaces
_Four engine functions for the workbench upgrade.gui panel — same shape as the equip-screen pair. Call directly to bypass z-order labels covering slot buttons._

Same z-order trap as the equip-screen: upgrade.gui's labels at .gui IDs 5..11 (LBL_UPGRADE3X/4X) cover the seven slot buttons at IDs 12..18 in hit-test, so `MoveMouseToPosition + ManagerLMouseDown/Up` resolves `mouseOver` to a label and never reaches the slot button. `vtable[15]` (HandleInputEvent 0x27) on a slot button is the keyboard-shortcut path and does NOT populate LB_ITEMS — verified empty for both available and disabled slots in `patch-20260525-141557.log` and `-142247.log`.

The fix is to bypass click-sim and call the engine's slot-pick chain directly, mirroring the equip-screen's `OnEnterSlot+OnSelectSlot` pattern. RE'd from Lane's gzf 2026-05-25:

**CSWGuiUpgrade::OnEnterSlot @0x006c3c30** — `(panel, slot_btn)`. Hover path: updates LBL_SLOTNAME / upgrade_count / property labels for the slot. Gates on `slot_btn->is_active != 0` (+0x4c — same offset as equip slot gate). Does NOT populate LB_ITEMS.

**CSWGuiUpgrade::OnSlotSelected @0x006c6500** — `(panel, slot_btn)`. THIS is the populate path: builds compatible-mods list from CSWPartyTable items + `upcrystals_2da` (for sabers) or `upgrades_2da` (for everything else), AddControls-replaces LB_ITEMS, calls `ShowItems(panel, 1)` to flip the item-pick zone visible, SetActiveControl(items_listbox), stores slot button at `panel.field74_0x2fb0`. Same is_active gate as OnEnterSlot.

**CSWGuiUpgrade::OnUpgradeSelected @0x006c5510** — `(panel, item_entry)`. Row-stage: called when picking a mod in LB_ITEMS. Stages the selection on the panel; install happens in OnAssemble. Gates on `item_entry->is_active != 0`.

**CSWGuiUpgrade::OnAssemble @0x006c6190** — `(panel, btn_assemble)`. Commit: plays gui sound, calls `FinishUpgrading` on the parent `CSWGuiUpgradeItemSelect` (field75_0x2fb4), then `PopModalPanel` — so the upgrade.gui panel closes synchronously. Gates on `btn_assemble->is_active != 0`.

All four have signature `void __thiscall (CSWGuiUpgrade*, CSWGuiControl*)`. Ghidra mis-types OnSlotSelected's param_1 as `CSWGuiControl` (by-value) but the asm confirms it's a pointer — `mov ebx, [ebp+0x8]; mov eax, [ebx+0x4c]` reads is_active from the dereferenced pointer.

**Direct-callers count is 0 for OnSlotSelected and OnUpgradeSelected** — both are registered as gui_object callbacks on the slot buttons / item entries (likely in `CSWGuiUpgrade::CSWGuiUpgrade @006c6b60` constructor or `OnPanelAdded @006c4d70`). The mouse-driven path reaches them via HandleLMouseUp on the button; we bypass via direct call.

**Why:** vtable[15] activate on a workbench slot button doesn't populate LB_ITEMS (verified). The equip-screen has the same issue and sidesteps it with direct engine dispatch — same fix applies here, just different addresses.

**How to apply:** When wiring a workbench-related action via the chain handler, queue a deferred op that raises `control->is_active = 1` then calls the appropriate engine function pair. See `menus_pending.cpp` `Kind::WorkbenchSlotSelect` and `Kind::WorkbenchUpgradeCommit` for the canonical shape.

---

## workbench_slot_occupancy
_CSWGuiUpgrade.field35_0x2f74[custom_value] is the installed-mod CSWSItem* per slot (occupied/empty + name + property text); how OnEnterSlot/OnSlotSelected/OnPanelAdded use it._


The workbench upgrade panel (CSWGuiUpgrade, PanelKind WorkbenchUpgrade, upgrade.gui IDs 12..18 slot buttons) stores **one installed-mod pointer per slot** in `field35_0x2f74` — an array of up to 4 `CSWSItem*` indexed by the **slot button's custom_value** (offset 0x58 on the control). Non-null = slot occupied; null = empty. Constant: `kUpgradeSlotInstalledItemsOff = 0x2f74` in engine_offsets.h.

Decompile-verified (Lane's gzf, 2026-06-05):
- **OnPanelAdded @0x006c4d70** fills `field35[slot]` on panel open: for each upgrades_2da row matching the slot type, if the base item (`field27_0x2f54`) carries that upgrade (bitmask test `*(uint*)(field27+0x294) & (1<<key)`), it `operator_new(0x29c)` + `LoadFromTemplate`s a CSWSItem into `field35[slot]`. So occupancy reflects mods already on the item, not just session edits.
- **OnSlotSelected @0x006c6500** install/remove branch keys off `(&field35)[custom_value] == 0` → install vs remove. Authoritative occupancy field.
- **OnEnterSlot @0x006c3c30** (hover/description handler): saber branch (category==1) reads `field35[cv]` for the crystal name (+0x280 CExoLocString); non-saber sets property_label from `GetKeyedPropertyString(field27, field71[cv])` and upgrade_count from a tag-search of party items. Gates on `slot_btn->is_active != 0`.

The installed-mod CSWSItem* is a real (engine-constructed, id=0x7f000000, not registered) item — safe to pass directly to `ReadItemPropertyDescription` (CSWSItem::GetPropertyDescription @ works on the pointer) and to `ExtractTextOrStrRef(item, 0x280, 0x284)` for its localized name.

**Category↔slot quirk:** for category 4 (2-slot armor) OnPanelAdded starts its slot loop at index 1 (local_70=1), so field35[0] is never filled and the cv=0 button (id 12) is a phantom slot that always reads empty — harmless. category 1=saber(4 slots, ids 15-18), 3=3-slot weapon(ids 12-14), 4=2-slot. The slot-type strref table is at `kAddrUpgradeSlotTypeTable` indexed `(cv-4)+category*4`.

**Helper:** `acc::engine::GetWorkbenchSlotInstalledItem(panel, slotControl)` (engine_reads) returns field35[custom_value] or null. Used by menus_extract 9b3 (append " , <modname>"/", leer" occupancy state to slot label via FmtEquipSlot* templates) and peek_description HandleWorkbenchSlotTooltip (Shift+arrow reads installed mod's property description, or WorkbenchSlotPeekEmpty). See [[project_workbench_engine_surfaces]], [[project_workbench_panel_layout]].

---

## map_cycle_architecture
_Map cycle exposes a single Landmark/Map-hint category. CycleContext World/Map routing via HasActiveMapPanel. Map listing merges waypoints + user-placed pins; engine quest pins filtered out._

Pillar 4 cycle has two state singletons: `acc::cycle::GetState(World)` and `acc::cycle::GetState(Map)`. Routing decided at the top of `cycle_input::PollWin32` + `TryHandleEvent` via `acc::engine::HasActiveMapPanel()` — true when `PanelKind::InGameMap` sits anywhere in `panels[]` (sub-screen hides under the InGameMenu strip; foreground-only check misses it).

**Map context exposes only Landmark / "Map hint"** as of 2026-05-26. `acc::filter::IsMapCycleable(c)` returns true only for `Landmark`; Door / NPC / Container / Item / Transition are world-only.

**Listing is heterogeneous in Map+Landmark** — `BuildCategoryListing` iterates two sources:
1. CSWSWaypoint entries with `IsMapNoteEnabled` + fog-explored (`isPin[i] = false`).
2. `CSWCArea.map_pins[]` entries with `IsMapPinEnabled` + high-bit user-flag (`flags & 0x80000000`) — user-placed pins from Shift+N. No fog gate (player dropped them themselves). Engine quest pins (high-bit clear) are filtered out — they're noisy / spoiler-laden, dropped per user directive 2026-05-26.

`CategoryListing.isPin[kMaxObjects]` parallel array discriminates entry shape. SortByDistanceAscending swaps it alongside obj/pos/distance. Consumers branch on `isPin[i]`:
- `cycle_input::AnnounceCurrent` chooses `GetMapPinNoteText` vs `GetWaypointMapNote` vs `GetObjectName`.
- Narrated-target stamp shape: `StampMapPin(obj, pos)` for pins (frozen pos since pins don't resolve via ResolveServerObjectHandle) vs `Stamp(obj, handle)` for game objects.

**Map-context name resolution** prefers `GetWaypointMapNote` (+0x230, CExoLocString with strref fallback) over `GetObjectName` (waypoint LocName at +0x238 — usually empty in stock K1, tag-fallback produces the "just numbers" announce). User pins read `GetMapPinNoteText` (+0x100) — auto-named in `map_user_markers::BuildAutoName`.

**Fog gate**: each waypoint position is tested via `acc::engine::IsWorldPointExplored(areaMap, pos)`. AreaMap resolved via `acc::engine::GetAreaMap()` (AppManager → CServerExoApp → GetModule → CSWSModule.area_map +0x218).

**Cursor hover** parallels the cycle's filter — `FindNearestUserMapPin` skips engine pins (high-bit clear) so the cursor only surfaces waypoints + user pins. `pending_note_is_pin` / `last_spoken_is_pin` flags discriminate at speak time.

**Activation handlers** branch on `slot.isMapPin`:
- Enter / Shift+Enter → speak `MapPinInteractHint`, no DispatchInteract (pins aren't interactable).
- Shift+- (autowalk) → speak `MapPinShiftDashHint` + landmark cue, no UseObject.
- Ctrl+- (beacon) → works directly from frozen `slot.pos`.
- Alt+- (force walk) → speak `MapPinAltDashUnsupported`.
- `-` (repeat) → re-speak pin name + clock + distance from frozen pos.

Cycle's `AnnounceCurrent` in map context calls `acc::map_ui_cursor::PanToWorld(world, suppressWaypoint)` so the cursor follows cycle focus. SuppressWaypoint is the focused obj iff `!isPin && category==Landmark` — pins don't latch the cursor's waypoint-suppress (would mis-target an unrelated waypoint pointer).

**Why**: cycle infrastructure was already polished (sort by distance, clock-frame, narrated_target stamping, Shift+- autowalk, Ctrl+- beacon). Folding pins into the same listing via a parallel `isPin[]` array is vastly cheaper than a separate cycle category. User-pin discoverability (Shift+N drops + cycle hears) is preserved while engine quest pins are filtered out of every surface (cycle, cursor, narrated_target).

**How to apply**: when adding new map-rendered surfaces, reuse the same shape — add to `IsMapCycleable`, add a `BuildCategoryListing` branch if iteration source differs. Heterogeneous entries (non-CSWSObject shapes) ride `isPin` discriminator + `narrated_target::StampMapPin`-style frozen-pos slot. Don't fork into a parallel map_ui_cycle TU.

---

## fog_of_war_exploration_model
_CSWSAreaMap explored-grid mechanics decompiled 2026-07-17; answers "how far away does a sighted player discover a map note"_

**Reveal loop**: `CServerExoAppInternal::UpdateMapData @0x004b4e80` runs per server tick; for every party member it calls `CSWSAreaMap::SetWorldPointExplored(pos, 1) @0x005792d0`. The radius argument is **1 fog-grid cell** — the function marks an L1 diamond (own cell + 4 orthogonal neighbours) via `SetMapPointExplored @0x00579000` (bitset write, one bit per grid cell). The only other caller is `ExecuteCommandRevealMap @0x00547240` (NWScript `RevealMap`, scripted full/partial reveals). `SetEntireMapExplored @0x00578fa0` exists for the reveal-all path.

**Grid geometry** (`CSWSAreaMap::Initialize @0x00578c60`): map-pixel space is fixed 440x256. Grid resolution comes from the .are `Map` struct: grid X count = `MapResX` (clamped to 88), grid Y count = `ftol(MapResX * 0.58181816)` (= 256/440, truncated). World-units-per-map-pixel (`+0x18` / `+0x1c`, same fields map_ui_cursor inverts) = rotated world-point span / map-pixel span from `WorldPt1/2` + `MapPt1/2` (MapPt normalized, x440 / x256). So **one fog cell in metres = (440/MapResX) * worldUnitsPerPxX by (256/MapResY) * worldUnitsPerPxY** — all readable at runtime from the live CSWSAreaMap (`+0x8` MapResX, `+0xc` MapResY, `+0x18`/`+0x1c` world-per-px).

**Consequence — discovery range scales with map size.** A map note becomes visible (and cycle-able via GetNext/PrevMapNote) the moment its cell is explored, i.e. when a party member's cell is within L1 distance 1 of the note's cell — roughly 1 to 2 cell-widths in world metres depending on in-cell positions. Measured cell sizes (from .are Map structs, computed 2026-07-17):
- danm14aa Dantooine plains: 26.7 x 32.1 m
- tat_m18aa Dune Sea: 44.1 x 47.5 m
- unk_m41aa Unknown World beach: 18.6 x 21.0 m
- kas_m25aa Shadowlands: 30.0 x 34.0 m
- danm13 Jedi Enclave: 7.8 x 7.8 m
- tar_m02aa Taris apartment: 5.7 x 6.4 m

So vanilla "sighted discovery" ranges from ~6 m (small interiors) to ~45-90 m (Dune Sea), while `transitions.cpp kLandmarkEnterRangeM` is a flat 8 m — the mismatch reported on open maps 2026-07-17.

**How to apply**: for parity with sighted discovery, derive the landmark proximity/discovery range per-area from the live fog cell size (e.g. `k * max(cellX, cellY)`, floored at the current 8 m) instead of a global constant.

## csw_mappin_layout
_Quest-marker pin struct offsets confirmed via Ghidra decomp 2026-05-18; CSWCArea.map_pins is pointer-array despite Lane's singular-pointer typing_

CSWCMapPin (size 0x110) field offsets confirmed via Ghidra-headless decomp:

- **+0x24** — position (Vector, inherited from CGameObject base at +0x00)
- **+0xfc** — `enabled` (int 0/1). Confirmed: HandleServerToPlayerMapPinEnabled @0x652d00 writes a BOOL here.
- **+0x100** — `note_text` (CExoString, 8 bytes char* + uint32 length). Confirmed: HandleServerToPlayerMapPinReferenceNumber @0x652d60 writes the wire packet's note here via `CExoString::operator=(&pin->field2_0x100, packetNote)`.
- **+0x104** — strref slot (CExoLocString tail; ExtractTextOrStrRef handles both inline + strref paths).
- **+0x108** — `flags` (ulong from wire packet)
- **+0x10c** — `subtype` (ulong; server-sent pins get =1)

GetMapPin(area, key1, key2) keys pins by `(flags, subtype)` — not (objectId, 0) as the wire-packet packet shape suggested.

**CSWCArea.map_pins is `CSWCMapPin**` (pointer-array), NOT inline-struct array.** Lane's PlaceHolder struct types it as singular `CSWCMapPin*` but assembly proves pointer-array:

- AddMapPin @0x606d90: `(&(this->map_pins->object).game_object.vtable)[count] = (GameObjectMethods*)param_1` — treats the address as an array of pointers and writes the new pin pointer at `[count]`.
- GetMapPin @0x605ac0: `pCVar2 = &pCVar2->object.game_object.id` — increments by 4 bytes (the `id` offset in CGameObject), confirming pointer stride.

CSWCArea offsets:
- **+0x1c4** — `map_pins` (CSWCMapPin**)
- **+0x1c8** — `map_pins_count` (int)
- **+0x1cc** — `map_pins_capacity` (int, doubles on full)

Server→client back-pointer: `CSWSArea.client_area (+0x2d0)`. Direct chain from GetCurrentArea() (server) to CSWCArea (client).

**Why:** Map pins are the engine's quest-marker / NWScript-placed-pin surface. Lay-off 1b (Phase 6 navsystem) consumes them via the engine_area helpers GetClientArea / GetMapPinCount / GetMapPinAt / GetMapPinPosition / IsMapPinEnabled / GetMapPinNoteText. Without confirming the pointer-array shape we'd iterate at 272-byte stride (the size of one pin) and read garbage past slot 0.

**How to apply:** If a future feature needs to enumerate, modify, or filter map pins, use the engine_area helpers (which encapsulate the pointer-array indexing + SEH guards). Don't re-derive the offsets — verified via Ghidra decomp of AddMapPin / ClearAllMapPins / GetMapPin / HandleServerToPlayerMapPinEnabled / HandleServerToPlayerMapPinReferenceNumber.

---

## ingamemap_button_chain
_The 5 chain buttons on the area-map panel and their RE'd handlers; useful when adding more map-side accessibility features_

CSWGuiInGameMap (PanelKind::InGameMap) hosts 5 navigable buttons:

- `up_button` @ panel-base + 0xab0 — empty-text image-only button at (80,392). `CSWGuiInGameMap::OnUpArrowPressed @0x006927b0` dispatches `HandleInputEvent(0x31, 1)` → `CSWGuiMapHider::GetPrevMapNote @0x00693090`. Cycles to **previous explored map-note waypoint**.
- `down_button` @ panel-base + 0xc74 — empty-text image-only button at (561,392). `CSWGuiInGameMap::OnDownArrowPressed @0x006927c0` dispatches `HandleInputEvent(0x32, 1)` → `CSWGuiMapHider::GetNextMapNote @0x00692e80`. Cycles to **next explored map-note waypoint**.
- `partyselect_button` @ +0x728 — text "Gruppenauswahl" at (252,416). Fires event 0x27 → opens party selection.
- `exit_button` @ +0x8ec — text "Schliess." at (466,424). Fires event 0x28 → close map.
- `return_button` @ +0x564 — text "Reisesystem deaktiviert" at (252,431). Fires event 0x29 → OnXButton → ReturnToEbonHawk gate.

**Why:** the up/down buttons read as "control N" in the speech path because `0x0073E658` (CSWGuiButton vtable) hits the spec-read miss when text is empty. The labels resolve via `menus_extract.cpp` per-kind fallback that matches `IdentifyPanel == InGameMap` + panel-base offset.

**How to apply:** when surfacing more map-side features, prefer reading these buttons by their struct-offset rather than chain index — chain order is determined by .gui resource and could shift if a future patch reorders. For data-side iteration of map notes, walk `CSWGuiMapHider.field11_0x238` (`CExoLinkedList<ulong>` of waypoint handles); GetNext/Prev already filter to explored + has-map-note nodes.

**Verified 2026-05-12** via Ghidra headless decomp of the four addresses above + the GoG xml MEMBER offsets (`docs/llm-docs/re/k1_win_gog_swkotor.exe.xml` line 7659+).

---

## options_subscreens_support
_Title-screen Options sub-screens — PanelKind identities + spinner-flanker squash; keyboard mapping screen full read+rebind (two-level tab UI). Shipped v0.5.2._


The nine title-screen Options sub-screens (reached via Hauptmenü → Optionen) were untouched/Unknown. Worked on 2026-06-13.

**Identities added** (engine_panels.h/.cpp): one vtable→kind table `kOptionsSubScreenVtables` + `IdentifyOptionsSubScreen` detector, grouped by `IsMainMenuOptionsSubScreen(kind)`. Vtables (GoG==Steam, see [[project_ghidra_gog_steam_bytes_match]]): SoundSettings 0x007587c0, AdvancedSoundSettings 0x00758550, GraphicsSettings 0x007586f8, AdvancedGraphicsSettings 0x007584a0, AutoPauseOptions 0x00758ee0, FeedbackOptions 0x007581e8, GameSettings 0x00758e00, MouseSettings 0x007585f8, KeyboardMapping 0x00759358. Titles still come from the generic label-walk (already worked); Esc/chain already worked while Unknown.

**"control N" noise = spinner flankers.** Advanced Sound (EAX), Advanced Graphics (AA/texture/anisotropy), Game Settings (Difficulty) lay each spinner as a centred value BUTTON at x=180 flanked by empty arrow buttons at x=72/x=288 (~108px out). The value button cycles via Left/Right (FindAdjacentArrow, no dx cap); the arrows are redundant. The chain flanker-squash used dx≤80 so 108px arrows leaked → spoke "control N". Fix: widened `kSquashDxMax` to 130 in menus_chain.cpp **gated on `IsMainMenuOptionsSubScreen`** (chargen's tighter spinners keep 80px). Flankers read empty because their value sibling is a BUTTON not a label, so FindSiblingLabel (labels only) never paints them. The other 6 sub-screens were already clean.

**Keyboard mapping screen — DONE (menus_keymap.cpp, two-level tab UI like menus_abilities).** Engine model decompiled (build/re/optkeymappings-keymapbutton, doc-worthy). Row = `CSWGuiKeyMapButton` (vtable 0x007593c8): action_button at +0 (event name), `mapped_key_button` at +0x1c8 holds the bound KEY name ("W") — read it for "{action}: {key}" (offsets kKeyMapButtonMappedKeyOffset/Off in engine_offsets.h; FromControl per-kind branch in menus_extract.cpp). `unchangeable` at +0x3a4. Panel `CSWGuiInGameOptKeyMappings`: field11_0xf2c (0xf2c)=capture-active flag, filter tabs .gui id 6/7/8, Default/OK/Cancel id 2/3/4, LST_EventList id 0.
Flow: tab level = flat list [Move,Game,Mini,OK,Cancel,Default]; Enter on category → `OnFilterMove/Game/Mini` (0x6ed390/3e0/430) → **RebindChain** (MANDATORY: OnFilter frees old rows; stale g_chain → MonitorFocusedControl derefs freed → CRASH; this was the tab-Enter crash) → drill. Enter on row → `SetCaptureEvent`(0x6ed480, panel,row) arms; while field11_0xf2c==1 pass ALL keys through (rv=0); engine `Update` applies; `Tick()` detects 1→0 and re-announces. OK/Cancel/Default via QueueActivate (click-sim). Esc→undrill.
**ret-4 trap:** OnFilter*/OnAccept/OnDefault all have SARIF BYTES_PURGED=4 — call with a dummy arg (`PFN(panel,0)`) or callee pops our return addr → crash. Same as [[project_engine_action_picker]]-style thiscall traps.

---

## save_popup_teardown_uaf
_Save commit frees the SaveLoad panel synchronously; downstream handlers that cached g_currentPanel must validate panels[] membership before dereferencing_

When the user commits a save (Enter on OK in the "Enter name for saved game"
editbox), the engine pops BOTH the editbox popup AND the underlying SaveLoad
panel in a single tick — modal_stack 2→0 — and frees the SaveLoad allocation
immediately. The heap allocator typically reuses that block for combat-log
strings within the same tick.

Any handler that cached `g_currentPanel = <SaveLoad>` at the previous focus
event and dereferences it on the NEXT event (e.g. the underlying in-game-menu
tooltip listbox firing OnListBoxSetActiveControl) reads the freed bytes. With
combat-log text written into them, `panel+0x20` (CExoArrayList::data) and
`panel+0x24` (CExoArrayList::size) come back as ASCII (e.g. " 12 ", "= ba"),
and the next `data[0]` dereference takes an AV.

**Why:** No engine "save in progress" flag exists — teardown is synchronous,
not flagged. The `IsModuleLoadPending` shape used for module transitions
doesn't apply.

**How to apply:** When caching a panel pointer across ticks, validate it
against `mgr->panels[]` immediately before dereferencing the panel struct.
The shared `IsPanelLive(panel)` helper in `menus_chain.cpp` (anonymous
namespace) does this; `DetectTabsCluster`, `ValidateTabbedPanel`, and
`ValidateChainPanel` already use it. Audit any new handler that touches
`g_currentPanel` / `g_chainPanel` / `g_tabbedPanel` from a *different*
event than the one that set it.

Crash analysed 2026-05-29, dump `swkotor.exe(1).31228.dmp`. EBX = SaveLoad
panel 0x26874828; peeking that address showed combat-log strings instead of
a panel struct. EIP in our patch DLL inside DetectTabsCluster (matched the
mov ebp,[ebx+0x24] / cmp ebp,2 / mov eax,[eax] / cmp [eax],0x0073E840
disassembly to `menus_chain.cpp:115-122`).

---

## messagebox_close_unpause
_Engine never clears server pause bit 2 or audio mixer when MessageBoxModal closes; the unpause-on-close is a CGuiInGame::HideSWInGameGui side-effect that only sub-screen closes hit_

When a CSWGuiMessageBox closes (Alt+F4 quit-confirm, save-overwrite,
dialog-skip popups), the engine's close path goes through
CSWGuiManager::PopModalPanel — it never invokes
CGuiInGame::HideSWInGameGui. As a result the world stays paused
(bit 2 of pause_state_ doesn't get reset) and the audio mixer stays
muted (CExoSoundInternal::SetSoundMode stays at 2). Sub-screens
(InGameOptions Esc-menu, Inventory, Map, …) close via HideSWInGameGui
and don't have this problem.

**Why:** Whoever opens the MessageBox is responsible for unpausing it
on close. CSWGuiTutorialBox has CSWGuiTutorialBox::MessageBoxUnpause
@ 0x006aa230 wired to its OnPanelRemoved chain. Plain CSWGuiMessageBox
(Alt+F4 etc.) has no such hook — the original opener was supposed to
register one but the engine's Alt+F4 path doesn't.

**Fix shape:** edge-trigger on modal_stack non-zero → 0 and call BOTH:

  1. CServerExoApp::SetPauseState(server, 2, 0) @ 0x004ae9a0
     `__thiscall(server, int source_bit=2, ulong on_off=0)`
     Idempotent — internal at 0x004b8110 early-returns if bit already
     matches. Server flips bit 2, resumes world timers, sends
     SetPauseState back to client, client re-enables animations.

  2. CExoSoundInternal::SetSoundMode(ExoSound, 0) @ 0x005d5e80
     `__thiscall(self, int mode)`
     Mode 2 = paused-by-combat-mute, 0 = playing. The server pause
     message in step 1 does NOT trigger SetSoundMode automatically
     (that path goes through SetPausedByCombat which has gates we
     can't reliably satisfy).

ExoSound global pointer slot @ 0x007a39ec; AppManager.server at
AppManager + 0x08, AppManager pointer slot @ 0x007a39fc.

**Why:** the unpause needs both halves. Either alone leaves a
half-broken state (walking but no audio, or audio but stuck paused).

**How to apply:** any time a MessageBoxModal-style popup needs to
restore world state on close, dispatch these two functions in this
order on the modal_stack 1→0 edge. **Do not** use
CSWCMessage::SendPlayerToServerInput_TogglePauseRequest @ 0x00677800
for edge-triggered cleanup — it XORs bit 2, so consecutive popup
closes alternate between paused/unpaused states. SetPauseState is the
idempotent variant.

Code: patches/Accessibility/engine_subscreen.cpp::TickInputClassReassert.
Full iteration history (TogglePauseRequest → HideSWInGameGui →
TogglePauseRequest + SetSoundMode → SetPauseState + SetSoundMode) in
docs/in-game-menu-input-investigation.md under "Alt+F4 quit-confirm
popup walking break — FIXED".

---

## kotor_editbox_no_caret
_The engine's editbox widget doesn't track or move a caret; arrow Left/Right are no-ops engine-side, all editing happens at end of string_

`CSWGuiEditbox` (vtable `0x0073eac8`, struct size `0x160`) does not store
caret position anywhere in its struct. Verified by 21 Left/Right
keypresses against an arm-time baseline diff of the full 352-byte
editbox struct — every press came back "no diff" across the entire
struct (`patch-20260509-202159.log`).

Implications:

- Backspace deletes from the end of the string regardless of any user-
  perceived cursor position. The widget is functionally append-only.
- Left/Right arrow keys produce zero observable engine-side state change.
  Don't bind them to caret-style nav (no field to read), and don't fake
  it with a patch-internal virtual cursor (would diverge from append-
  only delete behaviour).
- The two `short` fields at `+0x150` / `+0x152` and the `uint32` at
  `+0x15c` are render-side metadata (max-length / font / color), NOT
  string length or caret. Use `strnlen(c_string)` for the visible text
  length; ignore the `+0x15c` field for length purposes.

**Why:** The vanilla single editbox use case (chargen Name screen) was
reasonably blamable as "we just don't know where caret lives," but a
full-struct diff across many presses ruled out caret state existing in
the widget at all. Saved this finding so future editbox work doesn't
re-search for a caret offset that doesn't exist.

**How to apply:** When adding accessibility for any future editbox-
bearing panel, treat the widget as append-only: poll text via
`strnlen` of `c_string`, fire single-char insert/delete announces from
the diff loop, bind Up/Down to full-text re-read, leave Left/Right
unbound. Source of truth in `menus_editbox.cpp` (the `s_state` snapshot
+ `PollAndAnnounceDiff`).

**Enter is native submit — never QueueActivate the OK button on editbox
Enter.** The engine handles editbox Enter itself (HandleDoneButton); a
second programmatic activate pops the underlying panel and locks the UI.
(This absorbs the former `project_editbox_double_activate_race` note.)

---

## partyselection_portrait_layout
_CSWGuiPartySelectionButton (vtable 0x756BB8) — LIVE selected flag @ +0x1c4 (use this for in-team status); partyId @ +0x44c is an open-snapshot (stale after toggles); NPC roster slot @ +0x450 reliable; +0x448 flag word NOT a selectability gate; panel selected-count @ +0x68 (cap 2)_

`CSWGuiPartySelection.party_data[9]` is an array of `CSWGuiPartySelectionData`
entries (Lane SARIF). Each entry IS the portrait control — vtable
`0x00756BB8`. Layout:

- `+0x000..0x447`  CSWGuiButton + two inner labels (icons; inline text empty)
- `+0x1c4`         **LIVE selected flag** (int): 1 = currently in the party
                   on this screen, 0 = on the bench. Written by
                   `CSWGuiPartySelectionButton::SetSelected @0x006be370` on
                   every toggle. **Use this for "im Team"/"verfügbar", NOT
                   +0x44c** — +0x44c is a panel-open snapshot that never
                   updates, so reading it froze the spoken status at the
                   opening composition (the bug fixed 2026-06-08).
- `+0x448`         flag word — **DO NOT USE as selectability gate**.
                   OnPanelAdded @0x006beeb0 only writes it for some
                   slots; the rest carry uninitialised heap memory.
                   Observed garbage values in
                   patch-20260526-120026.log: `0xfffffff9`, `0x5f484c41`
                   ("ALH_"), `0x39000001` — three slots' low bits
                   randomly happen to be 1, breaking any `(flags & 1)`
                   gate. Trace-logged for diagnostics only.
- `+0x44c`         CSWParty index when slot is in the active party at
                   panel-open; `0xffffffff` otherwise. **Reliable.**
- `+0x450`         NPC roster slot index (0..8). The param
                   `CSWPartyTable::GetIsNPCAvailable / GetNPCSelectability
                   / GetNPCObject` take. **Reliable.**

**Why:** The flag word is the engine's mirror of GetIsNPCAvailable AND
GetNPCSelectability but isn't written for unrecruited slots, so trusting
it leaks "control N" rows into the chain. The +0x450 roster index IS
written for all 9 portraits (clean 0..8 sequence in every log).

**Toggle/commit model (decompiled 2026-06-08):**
`OnToggled @0x006bf2a0` → reads selected (+0x1c4); if adding (selected==0)
and panel selected-count `field2_0x68 @ panel+0x68` is already >1, it
early-outs and refuses (no state change — silent to the user, so
menus_chain speaks "Gruppe voll" in that case). Otherwise calls
`SetSelected` and inc/decrements panel+0x68. `UpdateCount @0x006be4e0`
shows `2 - field2_0x68` remaining (cap is 2 non-PC companions).
`OnPanelAdded @0x006beeb0` initialises +0x1c4=1 for the members in party
at open; `OnEnter @0x006bf5b0` (focus/hover) reads roster slot from
`custom_value[0x114]`. The live focus monitor re-runs the extractor and
re-announces when +0x1c4 flips, so no explicit re-announce hook is needed
for a successful toggle.

**How to apply:** Read `+0x450` for the slot index and ask the engine
directly via `PartyTableIsNPCAvailable(slot)` (defined in
engine_player.cpp). For "is in active party / selected" use the LIVE
`+0x1c4 != 0`, NOT `+0x44c`. Names
go through `GetPartyNpcNameForSlot(slot, ...)` which tries the engine
`GetNPCObject` chain first and falls back to a fixed roster table
(Bastila Shan, Canderous Ordo, Carth Onasi, HK-47, Jolee Bindo, Juhani,
Mission Vao, T3-M4, Zaalbar) — needed for the open-world case where
benched companions aren't in the current module so the engine accessor
returns 0.

---

## party_switch_engine_surfaces
_change_party strip is suppressed from the chain (IsDecorativeForChain); OnChangeCharacter switch handler + arrows RE'd but NOT wired (no direct-dispatch shipped)_

The InGameEquip and InGameCharacter panels both have a bottom-row strip
`[← arrow] [portrait 1] [portrait 2] [→ arrow]`. The portraits are the
switch targets; the arrows are pagination over an internal 9-slot NPC
roster.

**Why the arrows are useless in KOTOR 1**: max active party is 3 (PC +
2 NPCs). The 2 change_party portrait slots always cover both companions,
so pagination has nothing to advance to — pressing them visibly does
nothing. Suppressed from the chain via IsDecorativeForChain.

**STATUS (verified 2026-06-14):** Only the chain-suppression is wired in code (the change_party strip is hidden from the menu chain via `IsDecorativeForChain`, `menus_chain.cpp`). The direct-dispatch switch below was RE'd but **never wired** — there is no `QueueCharacterSwitch`/`ChangeCharacter` in the patch source. Treat the handler addresses as a speculative RE note, not a shipped path.

**Engine handlers** (RE'd from Lane's gzf — NOT wired):
- `CSWGuiInGameEquip::OnChangeCharacter     @ 0x006ba820`
- `CSWGuiInGameCharacter::OnChangeCharacter @ 0x006af350`
- (arrows, NOT wired: `OnSwitchLeft/Right` at 0x006b60f0 / 0x006b6370 for equip; 0x006af450 / 0x006af6d0 for character)

Signature: `void __thiscall(this_panel, CSWGuiControl* btn)`.
- `btn` IS dereferenced — handler branches on
  `btn == &this->change_party_2_button` to decide direction.
- Gates on `btn->is_active != 0` (returns early otherwise). Caller must
  raise is_active=1 before dispatch, same shape as the equip-slot picker.

**Why click-sim fails for the strip**: the bottom-row stat-row labels
overlap the buttons in z-order, so `MoveMouseToPosition` resolves to a
label and the subsequent LMouseDown/Up never reaches the portrait.
Verified in patch-20260525-210226.log line 1740 — `moveTo(186,424)`
resolved `mouseOver` to `defense_label`. Direct dispatch (calling
`OnChangeCharacter` with is_active forced) would bypass the trap if ever
wired, but this was never implemented.

**Identification**:
- Equip: struct offsets only — the engine renumbers gui IDs when the
  runtime-added char_left/right pair collides with BTN_CHANGE2's id=40,
  so id=40 in the panel walk is actually character_left at runtime.
  Offsets in `engine_offsets.h`:
  - kEquipPanelChangeParty1ButtonOffset, kEquipPanelChangeParty2ButtonOffset
  - kEquipPanelCharacterLeftButtonOffset, kEquipPanelCharacterRightButtonOffset
- CharSheet: gui cids 64/67 (change_party) and 65/66 (arrows). Stable
  because character.gui declares them all statically.

---

## galaxymap_model
_Galaxy/star-map travel screen engine model + accessibility implementation (planet cycle, hidden-state, description peek)_


CSWGuiInGameGalaxyMap (galaxymap.gui, PanelKind::InGameGalaxyMap @CGuiInGame+0x80) — the Ebon Hawk travel picker. **Single-axis** "cycle one planet, then Travel/Cancel", NOT a button grid.

**Engine model (decompiled, Lane's gzf, struct SIZE 0x2550):**
- 16 planet hotspots = image-only CSWGuiButtons (`planet_buttons[16]` @+0x64, stride 0x1c4) with EMPTY captions → render as "control N" through the generic chain. Each stores its planet index in `custom_value`.
- Real selection state lives **server-side on CSWPartyTable**, not the panel: `GetPlanetAvailable(idx)` = revealed (vs **hidden**), `GetPlanetSelectable(idx)` = reachable now, `galaxy_map_selected_planet`.
- `NextPlanet @0x694bf0` / `PrevPlanet @0x694ca0` iterate to the next `available && selectable` planet — **skip hidden/unselectable for free**.
- `HandleInputEvent @0x695980` switch: 0x27/0x2d=accept(travel, runs k_sup_galaxymap+HideGalaxyMapGui), 0x28/0x2e/0xdf=cancel, 0x2f/0x31/0x3d/0x3f=PrevPlanet, 0x30/0x32/0x3e/0x40=NextPlanet. Guarded on state!=0 (press).
- `DisplayPlanet` sets `LBL_PLANETNAME` (+0x1ca4) + `LBL_DESC` (+0x1de4) from `planets[idx]` strrefs ({name_ref, description_ref, model_ref} @+0x23cc, stride 0x18), loaded from `planetary.2da` (cols name/description/icon/model/guitag; rows 0-15: EndarSpire,Taris,EbonHawk,Dantooine,Tatooine,Kashyyyk,Manaan,Korriban,Leviathan,UnknownWorld,StarForge,Live01-05).
- accept_button +0x1f24, back_button +0x20e8, current_planet +0x254c. No title control in the .gui.

**Our impl (menus_galaxymap.cpp, shipped 2026-06-08):** Up/Down → QueueGalaxyInput(panel, 0x2f/0x30) via pending Kind::GalaxyInput → DispatchInput calls HandleInputEvent by address + re-reads LBL_PLANETNAME. Enter=accept(0x27), Esc=cancel(0x28). Shift+Down → SpeakDescription (LBL_DESC) via peek_description branch. First-sight Tick (from TickMonitors) scans panels[] for the kind, speaks GalaxyMapTitle ("Galaxiekarte") + current planet. Handler owns ALL nav keys so the unnamed planet buttons never leak into the generic chain. User chose Up/Down-cycles model (not Left/Right). See [[feedback_first_sight_title_only]].

---

## skillinfobox_showgranted
_Engine slot at CGuiInGame+0x9c is reused by chargen Feats/Skills/Powers to dump auto-granted entries; rows store only icon+strref, not the underlying ID_

The "second popup" on chargen Talente entry is `skillinfo.gui` mounted on
CGuiInGame's `skill_info_box` slot (PanelKind::SkillInfoBox). It is
**not** a feat picker — it's `CSWGuiFeatsCharGen::ShowGranted` (called
from OnPanelAdded after the tutorial), which dumps the class's
auto-granted feats. Different per class; user just dismisses with OK.

Key ghidra findings (k1_win_gog_swkotor.exe.gzf, addresses match Steam):

  * 0x006f3460  CSWGuiFeatsCharGen::InitiateFeats — populates a
                stack-local CExoArrayList<ushort> via
                CSWCLevelUpStats::AddGrantedFeats, filters against
                existing feats, then calls
                CSWGuiSkillInfoBox::SetSkillList(picker, list, 1).
                The feat-ID list is FREED before we'd read it.
  * 0x006cdfc0  CSWGuiSkillInfoBox::SetSkillList — for each feat ID,
                calls CSWGuiInGameSkillEntry::SetSkill on a fixed
                row entry. SetSkill stores only the icon (ResRef) and
                the name strref — NOT the feat ID. Row → feat ID
                mapping is therefore lost after this returns.
  * Recovery path: read the row's strref at SkillEntry +0xf0 (= the
    embedded CSWGuiLabel.text.text_params.str_ref) and reverse-lookup
    against Rules->feats[] (CSWFeat[i].name_strref @ +0x8). The same
    strref the engine wrote onto the row is what's in the rules table.

CSWGuiSkillInfoBox struct (size 0x24e8):
  +0x000  CSWGuiPanel panel
  +0x064  CSWGuiListBox skills_listbox
  +0x344  CSWGuiLabel  message_label   (LBL_MESSAGE — placeholder text)
  +0x484  CSWGuiButton ok_button       (BTN_OK)
  +0x648  CSWGuiInGameSkillEntry skills[10]   (rows, fixed array)

**Why:** The "naive" picker-row-to-ID mapping (read field23
"available_list" on the underlying CSWGuiFeatsCharGen) is wrong here —
field23 is for the main panel's actual feat-pick listbox, not the
ShowGranted overlay. ShowGranted's source list is freed.

**How to apply:** When adding accessibility for any reuse of
SkillInfoBox (Force Powers chargen, level-up Talents/Powers, etc.),
the row → ID path is "read row strref + reverse-lookup". Helper
already lives in menus_listbox.cpp as `ResolveFeatIdFromRowStrref`;
reuse or factor up if a non-feat domain needs it.

---

## ingame_abilities_screen
_In-game Fähigkeiten/abilities screen accessibility — engine surfaces, per-tab routing, the OnAbilitySelectionChanged trap_


The in-game **Fähigkeiten** screen (`CSWGuiInGameAbilities`, abilities.gui, `PanelKind::InGameAbilities`) is a tabbed character viewer made accessible by a **dedicated handler** `acc::menus::abilities::HandleInput` (menus_abilities.cpp), called from menus.cpp's OnHandleInputEvent before TryHandleInput. It is NOT a ListBoxPanelSpec and NOT the chargen/level-up grid code — structurally it's tabs-over-content, like the settings menu. All offsets/addresses live in engine_offsets.h (search `kAbilities`).

Three tabs via `CGuiInGame.field139_0xbc0` (0=Skills, 1=Powers, 2=Feats):
- **Skills**: flat `ability_listbox` (+0x30DC), 8 rows. The engine has NO keyboard nav for this tab, so WE drive it: `DriveListBoxSelection` + `OnEnterSkill(panel, row)` (@0x6ad180, `__thiscall(this, CSWGuiControl* row)`) — reads row->id as skill index, repaints name/rank/bonus/total labels + LB_DESC.
- **Feats/Powers**: 2D `CSWGuiSkillFlow` charts (field31 +0x3f88 / field30 +0x3f78). The engine DOES nav these natively — forward to `HandleInputEvent(panel, code, 1)` (@0x6ae5f0): 0x31=row-up, 0x32=row-down (runs `SkillFlowChart::HandleInput`@0x6cdd80 + `OnEnterFeat`/`OnEnterPower`). The chart WRAPS, so pre-clamp on chart row (field_0xd) vs row-count (field1_0x4). Feats are binary (no rank/bonus/total — those labels stay stale at 0, so don't read them).

Tab switching: `OnSkillsButtonPressed`/`OnPowersButtonPressed`/`OnFeatsButtonPressed` (@0x6adad0/0x6adaa0/0x6ada70) each just set field139 + UpdateView (safe). `DisplayPowers`@0x6abe70 is a pure predicate (Jedi + powers>0) to decide if the Powers tab exists. Engine cycle key is 0x29.

**TRAP — `OnAbilitySelectionChanged`@0x6ad4b0: do NOT call it.** It's the mouse hit-test handler (`SkillHitCheckMouse` on cursor coords) AND it crashes via `OnEnterPower`. Worse, despite Ghidra labelling it `(void)`, its SARIF purgeSize=4 → it does `ret 4`; calling it as a zero-arg thiscall slid the stack and corrupted the caller frame. See [[feedback_thiscall_int_param_calling_convention]] — always check SARIF purgeSize, not just the Ghidra signature. Description peek (Shift+Up/Down) routes through this screen's RefreshDetail; LB_DESC at +0x33BC (the original peek_description value was right — a mid-debug "fix" to 0x369c was wrong; that's exit_button).

Interaction (shipped): two-level submenu — tab level (Up/Down pick a tab clamped, Enter drills, Esc closes) / list level (Up/Down browse clamped, Esc returns to tab level). The real Esc keypress arrives as **kInputEsc2 (0xdf)**, not kInputEsc1 — match both or it falls through to the engine's close. Tab handlers `On*ButtonPressed` also have purgeSize=4 (pass a dummy arg).

Party switching works for free: engine Tab cycles the leader, `party_leader_announce` speaks the new name, the engine re-populates the panel, and our handler reads the new character's data on the next nav. **InGameAbilities was removed from the content-fingerprint monitor** (menus_monitors IsContentMonitored) — the dedicated handler owns all speech, so the monitor only double-spoke / clobbered the tab-name announce.

Process note: extracting field offsets from the SARIF via `paste`-pairing shifted everything by one row once (drove the wrong listbox). Verify each critical offset individually against a live panel-walk.

---

## charsheet_force_user_signal
_Force-user detection + FP offsets; charsheet caches no creature ptr; signal = lbl_force_stat bit_flags 0x02_


Detecting whether a character is a Force user (for showing/hiding Force Points).

**Force point offsets** — CSWCCreatureStats (client creature +0x2f8 → CSWCLevelUpStats, embeds stats at +0): `max_force_points` short @+0x11e, current `force_points` short @+0x120 (anchored by `field89_0x122` in swkotor.exe.h). Same struct as HP (+0x4c cur, +0x4e max). Shared reader: `engine_reads::ReadCreatureForcePoints(clientCreature, &cur, &max)`. `max==0` is the canonical "not a Force user" (non-Jedi classes, droids). Used by the H self-status brief off `GetClientLeader()`.

**There is NO universal "force-able" flag** and **999 is not a sentinel** (it's the godmode FP cheat value).

**The character sheet caches NO creature pointer.** `CSWGuiInGameCharacter.field66_0x59e4` is MISLABELED in Ghidra — that region holds `party_count` / the selected-char index. `CSWGuiInGameCharacter::SetStats @0x006afda0` re-fetches the creature fresh each call (CSWParty::GetCharacter(0) for leader, GetCreatureByGameObjectID for paged NPC). So you can't read FP off the panel.

**Charsheet Force-user signal = `lbl_force_stat`'s shown bit.** SetStats runs `CSWClass::IsJedi(class)` and for non-Jedi CLEARS bit 0x02 of `lbl_force_stat.control.bit_flags` (hides it); for Jedi sets the text + SETS bit 0x02. Read `*(panel + kCharSheetLblFp(0x16e4) + kControlBitFlagsOffset(0x44)) & 0x2`. The rendered FP label is stale garbage for non-Jedi (engine only writes it in the Jedi branch), so never trust the label value for non-Jedi — gate on this bit. See [[project_creature_hp_lives_on_client_stats]], [[project_kotor_gui_struct_offsets]].

**Chain went stale on Tab.** Variable row-sets (FP row present only for Jedi) don't refresh on a leader-switch because Tab repopulates the SAME panel pointer in place, so HandleNavStep's `activePanel != g_chainPanel` rebind never fires. Fix: `MonitorPanelContents` now calls `RebindChainPreserveIndex(p)` on any in-place content change when `g_chainPanel == p`. Reusable pattern for any future variable-row chain that must track engine-driven content changes. Shipped 2026-06-08, commit 80ce15a.

---

## placeable_state_overrides
_CSWSPlaceable.+0x260 dword holds switch/lever position; state_overrides.cpp owns per-tag label registry — add new puzzles here_

CSWSPlaceable+0x260 (dword) holds the switch/lever current position for
script-driven multi-state puzzles. Toggles 0/1 (and almost certainly
other small ints for >2-state cases) on each OnUsed dispatch, plus
script-propagated changes on neighbours (Lights Out rule).

**Why:** Sith-base Duros release puzzle had 5 wall switches that all
read as "Wandverkleidung" with no spoken state. The +0x358 sentinel we
first found is a one-shot "has been touched" flag, useless for puzzles
where position cycles. Diff-bisecting hex dumps (20 × 64-byte windows
+0x000..+0x4ff) across two activations of wall3 located +0x260 toggling
0→1→0 on wall3 while neighbour wall4 toggled 1→0→1 in lock-step,
matching the Duros's in-dialog "moving one switch moves the neighbour".

**How to apply:**

- `state_overrides.{cpp,h}` owns the per-puzzle registry — add a
  one-line `{tag, offset, label_map}` entry per known puzzle object.
  Wired into `narration::GetSpokenName` after the disambiguator suffix.
- For multi-state placeables the offset will almost always be `+0x260`
  again — try that first when adding a new puzzle.
- Unknown tags stay silent (no "Position 0" leak onto ordinary
  containers / doors).
- Labels are inline per-locale {de, en}; the registry sentinel-terminates
  on `labelDe == nullptr`.
- Limitation: only covers state-as-fixed-offset-field. Puzzles whose
  state lives in a named script local (`SetLocalInt(...)`) will need a
  local-vars iteration reader behind the same registry API — add a
  `local:nVarName` source dispatch when we hit one.

**Bisect methodology** (reusable for any unknown engine field):

1. Add `DumpObjectDiag` in `passive_narrate.cpp::NarrateHandle` —
   tag dump + wide hexdump (20 windows × 64 bytes) gated to the kind
   you're investigating.
2. Take 3+ snapshots across a state transition: baseline, post-action,
   post-action-2 (validates toggle vs one-shot).
3. Grep `bytes@+0xNNN` lines for the target handle, vertically align
   columns, look for the dword that toggles on every action (skip
   incrementing counters and heap-pointer drift).
4. Cross-check on a sibling object you didn't act on to rule out
   global-tick noise.

`tar09_durosx` resref at placeable +0x240 (CExoString) on these
switches looks like a script-target link field — likely useful later
for "what does this switch control" announcements but not exercised
yet.

---

## placeable_hasinventory_offset
_CSWSPlaceable HasInventory is +0x324 not +0x334 (Ghidra mislabel); empty-container detection via item_repository count_


CSWSPlaceable "HasInventory" GFF flag lives at **+0x324** (Ghidra struct in `swkotor.exe.h` mislabels +0x334 as `has_inventory` — that field reads 0 even on real loot containers). Verified by decompiling `CSWSPlaceable::LoadPlaceable` (`ReadFieldBYTE("HasInventory")` → +0x324) and `CSWSPlaceable::OpenInventory @0x587420` (gates container GUI on `field31_0x324 != 0`, then derefs `item_repository`). "Useable" → +0x328.

Loot container layout (all decompile-confirmed):
- `CSWSPlaceable.item_repository` @ **+0x36c** → `CItemRepository*` (null for non-container placeables like switches/computer panels).
- `CItemRepository.items_list` @ +0x0c, **`item_count` @ +0x10** (the `?` in Ghidra's `item_count?` is correct — `GetItemInRepository` / `ItemListGetItem` / `CalculateContentsWeight` all loop `i < item_count` over `items_list[i]`).

Empty-container test = `HasInventory(0x324)!=0 && item_repository!=null && item_count==0`. O(1), no list walk. Used by `engine_area::IsEmptyContainer` → `narration::GetSpokenName` to append ", leer"/", empty" tag (shipped v0.5.2, commit aa9c52f). The old `kPlaceableHasInventoryOffset=0x334` was a latent bug in `IsUsablePlaceable` too, masked because containers are usually `usable=1`.

---

## levelup_power_lost_investigation
_PC force-power lost on manual level-up (Consular/multiclass); engine commit model decompiled; diagnostic build live_


Open bug under investigation (2026-06-08). On the player character, a chosen
force power is silently NOT learned at level-up even though the level commits
(XP threshold + stats advance). Confirmed from save Game23: PC is Scout 7 /
Jedi Consular 4 but KnownList0 holds only 3 powers (Affect Mind 6, Cure 10,
Force Valor 22) where classpowergain.2da column `jcn` (2,1,1,1) requires 5 at
Consular 4 — **2 picks lost**. Reproduced in patch-20260608-105217.log ~11:07:
user picked Force Push (id 23), then Whirlwind (id 27) as a 2nd pick → engine
"all powers selected" modal (strref 0xa621) → Annehmen committed the level but
no power learned.

Key discriminator (user-reported): **Bastila (single-class Sentinel 9) leveled
all 9 with powers sticking; the PC (multiclass) lost a power twice.** So it is
NOT manual-vs-auto leveling — both use the same GUI. PC-specific or
multiclass-specific or double-pick-on-a-power-tree-specific.

Engine commit model for CSWGuiPowersLevelUp (all decompiled, GoG=Steam bytes):
- DeterminePower @0x006f1280 returns status by searching 3 lists: known
  (field24/25 @+0x19d8/+0x19dc)→2, chosen (field30/31 @+0x19f0/+0x19f4)→1,
  available (field27/28 @+0x19e4/+0x19e8)→0, else 3 (prereq). field19 byte0
  @+0x19bc = picks remaining; field20 @+0x19c0 bit0 = is-main-character.
- OnPowerPicked @0x006f2030: DeterminePower then switch; case 0 → if
  remaining(field19)!=0 AddChosenPower else "all selected" modal. The
  over-limit 2nd pick does NOT drop the 1st (engine keeps chosen list).
- AddChosenPower @0x006f1c90: only adds if power found in AVAILABLE list;
  moves it available→chosen, field31++, field19--.
- OnAcceptButton @0x006f1130 (reached via HandleInputEvent @0x006f28c0 event
  0x27, which our FireActivate-on-OK produces): guard `if (remaining!=0 &&
  avail>0) {"must select all" modal; return;}` else loops chosen list calling
  CSWCCreatureStats::AddKnownSpell(stats, class_count-1, spell) — classIndex =
  highest/last class (Consular for PC, only class for Bastila). Then
  SelectionCompleted(level_up_panel). Static analysis says this SHOULD commit
  Push; the losing line is not visible statically (runtime-dependent).

Diagnostic build LIVE (applied, uncommitted) as of 2026-06-08: temporary
DiagDumpPickState in menus_powers_levelup.cpp logs remaining/known/avail/
chosen + chosenIds[] at "after-pick" (each OnPowerPicked) and "at-accept"
(Enter on OK). Fork: chosenIds=[23] at accept but power absent → loss is
downstream in the Annehmen working→real apply (next: hook AddKnownSpell
@0x00649f90); chosen=0 → our pick path. Remove DIAG once root-caused.

ROOT CAUSE FOUND + FIX SHIPPED (2026-06-08, pending in-game test): NOT the
powers picker. DIAG proved chosen=[23] correct at accept in BOTH a kept-power
and lost-power run. It is **step ORDER** in the InGameLevelUp wizard. The
wizard commits each category to the real creature via Clear* funcs
(ClearAbilities/Skills/Feats/Powers) during ChangeState @0x006ee0e0;
ClearPowers @0x006e87e0 does ClearKnownSpells(real) then re-adds only the
LAST class's spells from a SEPARATE working creature that never got the new
power, so any Clear pass after the powers pick wipes it. Canonical order is
Abilities0/Skills1/Feats2/Powers3 — powers LAST. The engine enforces
sequential leveling: exactly one category button has bit_flags **bit 3 (0x8 =
CSWGuiControl::SetEnabled @0x004176a0)** set — the current step — and
OnSelectXxxButton (@0x006ee350 powers etc.) gates entry on is_active; a real
mouse can't click a disabled (bit3-clear) button. Our deferred FireActivate
force-raised is_active and bypassed it, letting the user enter powers first.
Proof: run patch-20260608-125730 (kept) Fähigkeiten 0x..8f→done, Kräfte
0x..86→0x..8f; run -125909 (lost) user entered Kräfte at 0x6 (bit3 CLEAR)
while Fähigkeiten was 0xf. Annehmen gains bit3 only when all steps done.

SHIPPED in v0.4.2 (2026-06-08, tag v0.4.2). Three commits + chargen:
(1) menus_chain.cpp Enter handler gates InGameLevelUp activation on
`bit_flags(+0x44) & 0x8`; if clear, blocks + SpeakLevelUpDoStepFirst() names
the bit3-set step ("Zuerst %s abschließen"). New strings FmtLevelUpDoStepFirst
/ LevelUpStepLocked (5 locales). (2) Display: menus_extract.cpp DisabledSuffix
now keys on bit 3 (not bit 1) for InGameLevelUp + the new CharGen kind, so
not-yet-your-turn steps announce "nicht verfügbar"; the old comment claiming
"SetEnabled never called, use bit 1" was WRONG. (3) Double-announce fix:
AnnounceControl now passes g_chainPanel to FromControl (was null), matching
MonitorFocusedControl, so the enriched label isn't re-spoken as a text change.
(4) Chargen display: new PanelKind CharGen = CSWGuiCustomPanel 0x007595e0 +
CSWGuiQuickPanel 0x00759668 (engine_panels IdentifyPanel vtable detect),
display-only (no gate — chargen permits revisiting, order already respected).
DIAG probes removed. Verified working in-game by user (level-up power sticks,
no double-read, chargen step state announced).
See [[project_radial_populate_decomp]], [[reference_ghidra_headless_decompile]].
Save repair option: add the 2 owed powers via KotOR_IO CLI (no GUI editor).

---

## levelup_annehmen_stale_button
_Pressing Accept on level-up destroys CSWGuiLevelUpPanel — subsequent FromControl on the stale button blows the GS cookie_

InGameLevelUp panel's "Annehmen" (Accept) button triggers `CSWGuiLevelUpPanel::OnSelectAcceptButton @ 0x006ee780` → `SelectionCompleted @ 0x006ee5d0` → `~CSWGuiLevelUpPanel @ 0x006eee70`. The panel and its child buttons are freed synchronously inside our FireActivate vtable[15] dispatch. Cached pointers (g_chain entries, MonitorFocusedControl's last control, the panel-walk fg pointer) all reference freed memory the moment FireActivate returns.

The next tick's `extract::FromControl(staleButton, ...)` walks the freed vtable. `CallDowncast` reads a bogus vtable slot whose contents happen to land in valid-but-wrong code, smashing FromControl's stack. The /GS cookie check at function epilogue trips `__report_gsfailure` → `__fastfail(2)` → STATUS_STACK_BUFFER_OVERRUN (0xc0000409). SEH around individual reads doesn't help — corruption is from a successful-but-wrong indirect call, not an AV.

**Why:** Confirmed via dump `swkotor.exe(1).31052.dmp` (2026-05-21 13:03 UTC) + accessibility.dll RVA 0x35A96 = `__report_gsfailure`, frame-1 RVA 0x210BF = epilogue of FromControl @ 0x1001F920, ESI = 0x173E4A40 = Annehmen button from the patch log's last `FireActivate target=173E4A40 is_active=0->1` line.

**How to apply:** Treat Annehmen/Accept/OK on commit-style panels (InGameLevelUp confirmed; quit-confirm/save-overwrite/MessageBox already share this failure mode per FireActivate comment) as panel-destroying. After FireActivate on such buttons, invalidate g_chain, g_currentPanel, MonitorFocusedControl's last-control, and any cached panel pointer before the next tick. The existing `is_active=0→1 raise` mitigation was only enough for MessageBox singletons; multi-modal panels need pointer invalidation too.

---

## levelup_panel_state_machine
_CSWGuiLevelUpPanel state machine — Annehmen is the only exit, SetCanCancel only ever 0, Zurück is step-nav only; chain filters both back buttons by struct offset_


`CSWGuiLevelUpPanel` (the in-game level-up hub, PanelKind::InGameLevelUp, vtable 0x00759568) is a category hub: button_level_steps[0..3] = Attribute/Fähigkeiten/Talente/Kräfte (each opens a CSWGuiLevelUpCharGen sub-screen), button_level_steps[4] = **Annehmen/Accept** (→ OnSelectAcceptButton @0x006ee780, gated on the clicked button's `is_active != 0` — satisfied by our FireActivate is_active 0→1 raise), then **button_back "Zurück"** and **button_cancel "Abbrechen"**.

State machine: `field9_0x1ccc` = current/highlighted step, `field10_0x1cd0` = first available step (set in OnPanelAdded), `field16_0x1ce8` = can-cancel. `ChangeState(1)` = step back: no-ops when `field9==field10 && field16==0`; steps `field9--` otherwise; runs `CancelLevelUp`+close only when `field16!=0`.

Key findings (RE'd 2026-05-31, decompile + SARIF xref):
- **The in-game level-up cannot be cancelled.** `SetCanCancel` @0x006ee640 has exactly ONE caller in the whole binary — `CSWGuiLevelUpCharGen::OnPanelAdded` @0x006e7b40 — and it passes **0**. So `field16` is always 0, the `CancelLevelUp` branch is dead, and **Annehmen is the only exit** for sighted and keyboard players alike.
- **Esc does not close it.** The panel's HandleInputEvent only maps 0x28/0x2e→ChangeState(1) and 0x2d→base; base CSWGuiPanel::HandleInputEvent @0x00409e60 just forwards to the focused control (no pop). It plays a GUI sound but stays open.
- **Zurück → CSWGuiPanel::OnBButtonPressed → panel->HandleInputEvent(0x28) → ChangeState(1)** = step-back of the visual category highlight only. Useless for us (we navigate categories with our own arrows).
- Button click dispatch (CSWGuiControl::HandleInputEvent @0x00418750) does NOT check the enabled bit — it fires the registered 0x27 event handler regardless of is_active/enabled. So our FireActivate always reaches the handler; "Back not working" was the ChangeState no-op, not a swallowed click.

Fix (commit fb80244): `isDecorative` in menus_chain.cpp drops both back buttons from the chain, identified by **fixed struct offset, NOT control id** — ids are reassigned per session (Zurück seen as id 19 then id 1). Offsets in engine_offsets.h: `kLevelUpButtonBackOffset=0x1944`, `kLevelUpButtonCancelOffset=0x1B08` (two trailing CSWGuiButton, stride 0x1c4, before field9_0x1ccc@0x1ccc). See [[project_action_menu_engine_surfaces]], [[project_powers_levelup_is_skillflow_tree]].

---

## powers_levelup_is_skillflow_tree
_The pwrlvlup.gui "powers_listbox" rows are 3-cell CSWGuiSkillFlow tree-rows; same shape as chargen feats. Drive via chargen_feats-style 2D nav, not ListBoxPanelSpec._

The CSWGuiPowersLevelUp panel (.gui id 6 control labelled "powers_listbox") is structurally a 2D feat tree. Each listbox row is a `CSWGuiSkillFlow` with up to 3 `CSWGuiFlowSkillStruct` cells at +0x5c / +0x184 / +0x2ac (base / improved / master variants). The embedded `CSWGuiSkillFlowChart` at panel+0x19fc tracks (selected_row, selected_col) but its rows_data is empty in level-up — iterate `powers_listbox.controls` instead (the engine's own source per `OnPowerSelectionChanged @0x006f1940` decomp).

**Why:** the previous flat-listbox spec couldn't pick a column within a row — `SkillHitCheckMouse` derives the column from stale mouse coords, so a `selection_index` write alone leaves col undefined. Verified live 2026-05-26: with chargen_feats-style 2D nav, all 17 power families and Wound � Choke � Kill 3-cell rows navigate correctly with status enum announces.

**How to apply:** for any panel whose engine class uses `CSWGuiSkillFlow` rows (Powers, Feats, possibly future variants), reach for `menus_powers_levelup.cpp` / `menus_chargen_feats.cpp` as templates � not the listbox dispatcher. Engine surfaces: `OnEnterPower @0x006f1460`, `OnPowerPicked @0x006f2030`, `OnPowerSelectionChanged @0x006f1940`, chart at `panel + 0x19fc`. SkillFlow cell offsets + status enum are shared with feats (see `engine_offsets.h:kSkillFlow*` and `kFlowSkillStruct*`).

---

