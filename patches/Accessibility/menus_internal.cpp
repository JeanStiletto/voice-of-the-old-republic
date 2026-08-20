// Implementations of the acc::menus::detail:: seam declared in
// menus_internal.h.
//
// These helpers were defined in menus.cpp until the Phase-1 structure pass
// (refactoring candidate 1). menus_internal.h has always declared them as
// the "internal-but-exposed-across-TUs" contract shared by menus.cpp,
// menus_extract.cpp, menus_listbox.cpp and menus_keymap.cpp; this file is
// the matching .cpp every other seam header in this directory already has
// (menus_chain, menus_pending, menus_listbox, menus_editbox, menus_monitors).
//
// Nothing here changed behaviourally in the move: the definitions are
// verbatim, including the saveload.gui control IDs and the chargen
// class-label cache, both of which are used only by functions in this file.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "menus_internal.h"
#include "strings.h"
#include "menus_pending.h"   // QueueButtonByIdActivate defers the activate
#include "engine_game.h"      // IsKotor2 — the save/load control ids differ
#include "engine_offsets.h"
#include "engine_panels.h"   // HasVtable
#include "engine_reads.h"
#include "engine_rebase.h"

using namespace acc::engine;

// CSWGuiSaveLoad control ids from saveload.gui / K2 saveload_p.gui
// (verified against chain logs: patch-20260505-160124.log lines 45-65;
// K2 witnessed live in patch-20260813-212757 — K2 re-authored the screen
// and every id but the action button moved, which once silently disabled
// the whole SaveLoad spec there).
//
// SINCE the gui-id audit (2026-08-18, docs/gui-id-audit.md) these ids are
// NOT how the panel or its controls are resolved any more: identification
// is the CSWGuiSaveLoad vtable, and the controls are the ctor-bound
// embedded members (kSaveLoadPanel*Offset). The ids survive only as the
// GuiIdMismatch tripwire input on the SaveLoadPanel* resolvers below, so
// a variant .gui file shows up as one loud log line instead of a
// mis-wired screen.
//
//   K1                              K2
//   id=0   games_listbox            id=12  games_listbox
//   id=12  back_button              id=13  back_button
//   id=14  saveload_button          id=14  saveload_button (unchanged)
inline int SaveLoadLbGamesId() { return acc::game::IsKotor2() ? 12 :  0; }
inline int SaveLoadBtnBackId() { return acc::game::IsKotor2() ? 13 : 12; }
inline int SaveLoadBtnSaveLoadId() { return 14; }

// KOTOR 2's GUI authoring width. Every _p.gui in its gui.bif declares
// MAIN_PNL as 800x600 and the engine stretches that to the window.
constexpr int kKotor2GuiAuthoringWidth = 800;

// See menus_internal.h for why this exists and when it is a no-op.
int acc::menus::detail::ScaleGuiThresholdPx(int guiPx) {
    if (!acc::game::IsKotor2()) return guiPx;
    HWND hwnd = GetActiveWindow();
    if (!hwnd) return guiPx;
    RECT rc = {0, 0, 0, 0};
    if (!GetClientRect(hwnd, &rc)) return guiPx;
    int clientW = rc.right - rc.left;
    if (clientW <= kKotor2GuiAuthoringWidth) return guiPx;
    return (int)((long long)guiPx * clientW / kKotor2GuiAuthoringWidth);
}

