#include "peek_description.h"

#include "engine_area.h"      // kItemLocNameOffset
#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "hotkeys.h"
#include "log.h"
#include "menus_abilities.h"  // RefreshDetail (abilities description repaint)
#include "menus_internal.h"   // kEquipBtn* slot ids, FindControlById
#include "menus_listbox.h"    // IsEquipPickerArmed
#include "prism.h"
#include "strings.h"          // WorkbenchSlotPeekEmpty

using acc::menus::detail::FindControlById;

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace acc::peek {

namespace {

constexpr int kCursorReset = -1;

int g_blockIdx = kCursorReset;

typedef void (__thiscall* PFN_PanelOnControl)(void* panel, void* control);

// Several OnControlEntered overrides early-out on `entry->is_active != 0`.
// HandleLMouseDown sets that on click; our keyboard chain step doesn't,
// so equipped inventory rows stay at 0 forever. Save → force 1 → call →
// restore around the engine call only.
static void CallOnControlEnteredWithActive(std::uintptr_t addr,
                                           void* panel, void* focused)
{
    auto* isActivePtr = reinterpret_cast<std::uint32_t*>(
        reinterpret_cast<unsigned char*>(focused) + kControlIsActiveOffset);
    std::uint32_t saved = *isActivePtr;
    if (saved == 0) *isActivePtr = 1;

    auto fn = reinterpret_cast<PFN_PanelOnControl>(addr);
    fn(panel, focused);

    *isActivePtr = saved;
}

// CSWGuiInGameInventory::OnControlEntered — is_active gate applies.
constexpr std::uintptr_t kAddrInventoryOnControlEntered = 0x006b3d10;

static void RefreshInventory(void* panel, void* focused) {
    if (!panel || !focused) return;
    CallOnControlEnteredWithActive(kAddrInventoryOnControlEntered,
                                   panel, focused);
}

// CSWGuiStore::OnControlEntered — same is_active gate as Inventory.
constexpr std::uintptr_t kAddrStoreOnControlEntered = 0x006c0aa0;

static void RefreshStore(void* panel, void* focused) {
    if (!panel || !focused) return;
    CallOnControlEnteredWithActive(kAddrStoreOnControlEntered,
                                   panel, focused);
}

// CSWGuiInGameJournal::OnControlEntered — no is_active gate, direct call.
constexpr std::uintptr_t kAddrJournalOnControlEntered = 0x00645100;

static void RefreshJournal(void* panel, void* focused) {
    if (!panel || !focused) return;
    auto fn = reinterpret_cast<PFN_PanelOnControl>(
        kAddrJournalOnControlEntered);
    fn(panel, focused);
}

// Repaint LB_DESC for the focused entry before Shift+Up/Down pages it. Routes
// through the abilities module's OnEnterSkill path (NOT OnAbilitySelectionChanged,
// which is mouse-coordinate-driven and corrupts the stack via a ret-4 mismatch).
// `focused` is unused — the selection lives on the ability_listbox.
static void RefreshAbilities(void* panel, void* /*focused*/) {
    if (!panel) return;
    acc::menus::abilities::RefreshDetail(panel);
}

// Panel kind → description-listbox offset + optional refresh adapter.
// Adding a panel = one row. refresh=nullptr reads whatever the engine
// already populated.
//
// Journal's slot is named `item_description_label` in SARIF but its
// DATATYPE is CSWGuiListBox — misnamed, treat as listbox.
//
// Options sub-panels also have description_listbox members but
// IdentifyPanel currently lumps them all under InGameOptions —
// settings-tooltip support waits on per-tab kind discrimination.
struct PanelPeekInfo {
    acc::engine::PanelKind kind;
    std::size_t            descListBoxOffset;
    void                 (*refresh)(void* panel, void* focused);  // optional
};

constexpr PanelPeekInfo kPanels[] = {
    { acc::engine::PanelKind::InGameInventory,  0x0844, RefreshInventory  },  // CSWGuiInGameInventory.description_listbox
    { acc::engine::PanelKind::InGameJournal,    0x01a4, RefreshJournal    },  // CSWGuiInGameJournal.item_description_label (a CSWGuiListBox)
    { acc::engine::PanelKind::InGameAbilities,  kAbilitiesDescListBoxOffset, RefreshAbilities },  // LB_DESC (0x33bc); refresh repaints it for the focused entry
    { acc::engine::PanelKind::Store,            0x1a40, RefreshStore      },  // CSWGuiStore.description_listbox
    // CSWGuiInGameEquip is handled via the item-tooltip path below: its
    // arrow-key nav goes through DriveListBoxSelection which bypasses the
    // engine's onSelectionChanged callback, so description_listbox at
    // +0x33b8 stays stale. Instead we derive the focused row from
    // items_listbox.selection_index and read the item description directly.
};

const PanelPeekInfo* LookupPanel(acc::engine::PanelKind k) {
    for (const auto& p : kPanels) {
        if (p.kind == k) return &p;
    }
    return nullptr;
}

// Item-tooltip path for listbox-driven panels with no inline
// description_listbox (Container, Equip picker) or where calling
// OnItemSelected would commit the action (Equip — picker resolves on
// the user's actual click). Resolves CSWGuiInGameItemEntry rows
// (item_game_object_id at +0x1c4) via the same client→server handle
// path the engine uses, then reads CSWSItem::GetPropertyDescription.
//
// minSel=1 skips the Equip-picker's protoitem template row;
// Container uses 0.
//
// findLb: stable inline offset for type-known panels, FindControlById
// for the heap-allocated workbench listboxes.
struct ItemTooltipPanelInfo {
    acc::engine::PanelKind kind;
    void*                (*findLb)(void* panel);
    int                    minSel;
};

void* ContainerFindLb(void* panel) {
    // CSWGuiContainer.items_listbox at +0x07f0.
    return reinterpret_cast<unsigned char*>(panel) + 0x07f0;
}

void* InGameEquipFindLb(void* panel) {
    // CSWGuiInGameEquip.items_listbox at +0x30d8.
    return reinterpret_cast<unsigned char*>(panel) + 0x30d8;
}

void* WorkbenchItemsFindLb(void* panel) {
    return FindControlById(panel, 0);
}

void* WorkbenchUpgradeFindLb(void* panel) {
    return FindControlById(panel, 0);
}

void* QuestItemFindLb(void* panel) {
    // CSWGuiQuestItem.LB_ITEMS at +0x488 (the plot/quest-item list).
    return reinterpret_cast<unsigned char*>(panel) + 0x488;
}

constexpr ItemTooltipPanelInfo kItemTooltipPanels[] = {
    { acc::engine::PanelKind::Container,        ContainerFindLb,        0 },
    { acc::engine::PanelKind::InGameEquip,      InGameEquipFindLb,      1 },
    { acc::engine::PanelKind::WorkbenchItems,   WorkbenchItemsFindLb,   0 },
    { acc::engine::PanelKind::WorkbenchUpgrade, WorkbenchUpgradeFindLb, 0 },
    { acc::engine::PanelKind::InGameQuestItems, QuestItemFindLb,        0 },
};

// Per-slot peek for the 9 equip buttons. itemIdOffset is the panel-
// cached client handle that the engine rewrites on every party-cycle, so
// the description always matches the displayed character.
struct EquipSlotPeekInfo {
    int    cid;            // .gui control id, locale-stable
    size_t itemIdOffset;
};

constexpr EquipSlotPeekInfo kEquipSlotPeek[] = {
    { kEquipBtnHeadId,    kEquipPanelHeadIdOffset         },
    { kEquipBtnImplantId, kEquipPanelImplantIdOffset      },
    { kEquipBtnBodyId,    kEquipPanelArmorIdOffset        },
    { kEquipBtnArmLId,    kEquipPanelLeftArmbandIdOffset  },
    { kEquipBtnArmRId,    kEquipPanelRightArmbandIdOffset },
    { kEquipBtnWeapLId,   kEquipPanelLeftWeaponIdOffset   },
    { kEquipBtnWeapRId,   kEquipPanelRightWeaponIdOffset  },
    { kEquipBtnBeltId,    kEquipPanelBeltIdOffset         },
    { kEquipBtnHandsId,   kEquipPanelGlovesIdOffset       },
};

const EquipSlotPeekInfo* FindEquipSlotByControl(void* control) {
    if (!control) return nullptr;
    int cid = 0;
    __try {
        cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(control) + kControlIdOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    for (const auto& s : kEquipSlotPeek) {
        if (s.cid == cid) return &s;
    }
    return nullptr;
}

// True on a non-empty description spoken; false on empty slot / unresolved
// item. Caller still consumes the key (predictable-behaviour rule).
bool HandleEquipSlotTooltip(void* panel, const EquipSlotPeekInfo& info) {
    if (!panel) return false;
    uint32_t handle = 0;
    __try {
        handle = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(panel) + info.itemIdOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Peek.EquipSlot",
                      "panel=%p offset=0x%x SEH reading slot id",
                      panel, (unsigned)info.itemIdOffset);
        return false;
    }
    // 0x7f000000 = kInvalidObjectId (engine's "slot empty" sentinel).
    if (handle == 0 || handle == 0xffffffff || handle == 0x7f000000) {
        acclog::Write("Peek.EquipSlot",
                      "panel=%p cid=%d slot empty (handle=0x%x); silent",
                      panel, info.cid, handle);
        return false;
    }

    // Panel-cached slot ids are client-side (high bit 0x80000000 set).
    void* item = acc::engine::ResolveItemFromClientHandle(handle);
    if (!item) {
        acclog::Write("Peek.EquipSlot",
                      "panel=%p cid=%d handle=0x%x; item not resolvable",
                      panel, info.cid, handle);
        return false;
    }

    char text[4096];
    if (!acc::engine::ReadItemPropertyDescription(item, text, sizeof(text))) {
        acclog::Write("Peek.EquipSlot",
                      "panel=%p cid=%d item=%p empty description",
                      panel, info.cid, item);
        return false;
    }

    prism::Speak(text, /*interrupt=*/true);
    acclog::Write("Peek.EquipSlot",
                  "panel=%p cid=%d handle=0x%x item=%p text=\"%s\"",
                  panel, info.cid, handle, item, text);
    return true;
}

// Shift+arrow on a workbench upgrade slot button (upgrade.gui IDs 12..18):
// speak the installed mod's full property description ("what the slot does
// for your item"), or announce the slot is empty. The installed mod lives at
// CSWGuiUpgrade.field35_0x2f74[slot_btn.custom_value] (engine-constructed from
// the mod template — see OnPanelAdded). Mirrors HandleEquipSlotTooltip; caller
// consumes the key regardless of return so behaviour is predictable.
bool HandleWorkbenchSlotTooltip(void* panel, void* control) {
    void* installed =
        acc::engine::GetWorkbenchSlotInstalledItem(panel, control);
    if (!installed) {
        prism::Speak(
            acc::strings::Get(acc::strings::Id::WorkbenchSlotPeekEmpty),
            /*interrupt=*/true);
        acclog::Write("Peek.Workbench",
                      "panel=%p control=%p slot empty", panel, control);
        return true;
    }

    char text[4096];
    if (acc::engine::ReadItemPropertyDescription(installed, text,
                                                 sizeof(text))) {
        prism::Speak(text, /*interrupt=*/true);
        acclog::Write("Peek.Workbench",
                      "panel=%p control=%p item=%p text=\"%s\"",
                      panel, control, installed, text);
        return true;
    }

    // Occupied but no property text — fall back to the mod's name so the peek
    // still says what's installed rather than going silent.
    if (acc::engine::ExtractTextOrStrRef(installed, kItemLocNameOffset,
                                         kItemLocNameOffset + 4, text,
                                         sizeof(text)) && text[0]) {
        prism::Speak(text, /*interrupt=*/true);
        acclog::Write("Peek.Workbench",
                      "panel=%p control=%p item=%p name-fallback=\"%s\"",
                      panel, control, installed, text);
    } else {
        acclog::Write("Peek.Workbench",
                      "panel=%p control=%p item=%p no description/name",
                      panel, control, installed);
    }
    return true;
}

const ItemTooltipPanelInfo* LookupItemTooltipPanel(acc::engine::PanelKind k) {
    for (const auto& p : kItemTooltipPanels) {
        if (p.kind == k) return &p;
    }
    return nullptr;
}

constexpr std::size_t kItemEntryGameObjectIdOffset = 0x1c4;

// Caller consumes the key regardless of return (predictable behaviour).
bool HandleItemTooltip(acc::engine::PanelKind kind,
                       const ItemTooltipPanelInfo& info,
                       void* activePanel) {
    void* lb = info.findLb ? info.findLb(activePanel) : nullptr;
    if (!lb) {
        acclog::Write("Peek.Item",
                      "panel=%s no listbox; silent",
                      acc::engine::PanelKindName(kind));
        return false;
    }
    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    int rowCount = 0;
    short selIdx = -1;
    __try {
        rowCount = (lbList && lbList->data) ? lbList->size : 0;
        selIdx = *reinterpret_cast<short*>(
            reinterpret_cast<unsigned char*>(lb) +
            kListBoxSelectionIndexOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Peek.Item",
                      "panel=%s lb=%p SEH reading listbox state",
                      acc::engine::PanelKindName(kind), lb);
        return false;
    }

    if (rowCount <= 0 || selIdx < info.minSel || selIdx >= rowCount) {
        acclog::Write("Peek.Item",
                      "panel=%s lb=%p no focused item (sel=%d rows=%d "
                      "minSel=%d); silent",
                      acc::engine::PanelKindName(kind), lb,
                      (int)selIdx, rowCount, info.minSel);
        return false;
    }

    void* row = nullptr;
    uint32_t clientHandle = 0;
    __try {
        row = lbList->data[selIdx];
        if (row) {
            clientHandle = *reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(row) +
                kItemEntryGameObjectIdOffset);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Peek.Item",
                      "panel=%s row=%p SEH reading item handle",
                      acc::engine::PanelKindName(kind), row);
        return false;
    }

    void* item = acc::engine::ResolveItemFromClientHandle(clientHandle);
    if (!item) {
        acclog::Write("Peek.Item",
                      "panel=%s row=%p handle=0x%x; item not resolvable",
                      acc::engine::PanelKindName(kind), row, clientHandle);
        return false;
    }

    char text[4096];
    if (!acc::engine::ReadItemPropertyDescription(item, text, sizeof(text))) {
        acclog::Write("Peek.Item",
                      "panel=%s item=%p empty description",
                      acc::engine::PanelKindName(kind), item);
        return false;
    }

    prism::Speak(text, /*interrupt=*/true);
    acclog::Write("Peek.Item",
                  "panel=%s row sel=%d/%d item=%p handle=0x%x text=\"%s\"",
                  acc::engine::PanelKindName(kind), (int)selIdx, rowCount,
                  item, clientHandle, text);
    return true;
}

// Rows can be CSWGuiLabel OR CSWGuiButton (engine uses both for text-only
// containers). Try every known text path. Returns source tag for logging.
const char* ReadRowText(void* row, char* outBuf, std::size_t bufSize) {
    if (!row || !outBuf || bufSize < 2) return nullptr;

    if (acc::engine::ReadGuiString(row, kLabelGuiStringPtrOffset,
                                   outBuf, bufSize)) {
        return "label-gui";
    }
    if (acc::engine::ReadGuiString(row, kButtonGuiStringPtrOffset,
                                   outBuf, bufSize)) {
        return "button-gui";
    }
    if (acc::engine::ExtractTextOrStrRefIndirect(
            row, kLabelTextOffset, kLabelStrRefOffset,
            kLabelTextObjectOffset, outBuf, bufSize)) {
        return "label-cexo";
    }
    if (acc::engine::ExtractTextOrStrRefIndirect(
            row, kButtonTextOffset, kButtonStrRefOffset,
            kButtonTextObjectOffset, outBuf, bufSize)) {
        return "button-cexo";
    }
    return nullptr;
}

bool ShiftHeld() {
    // OS-level query; the engine-side flag can latch stale on swallowed
    // up-edges.
    return acc::hotkeys::ShiftHeld();
}

}  // namespace

