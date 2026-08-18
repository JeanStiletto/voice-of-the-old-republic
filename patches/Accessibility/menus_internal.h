// internal seam between menus.cpp and menus_extract.cpp.
//
// This is NOT public API; it's a private contract between the two TUs that
// were one TU before Step 2B of the refactor. Module names declared here
// live in `acc::menus::detail::` to signal "internal-but-exposed-across-
// TUs."
//
// What the seam covers, and why each thing is here:
//
//   * g_currentPanel — the focused-panel pointer maintained by
//     OnSetActiveControl (in menus.cpp) and read by ExtractAnnounceableText
//     (in menus_extract.cpp) for the slider sibling-label / cycle-flanker
//     gates and the per-kind owner-panel fallback.
//
//   * IsChainNavigable — predicate for "can the user focus this control
//     with arrow keys?" Used by chain code in menus.cpp (RebindChain,
//     AppendChainEntry, FindCloseButton/CancelButton, FindAdjacentArrow)
//     AND by the sibling-label fallback in extract.
//
//   * IsClassSelectionIcon — positional detector for chargen class-icon
//     buttons. Used by extract's perkind-classsel branch AND by menus.cpp
//     chain code (cursor x-offset compensation, RebindChain pitch capture,
//     OnSetActiveControl prefill, AnnounceControl cache-aware skip).
//
//   * ClassLabelCacheLookup / Store — lazily-populated (icon → class_text)
//     cache for the chargen class-selection panel. The cache state lives
//     in menus.cpp (file-static); the extract path reads (lookup), the
//     OnSetActiveControl prefill writes (store). Lookup-then-store happens
//     inside extract step 9c too, so both directions cross the seam.
//
//   * kEquipBtn* / kEquipLb* — control IDs from equip.gui. Used by extract's
//     perkind-InGameEquip branch (slot-button label resolution) AND by the
//     OnHandleInputEvent equip-slot detector / equip-picker handler in
//     menus.cpp.
//
// The in-game-menu-icon name table stays private to its owning TU because
// only one side uses it. `kSaveLoad*` moved to menus_internal.cpp with
// IsSaveLoadPanel, its only consumer.
//
// The declarations in this header are implemented in menus_internal.cpp
// (they lived in menus.cpp until the Phase-1 structure pass split that
// file into menus.cpp / menus_internal.cpp / menus_focus.cpp /
// menus_dispatch.cpp).

#pragma once

#include <cstddef>
#include <cstdint>
#include "engine_offsets_select.h"