// Center pixel of a control's hit area. Returns false on null control,
// unreadable control, or degenerate extent (zero/negative width/height —
// sometimes seen on hidden panels and templated control prototypes).
//
// SEH-guarded because a non-null pointer is NOT proof the control is alive.
// Cycling sub-screens quickly on KOTOR 2 crashed the process here
// (patch-20260801-232432.log + CrashDumps/swkotor2.exe.46564.dmp: fault
// reading [esi+0xc] — this function's `ext[2]` — at accessibility.dll+0x18FFFA,
// with a freed-and-reused control pointer). The panel had been torn down by
// the screen switch while the chain still held its control; the null check
// passed and the read faulted. Same failure class as the FocusProbe and
// TryPartyPortrait crashes: engine reads here are guarded by convention, and
// this one was the exception.
bool acc::menus::detail::GetControlCenter(void* control, int& outCx, int& outCy) {
    if (!control) return false;
    __try {
        auto* ext = reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(control) + kControlExtentOffset);
        int width  = ext[2];
        int height = ext[3];
        if (width <= 0 || height <= 0) return false;
        outCx = ext[0] + width  / 2;
        outCy = ext[1] + height / 2;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Screen-absolute center of a CSWGuiListBox row. Listbox children's extents
// are listbox-local (origin at the listbox's top-left, not the screen) so
// click-sim at row.extent alone lands on dead space. Add the listbox's own
// extent origin to translate. Listboxes themselves are panel-direct children
// whose extents are already screen-absolute (panels render at fixed
// positions), so one accumulation step is sufficient for the InGameEquip
// LB_ITEMS case. If we ever need to click rows in a deeper-nested listbox,
// generalise this into a parent-chain walk.
// True if the control is button-like (CSWGuiButton or its subclasses
// CharButton / ActivatedButton / ButtonToggle) OR a CSWGuiSlider.
// MoveMouseToPosition's hover→active promotion path is safe for buttons but
// crashes when the active control is a label (verified: navigating onto the
// main-menu "Neue Inhalte verfügbar…" label froze the game). Sliders are
// included because Sound's Music/Voice/SFX/Movie controls are real sliders
// and we want chain navigation to land on them so we can announce their
// numeric value.
//
// Long-term: replace with a proper CSWGuiControl::GetIsSelectable call
// (vtable lookup at 0x4189d0) to also include editbox / listbox / etc.
bool acc::menus::detail::IsChainNavigable(void* control) {
    if (!control) return false;
    if (CallDowncast(control, kVtableAsButton)        != nullptr) return true;
    if (CallDowncast(control, kVtableAsButtonToggle)  != nullptr) return true;
    if (IsSlider(control))                                        return true;
    return false;
}

// Locate a child control on `panel` by its +0x50 ID field. The .gui-time IDs
// are stable per panel kind, so this is the canonical way to address a known
// control in a known panel without text-matching (which breaks across
// localizations) or relying on panel.controls index (which can shift).
//
// Step 4 of the refactor lifted the listbox-driven panel handlers
// (Container / SaveLoad / EquipPicker) into menus_listbox.cpp; this helper
// now spans both TUs via menus_internal.h.
void* acc::menus::detail::FindControlById(void* panel, int id) {
    if (!panel) return nullptr;
    // SEH: the panel may be freed, and the controls array can carry a
    // non-null garbage tail entry (the chargen TryPartyPortrait crash).
    __try {
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
        if (!list->data || list->size <= 0) return nullptr;
        int n = list->size > 64 ? 64 : list->size;
        for (int i = 0; i < n; ++i) {
            void* c = list->data[i];
            if (!c) continue;
            int cid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(c) + kControlIdOffset);
            if (cid == id) return c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// Equip-panel slot identity via the engine's embedded control arrays —
// see the menus_internal.h comment and docs/gui-id-audit.md. Pure
// pointer arithmetic; the control pointer is never dereferenced, so an
// unrelated control simply lands outside the array (or off-stride) and
// answers -1.
namespace {
int SlotIndexInEmbeddedArray(void* panel, void* control,
                             size_t baseOff, size_t stride) {
    if (!panel || !control) return -1;
    if (!acc::off::Ok(baseOff) || !acc::off::Ok(stride) || stride == 0)
        return -1;
    uintptr_t base = reinterpret_cast<uintptr_t>(panel) + baseOff;
    uintptr_t c    = reinterpret_cast<uintptr_t>(control);
    if (c < base) return -1;
    uintptr_t delta = c - base;
    if (delta % stride != 0) return -1;
    int idx = static_cast<int>(delta / stride);
    return idx < acc::menus::detail::EquipSlotCount() ? idx : -1;
}
}  // namespace

int acc::menus::detail::EquipSlotIndexFromButton(void* panel, void* control) {
    return SlotIndexInEmbeddedArray(panel, control,
                                    kEquipPanelSlotButtonsOffset,
                                    kEquipPanelSlotButtonStride);
}

int acc::menus::detail::EquipSlotIndexFromControl(void* panel, void* control) {
    int idx = EquipSlotIndexFromButton(panel, control);
    if (idx >= 0) return idx;
    return SlotIndexInEmbeddedArray(panel, control,
                                    kEquipPanelSlotLabelsOffset,
                                    kEquipPanelSlotLabelStride);
}

// Engine-truth resolvers for the equip / workbench-upgrade picker
// members. The member address is the answer; the old .gui-id lookup stays
// only as a tripwire so a variant .gui file shows up in tester logs
// instead of silently resolving wrong (userlogs/077noequipment is exactly
// that log line's audience). acclog::Once caps it at one line per tag per
// session. guiId < 0 skips the tripwire — used where the historical id is
// known to be ambiguous on one game (K2 reuses the workbench assemble id
// inside its slot-button id set).
namespace {
void* PanelMemberWithTripwire(void* panel, size_t memberOff, int guiId,
                              const char* tag) {
    void* member = acc::off::Ptr(panel, memberOff);
    if (!member) return nullptr;
    if (guiId >= 0) {
        void* byId = acc::menus::detail::FindControlById(panel, guiId);
        if (byId && byId != member) {
            acclog::Once(tag, "GuiIdMismatch: %s member=%p but .gui id %d "
                         "resolves to %p — variant .gui file on this install",
                         tag, member, guiId, byId);
        }
    }
    return member;
}
}  // namespace

void* acc::menus::detail::EquipPanelItemsListBox(void* panel) {
    return PanelMemberWithTripwire(panel, kEquipPanelItemsListBoxOffset,
                                   kEquipLbItemsId, "Equip.LB_ITEMS");
}

void* acc::menus::detail::EquipPanelEquipButton(void* panel) {
    return PanelMemberWithTripwire(panel, kEquipPanelEquipButtonOffset,
                                   kEquipBtnEquipId, "Equip.BTN_EQUIP");
}

void* acc::menus::detail::EquipPanelBackButton(void* panel) {
    return PanelMemberWithTripwire(panel, kEquipPanelBackButtonOffset,
                                   kEquipBtnBackId, "Equip.BTN_BACK");
}

void* acc::menus::detail::UpgradePanelItemsListBox(void* panel) {
    return PanelMemberWithTripwire(panel, kUpgradePanelItemsListBoxOffset,
                                   kWorkbenchUpgradeLbItemsId,
                                   "Upgrade.LB_ITEMS");
}

void* acc::menus::detail::UpgradePanelAssembleButton(void* panel) {
    // No id tripwire: the historical id 24 is a K2 slot-button id too, so
    // an id lookup is ambiguous there by construction.
    return PanelMemberWithTripwire(panel, kUpgradePanelAssembleButtonOffset,
                                   acc::game::IsKotor2() ? -1 : 24,
                                   "Upgrade.BTN_ASSEMBLE");
}

void* acc::menus::detail::UpgradePanelBackButton(void* panel) {
    return PanelMemberWithTripwire(panel, kUpgradePanelBackButtonOffset,
                                   kWorkbenchUpgradeBtnBackId,
                                   "Upgrade.BTN_BACK");
}

// CSWGuiSaveLoad — same engine-truth resolvers (members mined from both
// games' ctors, see kSaveLoadPanel*Offset). Callers should have vetted the
// panel with IsSaveLoadPanel first; the member address is pure pointer
// arithmetic either way.
void* acc::menus::detail::SaveLoadPanelGamesListBox(void* panel) {
    return PanelMemberWithTripwire(panel, kSaveLoadPanelGamesListBoxOffset,
                                   SaveLoadLbGamesId(), "SaveLoad.LB_GAMES");
}

void* acc::menus::detail::SaveLoadPanelActionButton(void* panel) {
    return PanelMemberWithTripwire(panel, kSaveLoadPanelActionButtonOffset,
                                   SaveLoadBtnSaveLoadId(),
                                   "SaveLoad.BTN_SAVELOAD");
}

void* acc::menus::detail::SaveLoadPanelBackButton(void* panel) {
    return PanelMemberWithTripwire(panel, kSaveLoadPanelBackButtonOffset,
                                   SaveLoadBtnBackId(), "SaveLoad.BTN_BACK");
}

// K2 workbench/crafting resolvers (see menus_internal.h). The tripwire ids
// are the historical hardcoded ones, mined from K2's own gui.bif: the two
// crafting .gui files agree on the listbox/accept ids (4/5/12) but disagree
// on BTN_Examine (component_p 13, chemical_p 14), which is why the examine
// tripwire follows the panel kind. The offsets poison on K1, so every
// resolver returns nullptr there without touching the tripwire.
namespace {
// The two crafting classes lay the same controls out at different offsets;
// pick per panel kind, or return nullptr when `panel` is neither.
void* CraftMemberWithTripwire(void* panel, size_t componentOff,
                              size_t chemicalOff, int guiId,
                              const char* tag) {
    switch (acc::engine::IdentifyPanel(panel)) {
        case acc::engine::PanelKind::WorkbenchCreateItem:
            return PanelMemberWithTripwire(panel, componentOff, guiId, tag);
        case acc::engine::PanelKind::WorkbenchCreateMedical:
            return PanelMemberWithTripwire(panel, chemicalOff, guiId, tag);
        default:
            return nullptr;
    }
}
}  // namespace

void* acc::menus::detail::UpgradeSelPanelListBox(void* panel) {
    return PanelMemberWithTripwire(panel, kUpgradeSelPanelListBoxOffset,
                                   /*LB_UPGRADELIST=*/9,
                                   "UpgradeSel.LB_UPGRADELIST");
}

void* acc::menus::detail::UpgradeSelPanelUpgradeButton(void* panel) {
    return PanelMemberWithTripwire(panel, kUpgradeSelPanelUpgradeButtonOffset,
                                   /*BTN_UPGRADEITEMS=*/5,
                                   "UpgradeSel.BTN_UPGRADEITEMS");
}

void* acc::menus::detail::UpgradeSelPanelTitleLabel(void* panel) {
    return PanelMemberWithTripwire(panel, kUpgradeSelPanelTitleLabelOffset,
                                   /*LBL_TITLE=*/4, "UpgradeSel.LBL_TITLE");
}

void* acc::menus::detail::CraftPanelShopListBox(void* panel) {
    return CraftMemberWithTripwire(panel, kCraftComponentShopListBoxOffset,
                                   kCraftChemicalShopListBoxOffset,
                                   /*LB_SHOPITEMS=*/4, "Craft.LB_SHOPITEMS");
}

void* acc::menus::detail::CraftPanelInvListBox(void* panel) {
    return CraftMemberWithTripwire(panel, kCraftComponentInvListBoxOffset,
                                   kCraftChemicalInvListBoxOffset,
                                   /*LB_INVITEMS=*/5, "Craft.LB_INVITEMS");
}

void* acc::menus::detail::CraftPanelAcceptButton(void* panel) {
    return CraftMemberWithTripwire(panel, kCraftComponentAcceptButtonOffset,
                                   kCraftChemicalAcceptButtonOffset,
                                   /*BTN_Accept=*/12, "Craft.BTN_Accept");
}

void* acc::menus::detail::CraftPanelExamineButton(void* panel) {
    bool medical = acc::engine::IdentifyPanel(panel) ==
                   acc::engine::PanelKind::WorkbenchCreateMedical;
    return CraftMemberWithTripwire(panel, kCraftComponentExamineButtonOffset,
                                   kCraftChemicalExamineButtonOffset,
                                   /*BTN_Examine=*/medical ? 14 : 13,
                                   "Craft.BTN_Examine");
}

void* acc::menus::detail::CraftPanelTitleLabel(void* panel) {
    bool medical = acc::engine::IdentifyPanel(panel) ==
                   acc::engine::PanelKind::WorkbenchCreateMedical;
    return CraftMemberWithTripwire(panel, kCraftComponentTitleLabelOffset,
                                   kCraftChemicalTitleLabelOffset,
                                   /*LBL_TITLE=*/medical ? 15 : 19,
                                   "Craft.LBL_TITLE");
}

// CSWGuiPowersLevelUp resolvers (see menus_internal.h). The tripwire ids
// are the per-game historical ones mined from each game's own gui.bif —
// the two games re-number pwrlvlup's controls AND collide (K1 id 6 is
// LB_POWERS while K2 id 6 is a label; K1 id 12 is BTN_BACK while K2 id 12
// is LB_POWERS), which is exactly the fragility this audit removes.
void* acc::menus::detail::PowersPanelPowersListBox(void* panel) {
    return PanelMemberWithTripwire(panel, kPowersPanelPowersListBoxOffset,
                                   acc::game::IsKotor2() ? 12 : 6,
                                   "Powers.LB_POWERS");
}

void* acc::menus::detail::PowersPanelDescListBox(void* panel) {
    return PanelMemberWithTripwire(panel, kPowersPanelDescListBoxOffset,
                                   acc::game::IsKotor2() ? 3 : 7,
                                   "Powers.LB_DESC");
}

void* acc::menus::detail::PowersPanelPowerLabel(void* panel) {
    return PanelMemberWithTripwire(panel, kPowersPanelPowerLabelOffset,
                                   acc::game::IsKotor2() ? 4 : 8,
                                   "Powers.LBL_POWER");
}

void* acc::menus::detail::PowersPanelSubTitleLabel(void* panel) {
    return PanelMemberWithTripwire(panel, kPowersPanelSubTitleLabelOffset,
                                   /*SUB_TITLE_LBL, both games*/ 1,
                                   "Powers.SUB_TITLE_LBL");
}

void* acc::menus::detail::PowersPanelRecommendedButton(void* panel) {
    return PanelMemberWithTripwire(panel, kPowersPanelRecommendedButtonOffset,
                                   acc::game::IsKotor2() ? 11 : 9,
                                   "Powers.RECOMMENDED_BTN");
}

void* acc::menus::detail::PowersPanelAcceptButton(void* panel) {
    return PanelMemberWithTripwire(panel, kPowersPanelAcceptButtonOffset,
                                   acc::game::IsKotor2() ? 10 : 11,
                                   "Powers.ACCEPT_BTN");
}

void* acc::menus::detail::PowersPanelBackButton(void* panel) {
    return PanelMemberWithTripwire(panel, kPowersPanelBackButtonOffset,
                                   acc::game::IsKotor2() ? 9 : 12,
                                   "Powers.BACK_BTN");
}

// CSWGuiContainer resolvers (see menus_internal.h). container.gui and
// container_p.gui number these identically, so one tripwire id per
// control covers both games.
void* acc::menus::detail::ContainerPanelItemsListBox(void* panel) {
    return PanelMemberWithTripwire(panel, kContainerPanelItemsListBoxOffset,
                                   /*LB_ITEMS=*/2, "Container.LB_ITEMS");
}

void* acc::menus::detail::ContainerPanelOkButton(void* panel) {
    return PanelMemberWithTripwire(panel, kContainerPanelOkButtonOffset,
                                   /*BTN_OK=*/3, "Container.BTN_OK");
}

void* acc::menus::detail::ContainerPanelCancelButton(void* panel) {
    return PanelMemberWithTripwire(panel, kContainerPanelCancelButtonOffset,
                                   /*BTN_CANCEL=*/5, "Container.BTN_CANCEL");
}

void* acc::menus::detail::ContainerPanelGiveItemsButton(void* panel) {
    return PanelMemberWithTripwire(panel, kContainerPanelGiveItemsButtonOffset,
                                   /*BTN_GIVEITEMS=*/4,
                                   "Container.BTN_GIVEITEMS");
}

// CSWGuiFeatsCharGen resolvers (see menus_internal.h). The tripwire ids
// are each game's own ftchrgen numbering: K1 9/11/12 =
// Recommended/Accept/Back, K2 9/10/11 = Back/Accept/Recommended. K1's
// BTN_BACK id 12 lands on K2's LB_FEATS listbox, which is what made the
// old id table mis-fire on KOTOR 2 (patch-20260803-011930.log: reading a
// listbox as a button spoke a stray str_ref and Enter activated the
// listbox instead of Abbrechen).
void* acc::menus::detail::FeatsPanelAcceptButton(void* panel) {
    return PanelMemberWithTripwire(panel, kFeatsCharGenAcceptButtonOffset,
                                   acc::game::IsKotor2() ? 10 : 11,
                                   "Feats.BTN_ACCEPT");
}

void* acc::menus::detail::FeatsPanelBackButton(void* panel) {
    return PanelMemberWithTripwire(panel, kFeatsCharGenBackButtonOffset,
                                   acc::game::IsKotor2() ? 9 : 12,
                                   "Feats.BTN_BACK");
}

void* acc::menus::detail::FeatsPanelRecommendedButton(void* panel) {
    return PanelMemberWithTripwire(panel, kFeatsCharGenRecommendedButtonOffset,
                                   acc::game::IsKotor2() ? 11 : 9,
                                   "Feats.BTN_RECOMMENDED");
}

// Workbench-upgrade slot membership via the panel's embedded button run
// (one contiguous array in both games — see the kUpgradePanelSlotButtons
// note in engine_offsets_fields.h). Returns the ARRAY index (identity /
// membership only): the engine's slot semantics live in the button's
// custom_value, which is per-bank, so callers keep reading that.
int acc::menus::detail::UpgradeSlotIndexFromButton(void* panel,
                                                   void* control) {
    if (!panel || !control) return -1;
    if (!acc::off::Ok(kUpgradePanelSlotButtonsOffset) ||
        !acc::off::Ok(kUpgradePanelSlotButtonStride)) return -1;
    uintptr_t base = reinterpret_cast<uintptr_t>(panel) +
                     kUpgradePanelSlotButtonsOffset;
    uintptr_t c = reinterpret_cast<uintptr_t>(control);
    if (c < base) return -1;
    uintptr_t delta = c - base;
    if (delta % kUpgradePanelSlotButtonStride != 0) return -1;
    int idx = static_cast<int>(delta / kUpgradePanelSlotButtonStride);
    return idx < UpgradeSlotButtonCount() ? idx : -1;
}

// Detect the CSWGuiSaveLoad panel (the "Spiel laden" / "Spiel speichern"
// dialog). The panel is allocated dynamically when the user activates the
// load/save action, has no slot in CGuiInGame, and so doesn't show up via
// the slot table — but it carries its own vtable, and for a dynamically
// allocated panel the vtable IS its identity: written by the ctor, per-exe
// constant, immune to anything a content mod can put in a .gui file.
//
// This replaces the old structural probe (the {listbox id 0, buttons
// 11/12/14} quartet plus per-control vtable tightening) from before the
// gui-id audit — that shape trusted .gui-authored ids, needed a
// belt-and-braces collision defence against upgrade.gui's identical id
// quartet, and would misidentify on any install with a renumbered
// saveload.gui. HasVtable is SEH-guarded, which this call path needs
// anyway: the save-popup teardown can leave a freed SaveLoad panel
// pointer in the chain for a tick (see menus_chain.cpp).
bool acc::menus::detail::IsSaveLoadPanel(void* panel) {
    return acc::engine::HasVtable(panel, kVtableCSWGuiSaveLoad);
}

// Read the user-visible text of a CExoString-style field on a control. Returns
// nullptr if the field is empty or the c_string pointer is null. The two
// fields we care about on CSWGuiSaveLoadEntry (areaname, lastmodule) are plain
// CExoStrings populated from the save GFF — no TLK indirection, no engine
// rendering callback needed. Output is borrowed from the engine; valid until
// the entry is freed (we use it inline within a single input event).
// SEH-guarded because the caller cannot supply the frame: it uses the returned
// pointer in a snprintf, so the read has to have already succeeded or yielded
// null. The KOTOR 1 offsets are known-good, but kSaveLoadEntry*Offset is still
// Todo for KOTOR 2, where it resolves to acc::off::kUnportedOffset — this would
// then dereference far outside the mapping and take the process down instead of
// falling back to the plain row text. Found by tools/re-scripts/
// handler_chain_audit.py while clearing the KOTOR 2 handler gates.
const char* acc::menus::detail::ReadSaveLoadEntryString(void* entry, size_t fieldOffset) {
    if (!entry) return nullptr;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(entry);
        auto* str  = reinterpret_cast<CExoString*>(base + fieldOffset);
        if (!str || !str->c_string || str->length == 0) return nullptr;
        return str->c_string;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ListBoxNavResult struct + DriveListBoxSelection signature now live in
// menus_internal.h (Step 4 — listbox-driven panels lifted to
// menus_listbox.cpp). The function is still defined here because the
// dialog/container monitors call it too.
//
// `minSel` is the lowest selectable row index. Pass 0 for normal listboxes;
// pass 1 for the equip-picker LB_ITEMS where row 0 is the .gui-time
// PROTOITEM template (verified empirically — see
// docs/equip-flow-investigation.md). Existing selection_index < minSel
// (typically the engine's initial -1) lands on `minSel` regardless of
// direction, closer to user expectation than wrapping or staying silent.
//
// Returns false iff `listbox` is null or has rowCount==0; caller logs +
// ignores. On true, `out` is fully populated.
static bool DriveListBoxSelectionBody(void* listbox,
                                      acc::menus::detail::ListBoxNavOp op,
                                      short minSel,
                                      acc::menus::detail::ListBoxNavResult& out);

bool acc::menus::detail::DriveListBoxSelection(void* listbox, ListBoxNavOp op,
                                               short minSel,
                                               ListBoxNavResult& out)
{
    out = {};
    if (!listbox) return false;
    // Every field access below is an engine read and belongs under SEH like
    // the rest of this codebase's engine reads. The guard is what turns a
    // stale or unported-offset listbox pointer into a declined navigation
    // instead of a process kill — the KOTOR 2 in-world arrow-key crash
    // (2026-08-01) died on the very first read here. Callers already treat
    // false as "nothing to navigate".
    __try {
        return DriveListBoxSelectionBody(listbox, op, minSel, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = {};
        return false;
    }
}

// The body, split out so the SEH wrapper above stays free of anything needing
// unwinding (MSVC C2712) — see the CLAUDE.md note on __try and objects.
static bool DriveListBoxSelectionBody(void* listbox,
                                      acc::menus::detail::ListBoxNavOp op,
                                      short minSel,
                                      acc::menus::detail::ListBoxNavResult& out)
{
    using acc::menus::detail::ListBoxNavOp;
    auto* lbBase = reinterpret_cast<unsigned char*>(listbox);
    auto* lbList = reinterpret_cast<CExoArrayList*>(
        lbBase + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;
    if (rowCount <= 0) {
        out.rowCount = 0;
        return false;
    }

    short* selPtr = reinterpret_cast<short*>(
        lbBase + kListBoxSelectionIndexOffset);
    short* topPtr = reinterpret_cast<short*>(
        lbBase + kListBoxTopVisibleIndexOffset);
    short* ippPtr = reinterpret_cast<short*>(
        lbBase + kListBoxItemsPerPageOffset);

    short oldSel = *selPtr;
    short newSel;
    if (op == ListBoxNavOp::JumpFirst) {
        newSel = minSel;
    } else if (op == ListBoxNavOp::JumpLast) {
        newSel = (short)(rowCount - 1);
        if (newSel < minSel) newSel = minSel;
    } else if (oldSel < minSel) {
        // Pre-StepUp/StepDown anchoring: any stale -1 / out-of-range
        // selection lands on minSel regardless of direction (matches
        // user expectation better than a wrap or silent no-op).
        newSel = minSel;
    } else if (op == ListBoxNavOp::StepDown) {
        newSel = (short)(oldSel + 1);
        if (newSel >= rowCount) newSel = (short)(rowCount - 1);
    } else {  // StepUp
        newSel = (short)(oldSel - 1);
        if (newSel < minSel) newSel = minSel;
    }

    if (newSel != oldSel) {
        *selPtr = newSel;
        short ipp = *ippPtr;
        short top = *topPtr;
        if (ipp <= 0) ipp = 1;
        if (newSel < top) {
            *topPtr = newSel;
        } else if (newSel >= top + ipp) {
            *topPtr = (short)(newSel - ipp + 1);
        }
    }

    out.oldSel   = oldSel;
    out.newSel   = newSel;
    out.rowCount = rowCount;
    out.row      = (newSel >= 0 && newSel < rowCount) ? lbList->data[newSel]
                                                       : nullptr;
    return true;
}

static bool DriveListBoxSelectionEngineBody(
    void* listbox, acc::menus::detail::ListBoxNavOp op, short minSel,
    acc::menus::detail::ListBoxNavResult& out);

bool acc::menus::detail::DriveListBoxSelectionEngine(void* listbox,
                                                     ListBoxNavOp op,
                                                     short minSel,
                                                     ListBoxNavResult& out)
{
    out = {};
    if (!listbox) return false;
    // SEH for the same reason as its raw-write twin above — this one also
    // CALLS the engine's SetSelectedControl on the pointer, so a bad listbox
    // would hand a wild `this` to engine code.
    __try {
        return DriveListBoxSelectionEngineBody(listbox, op, minSel, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = {};
        return false;
    }
}

static bool DriveListBoxSelectionEngineBody(
    void* listbox, acc::menus::detail::ListBoxNavOp op, short minSel,
    acc::menus::detail::ListBoxNavResult& out)
{
    using acc::menus::detail::ListBoxNavOp;
    auto* lbBase = reinterpret_cast<unsigned char*>(listbox);
    auto* lbList = reinterpret_cast<CExoArrayList*>(
        lbBase + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;
    if (rowCount <= 0) {
        out.rowCount = 0;
        return false;
    }

    short oldSel = *reinterpret_cast<short*>(
        lbBase + kListBoxSelectionIndexOffset);

    // Identical no-wrap clamp to DriveListBoxSelection — only the commit path
    // differs (engine SetSelectedControl vs. raw field write).
    short newSel;
    if (op == ListBoxNavOp::JumpFirst) {
        newSel = minSel;
    } else if (op == ListBoxNavOp::JumpLast) {
        newSel = (short)(rowCount - 1);
        if (newSel < minSel) newSel = minSel;
    } else if (oldSel < minSel) {
        newSel = minSel;
    } else if (op == ListBoxNavOp::StepDown) {
        newSel = (short)(oldSel + 1);
        if (newSel >= rowCount) newSel = (short)(rowCount - 1);
    } else {  // StepUp
        newSel = (short)(oldSel - 1);
        if (newSel < minSel) newSel = minSel;
    }

    // Drive the engine's real selection. Re-assert even on a boundary clamp
    // (newSel == oldSel) so a frame of hover drift is corrected on the next
    // keypress; only play the select sound when the selection actually moves.
    auto setSel = reinterpret_cast<PFN_CSWGuiListBoxSetSelectedControl>(
        kAddrCSWGuiListBoxSetSelectedControl);
    setSel(listbox, newSel, newSel != oldSel ? 1 : 0);

    // Read selection_index back — SetSelectedControl is authoritative (it can
    // clamp internally), so the announce/commit see exactly what the engine set.
    short engineSel = *reinterpret_cast<short*>(
        lbBase + kListBoxSelectionIndexOffset);
    out.oldSel   = oldSel;
    out.newSel   = engineSel;
    out.rowCount = rowCount;
    out.row      = (engineSel >= 0 && engineSel < rowCount)
                       ? lbList->data[engineSel]
                       : nullptr;
    return true;
}

// Queue activation of the chain-navigable button child of `panel` whose
// .gui-time id matches `buttonId`. Mirrors the activate path used by
// chain-Enter elsewhere: queues an Activate op via menus_pending and sets
// the speech-suppress budget so the post-activation focus echo doesn't
// double-speak. The actual vtable[15].HandleInputEvent runs one tick later
// in TickPendingOps.
//
// Returns false on debounce (any op already pending) or if the button id
// isn't found on the panel; caller still consumes the keypress in those
// cases so the engine's stale activeControl can't take over.
//
// `logPrefix` is used in the diagnostic log line — pass something like
// "Container: Enter -> BTN_OK".
bool acc::menus::detail::QueueButtonByIdActivate(void* panel, int buttonId,
                                                 const char* logPrefix)
{
    if (acc::menus::pending::IsPending()) {
        acclog::Write(logPrefix, "-- op already pending; ignoring");
        return false;
    }
    void* tgt = FindControlById(panel, buttonId);
    if (!tgt) {
        acclog::Write(logPrefix, "-- target id=%d not resolved on panel=%p",
                      buttonId, panel);
        return false;
    }
    acc::menus::pending::QueueActivate(tgt);
    acclog::Write(logPrefix, "panel=%p target=%p", panel, tgt);
    return true;
}

// Same debounce + queue + log, for a target already resolved through an
// engine-member resolver (the gui-id audit's replacement for the id
// lookup above). Null target is logged, not fatal — callers still
// consume the keypress.
bool acc::menus::detail::QueueControlActivate(void* target,
                                              const char* logPrefix)
{
    if (acc::menus::pending::IsPending()) {
        acclog::Write(logPrefix, "-- op already pending; ignoring");
        return false;
    }
    if (!target) {
        acclog::Write(logPrefix, "-- target not resolved");
        return false;
    }
    acc::menus::pending::QueueActivate(target);
    acclog::Write(logPrefix, "target=%p", target);
    return true;
}

// Positional detector for chargen class-icon buttons. The 6 class icons
// are CSWGuiClassSelChar[6] starting at panel+0x6c with stride 0x25c, and
// each CSWGuiClassSelChar embeds a CSWGuiButton at offset 0 — so a focused
// class-icon control pointer lands exactly on `panel + 0x6c + i * 0x25c`
// for some 0 ≤ i < 6.
bool acc::menus::detail::IsClassSelectionIcon(void* panel, void* control) {
    if (!panel || !control) return false;
    // Guarded deref: this is the gate for menus_extract's step 9c, so it
    // runs for every control that reaches that far on EVERY panel, not just
    // chargen — and its caller's `panel` has only cleared IsPanelInManager,
    // which proves the pointer is listed, not that the object is alive.
    if (!acc::engine::HasVtable(panel, kVtableCSWGuiClassSelection)) {
        return false;
    }
    auto* panelBase = reinterpret_cast<unsigned char*>(panel);
    auto* ctrlBase  = reinterpret_cast<unsigned char*>(control);
    ptrdiff_t off = ctrlBase - panelBase;
    ptrdiff_t arrayEnd = (ptrdiff_t)(kClassSelectionsArrayOffset +
                                     kClassSelectionsCount * kClassSelCharSize);
    if (off < (ptrdiff_t)kClassSelectionsArrayOffset || off >= arrayEnd) {
        return false;
    }
    return ((off - (ptrdiff_t)kClassSelectionsArrayOffset) %
            (ptrdiff_t)kClassSelCharSize) == 0;
}

// Per-icon class-name cache for CSWGuiClassSelection. See the long
// comment in ExtractAnnounceableText step 9c for why this exists. Sized
// to hold all 6 icons of a single panel; key is (panel, icon). Keyed by
// panel as well as icon so a chargen restart on a new panel instance
// doesn't surface stale entries from the previous run. First-write
// wins — once an entry is locked, subsequent updates are ignored so the
// engine's transient class_label revert can't corrupt a settled value.
// `text` is the composed announce: the class name off class_label, plus the
// class blurb off LBL_DESC when the panel had one. Sized for both — the
// German blurbs run to about 200 characters.
struct ClassLabelCacheEntry {
    void* panel;
    void* icon;
    char  text[acc::menus::detail::kAnnounceTextMax];
};
static constexpr int kClassLabelCacheSize = 8;
static ClassLabelCacheEntry g_classLabelCache[kClassLabelCacheSize];

// The class-description label's authored text, snapshotted at panel-walk
// time. classsel.gui ships LBL_DESC with a placeholder — empty on KOTOR 1,
// the literal "This is just a test line." repeated on KOTOR 2 — and the
// engine overwrites it with the real class description on hover. Comparing
// against the snapshot is how we tell "the engine has written something" from
// "this is still the .gui default", without hard-coding either placeholder.
// One slot: only one class-selection panel exists at a time.
static void* g_classDescBaselinePanel = nullptr;
static char  g_classDescBaseline[384] = {0};

void acc::menus::detail::ClassDescBaselineCapture(void* panel, const char* text) {
    if (!panel) return;
    g_classDescBaselinePanel = panel;
    if (text) {
        strncpy_s(g_classDescBaseline, text, _TRUNCATE);
    } else {
        g_classDescBaseline[0] = '\0';
    }
}

bool acc::menus::detail::ClassDescIsEngineWritten(void* panel, const char* text) {
    if (!text || text[0] == '\0') return false;
    // No snapshot for this panel: we never saw its authored state, so we
    // cannot tell content from placeholder. Stay quiet rather than risk
    // speaking "This is just a test line." at the user.
    if (panel != g_classDescBaselinePanel) return false;
    return strcmp(text, g_classDescBaseline) != 0;
}

const char* acc::menus::detail::ClassLabelCacheLookup(void* panel, void* icon) {
    for (int i = 0; i < kClassLabelCacheSize; ++i) {
        const auto& e = g_classLabelCache[i];
        if (e.panel == panel && e.icon == icon && e.text[0] != '\0') {
            return e.text;
        }
    }
    return nullptr;
}

// Compose what the user hears for one class icon: the class name, followed by
// the blurb the engine renders beneath it, when there is one. Both call sites
// (the extractor's cache-fill and the outgoing-icon prefill) go through here
// so the cached string is identical whichever path wins the race.
//
// Reading LBL_DESC has to happen at the same instant class_label is read —
// by the time a chain step announces an icon, both labels already hold the
// NEXT icon's text.
void acc::menus::detail::ComposeClassAnnounce(void* panel, const char* name,
                                              char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return;
    outBuf[0] = '\0';
    if (!name || name[0] == '\0') return;
    strncpy_s(outBuf, bufSize, name, _TRUNCATE);
    if (!panel) return;

    void* descLabel = acc::menus::detail::FindControlById(panel,
                                                          kClassSelDescLabelId);
    if (!descLabel) return;
    char desc[acc::menus::detail::kAnnounceTextMax] = {0};
    if (!acc::engine::ReadLabelText(descLabel, desc, sizeof(desc))) return;
    if (!ClassDescIsEngineWritten(panel, desc)) return;

    char composed[acc::menus::detail::kAnnounceTextMax];
    snprintf(composed, sizeof(composed),
             acc::strings::Get(acc::strings::Id::FmtClassNameWithDescription),
             name, desc);
    strncpy_s(outBuf, bufSize, composed, _TRUNCATE);
}

void acc::menus::detail::ClassLabelCacheStore(void* panel, void* icon, const char* text) {
    if (!panel || !icon || !text || text[0] == '\0') return;
    // First-write wins for a (panel, icon) pair: bail out if already cached.
    for (int i = 0; i < kClassLabelCacheSize; ++i) {
        const auto& e = g_classLabelCache[i];
        if (e.panel == panel && e.icon == icon) return;
    }
    // Find a free slot, evicting any entry from a different panel if full.
    int slot = -1;
    for (int i = 0; i < kClassLabelCacheSize; ++i) {
        if (g_classLabelCache[i].panel == nullptr) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < kClassLabelCacheSize; ++i) {
            if (g_classLabelCache[i].panel != panel) { slot = i; break; }
        }
    }
    if (slot < 0) return;
    g_classLabelCache[slot].panel = panel;
    g_classLabelCache[slot].icon  = icon;
    strncpy_s(g_classLabelCache[slot].text, text, _TRUNCATE);
}

// Find the first CSWGuiListBox child in a panel's controls. Returns
// nullptr if none. CSWGuiDialog::replies_listbox is at child[1] in
// observed panels (preceded by the message_label at child[0]); first-
// match on IsListBox is robust enough for the dialog case.
void* acc::menus::detail::FindListBoxChild(void* panel) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 32 ? 32 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (c && IsListBox(c)) return c;
    }
    return nullptr;
}