void OnShiftReleased() {
    if (g_blockIdx != kCursorReset) {
        acclog::Write("Peek", "shift release; reset block cursor (was %d)",
                      g_blockIdx);
    }
    g_blockIdx = kCursorReset;
}

bool SpeakItemRowDescription(void* row) {
    if (!row) return false;
    uint32_t clientHandle = 0;
    __try {
        clientHandle = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(row) +
            kItemEntryGameObjectIdOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Peek.Item", "row=%p SEH reading item handle", row);
        return false;
    }
    void* item = acc::engine::ResolveItemFromClientHandle(clientHandle);
    if (!item) {
        acclog::Write("Peek.Item",
                      "row=%p handle=0x%x; item not resolvable",
                      row, clientHandle);
        return false;
    }
    char text[4096];
    if (!acc::engine::ReadItemPropertyDescription(item, text, sizeof(text))) {
        acclog::Write("Peek.Item", "row=%p item=%p empty description",
                      row, item);
        return false;
    }
    prism::Speak(text, /*interrupt=*/true);
    acclog::Write("Peek.Item", "row=%p item=%p text=\"%s\"", row, item, text);
    return true;
}

bool HandleShiftArrow(int param_1, int param_2, void* activePanel,
                      void* focusedControl) {
    // Press edge only — release events fall through.
    if (param_2 == 0) return false;
    if (param_1 != kInputNavUp && param_1 != kInputNavDown) return false;
    if (!ShiftHeld()) return false;
    if (!activePanel) return false;

    auto kind = acc::engine::IdentifyPanel(activePanel);

    // Slot path runs before picker-listbox because InGameEquip hosts
    // both surfaces; the picker is empty until a slot is drilled into.
    //
    // Skip the slot path while the picker is armed — chain focus stays
    // on the originating slot button while picker rows are driven by
    // DriveListBoxSelection, so without this gate FindEquipSlotByControl
    // would match the slot, find the handle at 0x7f000000 (engine moves
    // the item out for the swap), and silently early-out instead of
    // letting the item-tooltip path read the row the user is sitting on.
    if (kind == acc::engine::PanelKind::InGameEquip && focusedControl &&
        !acc::menus::listbox::IsEquipPickerArmed()) {
        if (const EquipSlotPeekInfo* slotInfo =
                FindEquipSlotByControl(focusedControl)) {
            HandleEquipSlotTooltip(activePanel, *slotInfo);
            return true;
        }
    }

    // Workbench upgrade slot button (upgrade.gui IDs 12..18): speak the
    // installed mod's description, or "empty". Runs before the item-tooltip
    // path because that path targets LB_ITEMS — the mod picker shown only
    // after a slot is drilled into. While focus sits on a slot button the
    // list is empty, so without this branch Shift+arrow would say nothing.
    // When the user has drilled into a slot, focus moves to LB_ITEMS rows
    // (id != 12..18) and this branch falls through to the picker path.
    if (kind == acc::engine::PanelKind::WorkbenchUpgrade && focusedControl) {
        int cid = -1;
        __try {
            cid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(focusedControl) +
                kControlIdOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            cid = -1;
        }
        if (cid >= 12 && cid <= 18) {
            HandleWorkbenchSlotTooltip(activePanel, focusedControl);
            return true;
        }
    }

    // Item-tooltip path (Container, Equip picker, Workbench listboxes).
    if (const ItemTooltipPanelInfo* itemInfo = LookupItemTooltipPanel(kind)) {
        HandleItemTooltip(kind, *itemInfo, activePanel);
        return true;
    }

    const PanelPeekInfo* info = LookupPanel(kind);
    if (!info) {
        // Generic fallback: focused control's own .gui tooltip strref.
        // Covers LevelUp / PartySelection / options widgets / etc.
        if (focusedControl) {
            char tip[8192];
            if (acc::engine::ReadControlTooltip(focusedControl, tip,
                                                sizeof(tip))) {
                prism::Speak(tip, /*interrupt=*/true);
                acclog::Write("Peek.Control",
                              "panel=%s control=%p tooltip=\"%s\"",
                              acc::engine::PanelKindName(kind),
                              focusedControl, tip);
                return true;
            }
            acclog::Write("Peek.Control",
                          "panel=%s control=%p no tooltip; pass through",
                          acc::engine::PanelKindName(kind), focusedControl);
        }
        return false;  // unknown / no tooltip — pass through to plain nav
    }

    // Re-stage description for the focused row. SEH-guarded against
    // stale entries during panel teardown.
    if (info->refresh) {
        __try {
            info->refresh(activePanel, focusedControl);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("Peek", "panel=%s refresh SEH (focused=%p); "
                          "reading stale rows",
                          acc::engine::PanelKindName(kind), focusedControl);
        }
    }

    // Inline at known offset; address is the listbox base, no lookup.
    void* lb = reinterpret_cast<unsigned char*>(activePanel) +
               info->descListBoxOffset;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;

    if (rowCount <= 0) {
        // Consume the key anyway (predictable-behaviour rule).
        acclog::Write("Peek", "panel=%s lb=%p rowCount=0; silent",
                      acc::engine::PanelKindName(kind), lb);
        return true;
    }

    bool down = (param_1 == kInputNavDown);
    int prev = g_blockIdx;
    if (g_blockIdx == kCursorReset) {
        // First press after a release speaks block 0 in either direction.
        g_blockIdx = 0;
    } else if (down) {
        if (g_blockIdx < rowCount - 1) ++g_blockIdx;
    } else {
        if (g_blockIdx > 0) --g_blockIdx;
    }

    // Clamp — panel may have re-populated with fewer rows.
    if (g_blockIdx >= rowCount) g_blockIdx = rowCount - 1;

    void* row = lbList->data[g_blockIdx];
    char text[8192];
    const char* src = row ? ReadRowText(row, text, sizeof(text)) : nullptr;
    if (src) {
        // interrupt=true: each press is a discrete read; fast double-
        // press should land on the latest block, not queue both.
        prism::Speak(text, /*interrupt=*/true);
        acclog::Write("Peek", "panel=%s block %d/%d (was %d) src=%s text=\"%s\"",
                      acc::engine::PanelKindName(kind),
                      g_blockIdx, rowCount, prev, src, text);
    } else {
        acclog::Write("Peek", "panel=%s block %d/%d (was %d) row=%p no-text; silent",
                      acc::engine::PanelKindName(kind),
                      g_blockIdx, rowCount, prev, row);
    }

    return true;
}

}  // namespace acc::peek