namespace acc::menus::detail {

// Buffer size every announce path allocates for a control's spoken text.
//
// It was 256 for years, which was not merely a clipping limit: several
// per-kind extraction steps used to bail out ("this control has no name")
// rather than truncate when their text did not fit, so the CALLER'S buffer
// size decided whether a control was announceable at all. EmitText in
// menus_extract.cpp now truncates instead, and this constant is sized so
// nothing the game renders gets clipped in the first place — the longest
// real string is a chargen class name plus its description, a bit over 250
// characters in German. Cheap: these are stack buffers on paths that run at
// most once per keypress, plus one static snapshot in the focus monitor.
constexpr size_t kAnnounceTextMax = 1024;

bool IsChainNavigable(void* control);
bool IsClassSelectionIcon(void* panel, void* control);
const char* ClassLabelCacheLookup(void* panel, void* icon);
void ClassLabelCacheStore(void* panel, void* icon, const char* text);

// Compose "class name" + the engine's class blurb into one announce string.
// Must be called at the moment class_label is read — both labels hold the
// NEXT icon's text by the time a chain step announces this one.
void ComposeClassAnnounce(void* panel, const char* name,
                          char* outBuf, size_t bufSize);

// Class-description placeholder discrimination. classsel.gui authors
// LBL_DESC with a placeholder that the engine replaces on hover; Capture
// snapshots the authored text at panel-walk time (before any hover) and
// IsEngineWritten answers "is this the engine's class description, or still
// the .gui default?" for whatever is in the label now.
void ClassDescBaselineCapture(void* panel, const char* text);
bool ClassDescIsEngineWritten(void* panel, const char* text);

// Convert a distance measured in .gui authoring units into the units
// GetControlCenter below actually returns on the running game.
//
// The two games differ here and it is easy to miss, because KOTOR 1 needs
// no conversion at all: it ships one .gui variant per resolution and hands
// us the authored extents verbatim — mainmenu8x6.gui declares BTN_NEWGAME's
// centre at 485,292 and that is exactly what the chain logs. KOTOR 2
// stretches its single 800x600 layout to the window and stores the RESULT
// in the extents (the same fact menus_focus_k2.cpp records for the cursor
// warp), so at 2880x1800 every distance is 3.6x the authored one.
//
// Consequence: any bare pixel threshold in this codebase is a KOTOR 1
// assumption. Every one of them was measured off a KOTOR 1 .gui file, and
// on KOTOR 2 they are wrong by a factor that changes with the player's
// resolution. That is what leaked all twelve chargen +/- steppers into the
// chain as "control 12" … "control 34" (patch-20260803-011930.log): they
// sit 27 authored units from their value button, 97 at that resolution,
// past SquashCycleFlankers' 80-unit reach.
//
// Returns `guiPx` unchanged on KOTOR 1, and on KOTOR 2 whenever the window
// is unreadable — never shrinking a threshold, so a failed read can only
// preserve the pre-existing behaviour.
int ScaleGuiThresholdPx(int guiPx);

// Screen-center of a control's extent, false on degenerate extents.
// Used by FindSiblingLabel / IsCycleFlankerArrow in menus_extract.cpp
// and by FindAdjacentArrow / AppendChain* in menus.cpp.
bool GetControlCenter(void* control, int& outCx, int& outCy);

// Locate a child control on `panel` by its +0x50 ID field. Stable across
// localizations and panel.controls reordering.
// Defined in menus.cpp; used there + in menus_listbox.cpp (Step 4 spec
// callbacks for Container/SaveLoad/EquipPicker).
void* FindControlById(void* panel, int id);

// Find the first CSWGuiListBox among panel.controls. Container's input
// handler + the dialog/container monitors use it; the spec dispatcher in
// menus_listbox.cpp's Container entry calls it on every Up/Down step.
void* FindListBoxChild(void* panel);

// Detect the CSWGuiSaveLoad panel by its vtable (kVtableCSWGuiSaveLoad) —
// the ctor-written identity of this dynamically allocated panel, immune
// to .gui renumbering. Used by the SaveLoad spec entry's `matches`
// callback and the K2 selection-sync monitor. SEH-safe on stale pointers.
bool IsSaveLoadPanel(void* panel);

// Read a CExoString-style field on a control by byte offset. Used by the
// SaveLoad announce callback to pull planet (lastmodule) + area (areaname)
// from each save-row entry. Returns nullptr on empty field.
const char* ReadSaveLoadEntryString(void* entry, size_t fieldOffset);

// Result of a single arrow-key step on a listbox cursor. Filled by
// DriveListBoxSelection; caller reads `row` to announce, `newSel`/`rowCount`
// for index reporting, and `oldSel == newSel` to detect a boundary clamp
// (per-tick monitors that watch selection_index changes won't fire on a
// no-op step, so callers using a monitor announce inline only on clamp).
struct ListBoxNavResult {
    short oldSel;     // selection_index before the step
    short newSel;     // selection_index after the step
    int   rowCount;   // listbox.controls.size
    void* row;        // row pointer at newSel (nullptr iff rowCount == 0)
};

// Direction of cursor motion driven by DriveListBoxSelection.
//   * StepUp / StepDown: ±1 with boundary clamp (Nav-Up / Nav-Down).
//   * JumpFirst / JumpLast: absolute jump to minSel / rowCount-1
//     (Home / End). Caller still gets a `ListBoxNavResult` populated the
//     same way as a step; oldSel == newSel still indicates "already at
//     the boundary" so the inline announce-on-clamp branch fires.
enum class ListBoxNavOp { StepUp, StepDown, JumpFirst, JumpLast };

// Drive a CSWGuiListBox cursor in response to Nav-Up / Nav-Down / Home / End.
// Pure listbox-side effect: writes selection_index + scrolls
// top_visible_index to keep the new selection visible. Does NOT call
// SetSelectedControl, so the engine's onSelectionChanged callback won't
// run. minSel = first valid selection (0 normally, 1 for equip-picker
// LB_ITEMS to skip the protoitem template at row 0). Returns false iff
// listbox is null or rowCount == 0.
bool DriveListBoxSelection(void* listbox, ListBoxNavOp op, short minSel,
                           ListBoxNavResult& out);

// Same contract as DriveListBoxSelection, but drives the engine's own
// CSWGuiListBox::SetSelectedControl instead of writing selection_index raw.
// SetSelectedControl sets the row's real highlight state, plays the GUI select
// sound, and scrolls the page natively (OrganizeControls) — so multipage lists
// work the engine's way and the selection survives the per-frame hover-select
// that overwrites a raw index write. The target index is computed with the same
// no-wrap clamp as DriveListBoxSelection. Callers that use this MUST also keep
// the mouse cursor off the list while armed (see the picker arm path), otherwise
// the engine's hover handler re-selects the row under the cursor every frame.
// Returns false iff listbox is null or rowCount == 0.
bool DriveListBoxSelectionEngine(void* listbox, ListBoxNavOp op, short minSel,
                                 ListBoxNavResult& out);

// Warp the OS cursor to the empty top-left corner of the screen so the engine's
// per-frame hover-select (CSWGuiListBox::HandleMouseMove -> HitCheckMouseLocal
// -> SetSelectedControl) finds no row under it and stops overwriting the
// keyboard-driven selection_index writes DriveListBoxSelection makes. Any panel
// that drives a listbox from the keyboard must park the cursor while it is up.
//
// MUST be called from a per-tick monitor (Update tick), never from the input
// hook — MoveMouseToPosition recurses through the hover pipeline. `tag` is the
// caller's acclog tag. Defined in menus_monitors.cpp, next to the park-position
// rationale (the corner has to clear the camera edge-turn band).
bool ParkCursorToCorner(const char* tag);

// Queue activation of the chain-navigable button child of `panel` whose
// +0x50 ID matches `buttonId`. Reserved for select-then-confirm panels
// (Container / SaveLoad spec entries). Returns false on debounce or
// missing target — caller still consumes the keypress so the engine's
// stale activeControl can't take over.
bool QueueButtonByIdActivate(void* panel, int buttonId, const char* logPrefix);

// Same debounce/queue/log contract for a control already resolved via an
// engine-member resolver (SaveLoadPanel* etc.) instead of a .gui id.
bool QueueControlActivate(void* target, const char* logPrefix);

}  // namespace acc::menus::detail

// File-scope global maintained by OnSetActiveControl in menus.cpp.
// Read-only from menus_extract.cpp; never assign here.
extern void* g_currentPanel;

// Liveness-filtered view of g_currentPanel — returns it only while it is
// still in the manager's panels[], nullptr once the engine has freed it.
//
// g_currentPanel is a raw cached pointer with no invalidation hook, and
// OnSetActiveControl (its only writer) is SUPPRESSED for the whole duration
// of a save-load / module load. So every area transition leaves it pointing
// at a panel the module teardown freed, and it stays that way until the next
// ordinary focus event. The freed block is promptly reused, so panel+0x20 /
// +0x24 (CExoArrayList data + size) read back as whatever now lives there —
// non-null, plausibly-sized, and data[0] takes an AV. Same failure the chain
// side already guards with ValidateChainPanel; this is its shared form.
//
// Use this at every site that DEREFERENCES the panel. Pointer comparisons
// (g_chainPanel != g_currentPanel and friends) may read the raw global.
void* CurrentPanelIfLive();

// Cross-TU state owned by menus.cpp, touched by the two TUs the Phase-1
// split carved out of it. These were file-static / anonymous-namespace
// while everything lived in one TU; the split is the only reason they are
// declared at all, so treat them as private to the menus module.
//
//   * g_drilledIntoSubScreen — arrow-nav retarget flag. Set/cleared by the
//     drill router + Esc gate in menus_dispatch.cpp, published to the rest
//     of the mod through menus.h's IsDrilledIntoSubScreen accessor.
//   * s_synthesizedNav — true while PollHomeEndKeys (menus.cpp) is
//     synthesising a call into OnHandleInputEvent (menus_dispatch.cpp), so
//     the press/release pair tracker there skips the synthetic press.
//   * s_pendingAnnounce{Panel,Control} — the last-write-wins focus slot.
//     Written by AnnounceNewFocusedControl (menus_focus.cpp), drained once
//     per tick by DrainPendingAnnounce (menus.cpp).
extern bool g_drilledIntoSubScreen;

namespace acc::menus {
extern bool  s_synthesizedNav;
extern void* s_pendingAnnouncePanel;
extern void* s_pendingAnnounceControl;
}  // namespace acc::menus

// Equipment screen control IDs from equip.gui / K2 equip_p.gui (extracted
// via xoreos-tools gff2xml; K2 values mined 2026-08-02 — the install's
// override copy is id-identical to the gui.bif one). K2 RE-NUMBERS the
// whole panel: the slot buttons moved from K1's odd 7..23 spread to a
// 15..25 block (implant parked at 48), LB_ITEMS 5 → 41, BTN_BACK/BTN_EQUIP
// 36/37 → 39/40, and two K2-only second-weapon-set slots appeared
// (BTN_INV_WEAP_L2/R2).
//
// SINCE 2026-08-18 these ids are NOT how equip controls are resolved or
// matched any more: a beta install shipped a variant equip_p.gui with the
// tail ids shifted by one (userlogs/077noequipment), so every consumer
// moved to the engine's own ctor-bound members — EquipPanelItemsListBox /
// EquipPanelEquipButton / EquipPanelBackButton and the
// EquipSlotIndexFrom* pointer arithmetic below. The three ids the member
// resolvers take stay ONLY as the GuiIdMismatch tripwire input (a variant
// .gui then shows up as one loud log line instead of a broken picker);
// the slot-button ids stay as documentation of the mined files.
const int kEquipBtnHeadId    = acc::game::IsKotor2() ? 15 :  7;  // BTN_INV_HEAD     (TLK 31375)
const int kEquipBtnImplantId = acc::game::IsKotor2() ? 48 :  9;  // BTN_INV_IMPLANT  (literal — no TLK)
const int kEquipBtnBodyId    = acc::game::IsKotor2() ? 17 : 11;  // BTN_INV_BODY     (TLK 31380)
const int kEquipBtnArmLId    = acc::game::IsKotor2() ? 18 : 13;  // BTN_INV_ARM_L    (TLK 31376)
const int kEquipBtnWeapLId   = acc::game::IsKotor2() ? 19 : 15;  // BTN_INV_WEAP_L   (TLK 31378)
const int kEquipBtnBeltId    = acc::game::IsKotor2() ? 22 : 17;  // BTN_INV_BELT     (TLK 31382)
const int kEquipBtnWeapRId   = acc::game::IsKotor2() ? 23 : 19;  // BTN_INV_WEAP_R   (TLK 31379)
const int kEquipBtnArmRId    = acc::game::IsKotor2() ? 24 : 21;  // BTN_INV_ARM_R    (TLK 31377)
const int kEquipBtnHandsId   = acc::game::IsKotor2() ? 25 : 23;  // BTN_INV_HANDS    (TLK 31383)
// KOTOR 2 only: the second weapon-set slot buttons. -1 on K1 (matches no
// control). Slot indices 9/10 in the engine's arrays; their item-id
// offsets are kEquipPanelSlotItemIdsOffset + 0x24/0x28.
const int kEquipBtnWeapL2Id  = acc::game::IsKotor2() ? 20 : -1;  // BTN_INV_WEAP_L2
const int kEquipBtnWeapR2Id  = acc::game::IsKotor2() ? 21 : -1;  // BTN_INV_WEAP_R2
const int kEquipLbItemsId    = acc::game::IsKotor2() ? 41 :  5;  // LB_ITEMS
// upgrade.gui LB_ITEMS — the compatible-crystal list the workbench upgrade
// picker drives. Same role as the equip picker's LB_ITEMS: excluded from the
// generic chain flatten so its rows aren't double-exposed as stale buttons.
// Same id in both games' .gui files.
constexpr int kWorkbenchUpgradeLbItemsId = 0;  // LB_ITEMS
// upgrade.gui BTN_BACK — the cursor-park target while the upgrade picker is
// armed (see ParkPickerCursorOffList in menus_listbox_picker.cpp). Shared
// here rather than file-local because both the spec callbacks and the picker
// monitor need it, and they now live in separate TUs. K2 re-numbers it to 13
// — inside K1's slot-button range, the same collision
// IsWorkbenchUpgradeSlotButtonId already routes around.
const int kWorkbenchUpgradeBtnBackId = acc::game::IsKotor2() ? 13 : 28;  // BTN_BACK
const int kEquipBtnBackId    = acc::game::IsKotor2() ? 39 : 36;  // BTN_BACK         (TLK 1582 = Schliess.)
const int kEquipBtnEquipId   = acc::game::IsKotor2() ? 40 : 37;  // BTN_EQUIP        (TLK 1580 = OK)

namespace acc::menus::detail {

// Equip-panel slot identity from the engine's embedded control arrays
// (ctor-mined bases/strides — kEquipPanelSlotButtons/LabelsOffset). Pure
// pointer arithmetic against the panel, no control deref, so no SEH
// needed. Replaces the old .gui-id matching (IsEquipSlotButtonId), which
// broke on installs with a renumbered equip_p.gui (userlogs/
// 077noequipment — see docs/gui-id-audit.md).
//
// Returns the 0-based slot index shared by BOTH games' engine order:
//   0 weapL, 1 weapR, 2 head, 3 armL, 4 armR, 5 body, 6 hands,
//   7 implant, 8 belt; KOTOR 2 appends 9 weapL2, 10 weapR2.
// -1 when the control is not one of this panel's slot buttons (/labels).
// The matching per-slot equipped-item handle lives at
// panel + kEquipPanelSlotItemIdsOffset + 4*index.
int EquipSlotIndexFromButton(void* panel, void* control);
int EquipSlotIndexFromControl(void* panel, void* control);  // button OR label

// Slots in the engine's arrays for the running game (9 on K1, 11 on K2).
inline int EquipSlotCount() { return acc::game::IsKotor2() ? 11 : 9; }

// Engine-truth resolvers for the equip panel's picker members — the
// EMBEDDED items_listbox ("LB_ITEMS"), equip button ("BTN_EQUIP") and
// back button ("BTN_BACK") the constructors bind by tag. Primary path
// for everything that used to FindControlById those three ids; each
// resolver keeps the id lookup only as a TRIPWIRE, logging
// "GuiIdMismatch" once per session when a variant .gui file would have
// sent the id path to a different control. Defined in menus_internal.cpp.
void* EquipPanelItemsListBox(void* panel);
void* EquipPanelEquipButton(void* panel);
void* EquipPanelBackButton(void* panel);

// Workbench upgrade panel (CSWGuiUpgrade) — same engine-truth resolvers.
// The historical assemble id (24) doubles as a K2 slot-button id, so its
// resolver skips the tripwire on K2.
void* UpgradePanelItemsListBox(void* panel);
void* UpgradePanelAssembleButton(void* panel);
void* UpgradePanelBackButton(void* panel);

// Slot-button membership on the upgrade panel via the embedded button
// run (K1: 7 from +0x64, K2: 9 from +0x7a8). Returns the array index or
// -1; identity/membership ONLY — slot semantics stay in the button's
// custom_value (per-bank), which LookupUpgradeSlotType and
// GetWorkbenchSlotInstalledItem already read. Replaces the id-based
// IsWorkbenchUpgradeSlotButtonId.
int UpgradeSlotIndexFromButton(void* panel, void* control);
inline int UpgradeSlotButtonCount() { return acc::game::IsKotor2() ? 9 : 7; }

// CSWGuiSaveLoad — same engine-truth resolvers (ctor-bound embedded
// members, kSaveLoadPanel*Offset; ids only feed the GuiIdMismatch
// tripwire). Panel identity itself is the CSWGuiSaveLoad vtable — see
// IsSaveLoadPanel above. Defined in menus_internal.cpp.
void* SaveLoadPanelGamesListBox(void* panel);   // LB_GAMES
void* SaveLoadPanelActionButton(void* panel);   // BTN_SAVELOAD (Laden/Speichern)
void* SaveLoadPanelBackButton(void* panel);     // BTN_BACK

// K2 workbench/crafting engine-truth resolvers (ctor-bound embedded
// members, kUpgradeSelPanel*/kCraftComponent*/kCraftChemical* in
// engine_offsets_fields.h; the historical .gui ids only feed the
// GuiIdMismatch tripwire). All return nullptr on KOTOR 1 — the member
// offsets poison there and no K1 code path consumes these panels.
//
// UpgradeSelPanel* address CSWGuiUpgradeSelection (upgradesel_p, the K2
// workbench top screen with the inline item list). CraftPanel* address
// the two crafting screens and dispatch on the panel's kind internally —
// CSWGuiCreateItem (component_p) and CSWGuiCreateMedicalItem (chemical_p)
// lay the same controls out at different offsets — returning nullptr when
// `panel` is neither. Defined in menus_internal.cpp.
void* UpgradeSelPanelListBox(void* panel);        // LB_UPGRADELIST
void* UpgradeSelPanelUpgradeButton(void* panel);  // BTN_UPGRADEITEMS
void* UpgradeSelPanelTitleLabel(void* panel);     // LBL_TITLE
void* CraftPanelShopListBox(void* panel);         // LB_SHOPITEMS (create view)
void* CraftPanelInvListBox(void* panel);          // LB_INVITEMS (breakdown view)
void* CraftPanelAcceptButton(void* panel);        // BTN_Accept
void* CraftPanelExamineButton(void* panel);       // BTN_Examine (view flip)
void* CraftPanelTitleLabel(void* panel);          // LBL_TITLE

// CSWGuiPowersLevelUp (pwrlvlup / pwrlvlup_p) engine-truth resolvers —
// ctor-bound embedded members, kPowersPanel*Offset in
// engine_offsets_fields.h. Both games re-number this .gui's ids and
// disagree with each other, so the per-game historical ids survive only
// as the GuiIdMismatch tripwire inside these resolvers. Panel identity
// is the CSWGuiPowersLevelUp vtable (IsPowersLevelUpStructural); callers
// vet the panel first. Defined in menus_internal.cpp.
void* PowersPanelPowersListBox(void* panel);      // LB_POWERS (skill-flow rows)
void* PowersPanelDescListBox(void* panel);        // LB_DESC
void* PowersPanelPowerLabel(void* panel);         // LBL_POWER (selected name)
void* PowersPanelSubTitleLabel(void* panel);      // SUB_TITLE_LBL (title)
void* PowersPanelRecommendedButton(void* panel);  // RECOMMENDED_BTN
void* PowersPanelAcceptButton(void* panel);       // ACCEPT_BTN
void* PowersPanelBackButton(void* panel);         // BACK_BTN

// CSWGuiContainer (container / container_p) engine-truth resolvers —
// ctor-bound embedded members, kContainerPanel*Offset in
// engine_offsets_fields.h. Panel identity is already engine-truth (the
// gui manager's own panel slot, IdentifyPanel == PanelKind::Container),
// so these only move control resolution off the .gui ids; both games'
// files agree on those ids, which survive as the GuiIdMismatch tripwire.
// Defined in menus_internal.cpp.
void* ContainerPanelItemsListBox(void* panel);    // LB_ITEMS (loot rows)
void* ContainerPanelOkButton(void* panel);        // BTN_OK (take all)
void* ContainerPanelCancelButton(void* panel);    // BTN_CANCEL
void* ContainerPanelGiveItemsButton(void* panel); // BTN_GIVEITEMS (give mode)

}  // namespace acc::menus::detail

// partyselection.gui BTN_NPC ("Hinzuf." / "Add") — the mouse flow's
// commit button: click a portrait to highlight it, then click this to
// toggle that companion in or out of the party. Keyboard nav never needs
// it because Enter on the portrait itself already runs the engine's
// OnToggled (see menus_chain_input.cpp's isPartyAddBlocked branch, which
// rides that same path), so it is filtered from the chain. KOTOR 1 only:
// K2's partyselect_p.gui has no single add button (every portrait is its
// own BTN_NPCn), so the K2 value matches no control and the filter is
// inert there.
const int kPartySelectionAddBtnId = acc::game::IsKotor2() ? -1 : 38;  // BTN_NPC

// CSWGuiInGameItemEntry (LB_ITEMS row) — field6_0x394 bit-field after the
// embedded CSWGuiButton + item id + borders + text. OnEnterSlot tags the
// currently-equipped row via SetItem(id, /*param_2=*/1, 0); SetItem packs
// (param_2 & 1) into bit 1 (0x2) of field6_0x394 — the same flag that makes
// the engine append the " (Ausgew.)" suffix. So bit 0x2 set ⇔ "this row is
// the item currently equipped in the selected slot". Used by EquipPickerOnEnter
// to route Enter on the equipped row to an unequip (commit the row-0
// 0x7f000000 "empty" entry) instead of a no-op re-equip.
//
// KOTOR 2 from its own constructor, which builds the same members in the same
// order — button, then three borders, then a text — and then writes the two
// trailing fields: the 0x7f000000 "empty" sentinel into the item id at 0x1d0,
// and three bit-clears (0x1, 0x2, 0x4) into 0x3ac. Arithmetic agrees exactly:
// KOTOR 2's button is 0x1d0 and its border 0x78, so 0x1d0 + 4 + 3*0x78 + 0x70
// (text) = 0x3ac. Both the sentinel and the low three flag bits are the same
// values KOTOR 1 uses, which is what carries the bit index across.
const size_t kEquipItemEntryFlagsOffset  = acc::off::Pick(0x394, 0x3ac);
const uint32_t kEquipItemEntryEquippedBit = acc::off::Same(0x2);
