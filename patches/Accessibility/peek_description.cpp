#include "peek_description.h"

#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "hotkeys.h"
#include "log.h"
#include "menus_internal.h"   // kEquipBtn* slot ids
#include "tolk.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace acc::peek {

namespace {

// "Before first" sentinel for the block cursor. After OnShiftReleased
// the cursor returns here so the next Shift+Down (or Shift+Up) speaks
// block 0 — there is no "block before the first" the user could land
// on by pressing Up first thing after a release.
constexpr int kCursorReset = -1;

// Block cursor state. Persists across panel changes; the next press
// starts at 0 anyway because OnShiftReleased reset it. If the panel
// changes WITHOUT a shift release (rare — would need a panel transition
// triggered by something other than the user's hand on the keyboard)
// the cursor clamps via the row-count check on read.
int g_blockIdx = kCursorReset;

// Refresh function signatures. The engine's normal mouse-driven flow
// dispatches OnControlEntered (or equivalent) on hover, which in turn
// calls SetDescription to populate the description listbox. Our
// keyboard nav (chain step / DriveListBoxSelection) bypasses that
// chain — selection_index is mutated directly, mouse-over hit-tests
// can land on dead space, etc. — so the description listbox stays
// pinned to whatever the engine populated last.
//
// To make peek read the *focused* row's description rather than a
// stale one, we manually re-fire the refresh path at peek time. Each
// panel exposes its own refresh entry point; the registry below
// stores a small adapter that takes the panel pointer and calls into
// the right engine function.
typedef void (__thiscall* PFN_PanelOnControl)(void* panel, void* control);

// Several panel `OnControlEntered` overrides decompile to a body
// gated by `if (param_1->...->is_active != 0)`. The mouse-driven
// HandleLMouseDown sets is_active=1 on click, but our keyboard chain
// step bypasses HandleLMouseDown — the equipped inventory row is the
// canonical case where is_active stays 0 forever (never clicked in
// normal play), so the description-stage code always skipped it.
//
// Fix shape: save → force is_active=1 → call → restore. The flag is
// read by other engine paths (border rendering, focus chain, click
// activation gates), so we narrowly scope the override to the call
// window. Decompiles confirm OnControlEntered itself does not write
// to the entry's is_active. See memory:
// project_oncontrolentered_is_active_gate.
//
// `focused` is the chain target. For inventory/store/journal rows
// the row IS a CSWGuiButton-derived control (item entries embed
// CSWGuiButton at offset 0), so the chain pointer + is_active offset
// resolves correctly either way.
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

// CSWGuiInGameInventory::OnControlEntered @ 0x006b3d10. is_active gate
// applies (decompiled).
constexpr std::uintptr_t kAddrInventoryOnControlEntered = 0x006b3d10;

static void RefreshInventory(void* panel, void* focused) {
    if (!panel || !focused) return;
    CallOnControlEnteredWithActive(kAddrInventoryOnControlEntered,
                                   panel, focused);
}

// CSWGuiStore::OnControlEntered @ 0x006c0aa0. Same is_active gate as
// Inventory (decompile: `if (param_1->is_active != 0) { ... }`).
// Takes a CSWGuiControl* — the focused store row in either
// shopitems_listbox or invitems_listbox.
constexpr std::uintptr_t kAddrStoreOnControlEntered = 0x006c0aa0;

static void RefreshStore(void* panel, void* focused) {
    if (!panel || !focused) return;
    CallOnControlEnteredWithActive(kAddrStoreOnControlEntered,
                                   panel, focused);
}

// CSWGuiInGameJournal::OnControlEntered @ 0x00645100. Differs from
// Inventory/Store: the outer gate is `if (param_1 != NULL)` only —
// no is_active check (decompiled). Direct call is sufficient.
constexpr std::uintptr_t kAddrJournalOnControlEntered = 0x00645100;

static void RefreshJournal(void* panel, void* focused) {
    if (!panel || !focused) return;
    auto fn = reinterpret_cast<PFN_PanelOnControl>(
        kAddrJournalOnControlEntered);
    fn(panel, focused);
}

// Map a panel kind to where its description listbox lives within the
// typed panel struct, plus an optional refresh function called before
// peek reads. Offsets verified against the SARIF DATATYPE entries
// (docs/llm-docs/re/k1_win_gog_swkotor.exe.xml). Adding a new panel =
// one new row here; refresh is optional (nullptr leaves the engine's
// already-populated state in place).
//
// CSWGuiInGameJournal's member is named `item_description_label` in
// the SARIF but its DATATYPE is CSWGuiListBox — same shape as the
// other panels' description listboxes, just a misnamed slot. We treat
// it as a listbox (which the engine also does at every callsite).
//
// Options sub-panels (CSWGuiInGameGameplay, CSWGuiOptionsFeedback, …)
// also have description_listbox members but their PanelKind values
// aren't enumerated yet — IdentifyPanel currently maps them all to
// PanelKind::InGameOptions. Settings-tooltip support waits on either
// (a) splitting InGameOptions into per-tab kinds, or (b) reading the
// active sub-panel via the tabbed-panel detector and dispatching here
// from there.
struct PanelPeekInfo {
    acc::engine::PanelKind kind;
    std::size_t            descListBoxOffset;
    // Refresh adapter — re-stages the description for the focused
    // row before peek reads. Takes (panel, focused_chain_target);
    // each panel uses one or the other (or both). nullptr = no
    // refresh; peek reads whatever the engine already populated.
    void                 (*refresh)(void* panel, void* focused);
};

constexpr PanelPeekInfo kPanels[] = {
    { acc::engine::PanelKind::InGameInventory,  0x0844, RefreshInventory  },  // CSWGuiInGameInventory.description_listbox
    { acc::engine::PanelKind::InGameJournal,    0x01a4, RefreshJournal    },  // CSWGuiInGameJournal.item_description_label (a CSWGuiListBox)
    { acc::engine::PanelKind::InGameAbilities,  0x33bc, nullptr           },  // CSWGuiInGameAbilities.description_listbox
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

// ---------------------------------------------------------------------------
// Item-tooltip path — for listbox-driven panels whose rows are
// CSWGuiInGameItemEntry instances (item_game_object_id at +0x1c4).
//
// The chain-nav peek path above requires an inline `description_listbox`
// member that the engine pre-formats on hover/select. Two of our supported
// surfaces don't fit that shape:
//
//   * Container (loot panel, "Plündern") — no description_listbox member at
//     all (container.gui has only LBL_MESSAGE + LB_ITEMS + 3 buttons).
//   * Equip picker (CSWGuiInGameEquip in picker mode) — has
//     description_listbox at +0x33b8, but it's populated by OnItemSelected
//     which we deliberately bypass with DriveListBoxSelection (calling it
//     would commit the equip on every Shift+Down).
//
// Both panels store CLIENT-side game-object handles in their item rows
// (Container.SetContainer @ 0x6b8130 calls ServerToClientObjectId before
// SetItem; Equip.OnItemSelected @ 0x6b7920 reverses that with
// ClientToServerObjectId before the item lookup). We resolve back to a
// CSWSItem* and call CSWSItem::GetPropertyDescription directly — the
// same source the engine's description listbox formatting consumes.
//
// minSel = 1 skips the protoitem template row (Equip picker only — rows
// in LB_ITEMS render from row 0 as the prototype). Container uses 0.
struct ItemTooltipPanelInfo {
    acc::engine::PanelKind kind;
    std::size_t            itemsListBoxOffset;
    int                    minSel;
};

constexpr ItemTooltipPanelInfo kItemTooltipPanels[] = {
    { acc::engine::PanelKind::Container,   0x07f0, 0 },  // CSWGuiContainer.items_listbox
    { acc::engine::PanelKind::InGameEquip, 0x30d8, 1 },  // CSWGuiInGameEquip.items_listbox
};

// Equip-panel slot peek table. When the focused chain target is one of
// the 9 slot buttons, Shift+arrow reads the description of the item
// currently equipped in that slot (handle cached in the panel struct at
// offset itemIdOffset). Source-of-truth for the displayed character —
// the panel updates these on every BTN_CHANGE1/2 party-cycle, so the
// description matches whichever companion is on screen.
struct EquipSlotPeekInfo {
    int    cid;            // .gui control id (panel-stable across locales)
    size_t itemIdOffset;   // CSWGuiInGameEquip-relative ulong handle
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
        // Control id is the .gui-time numeric ID at +0x50 — same field
        // the slot extractor in menus_extract reads to dispatch to the
        // per-slot label table.
        cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(control) + 0x50);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    for (const auto& s : kEquipSlotPeek) {
        if (s.cid == cid) return &s;
    }
    return nullptr;
}

// Read the slot's cached server-side handle, resolve to CSWSItem*, and
// speak the same multi-line description the engine's hover-into-listbox
// path would produce. Returns true if a non-empty description was spoken,
// false if the slot is empty or the resolve fails (caller still consumes
// the key per the predictable-behaviour rule in HandleShiftArrow).
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
    // 0x7f000000 = kInvalidObjectId, the engine's "slot empty" sentinel
    // (confirmed in UpdateInventory decomp: every blank-slot branch
    // writes 0x7f000000 to the slot id field). Treat alongside 0 and
    // 0xffffffff as "no item".
    if (handle == 0 || handle == 0xffffffff || handle == 0x7f000000) {
        acclog::Write("Peek.EquipSlot",
                      "panel=%p cid=%d slot empty (handle=0x%x); silent",
                      panel, info.cid, handle);
        return false;
    }

    // Panel-cached slot ids are CLIENT-side (high bit 0x80000000 set,
    // observed live: e.g. 0x8000017a for a Zaalbar bowcaster). The
    // picker rows go through the same client→server translation path,
    // so ResolveItemFromClientHandle is the right resolver here too.
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

    tolk::Speak(text, /*interrupt=*/true);
    acclog::Write("Peek.EquipSlot",
                  "panel=%p cid=%d handle=0x%x item=%p text=\"%s\"",
                  panel, info.cid, handle, item, text);
    return true;
}

const ItemTooltipPanelInfo* LookupItemTooltipPanel(acc::engine::PanelKind k) {
    for (const auto& p : kItemTooltipPanels) {
        if (p.kind == k) return &p;
    }
    return nullptr;
}

constexpr std::size_t kItemEntryGameObjectIdOffset = 0x1c4;

// Returns true iff a description was found and spoken. False on empty
// listbox / no selection / unresolved handle / empty description text;
// callers should still consume the key in those cases to keep behaviour
// predictable (Shift+arrow shouldn't silently fire the unshifted nav).
bool HandleItemTooltip(acc::engine::PanelKind kind,
                       const ItemTooltipPanelInfo& info,
                       void* activePanel) {
    void* lb = reinterpret_cast<unsigned char*>(activePanel) +
               info.itemsListBoxOffset;
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

    tolk::Speak(text, /*interrupt=*/true);
    acclog::Write("Peek.Item",
                  "panel=%s row sel=%d/%d item=%p handle=0x%x text=\"%s\"",
                  acc::engine::PanelKindName(kind), (int)selIdx, rowCount,
                  item, clientHandle, text);
    return true;
}

// Read the visible text of one description-listbox row. Description
// rows are usually CSWGuiLabels whose c_string lives in the
// CAurGUIStringInternal at gui_string (offset 0xE4 for label,
// 0x168 for button). Multi-paragraph descriptions are sometimes
// rendered as CSWGuiButtons (with no clickable behaviour — they're
// just text containers); the engine uses both classes for rows.
//
// Try every known path so we don't silently miss text on a row with
// an unexpected subclass. Order: gui_string at label offset →
// gui_string at button offset → inline CExoString/strref/text_object
// at label offsets → same at button offsets. Returns the source tag
// for logging on success, nullptr on miss.
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
    // Central registry's OS-level shift query — same rationale as before
    // (engine-side g_engineShiftHeld latches stale on swallowed up-edges;
    // GetAsyncKeyState is authoritative).
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

bool HandleShiftArrow(int param_1, int param_2, void* activePanel,
                      void* focusedControl) {
    // Press edge only — release events fall through.
    if (param_2 == 0) return false;
    if (param_1 != kInputNavUp && param_1 != kInputNavDown) return false;
    if (!ShiftHeld()) return false;
    if (!activePanel) return false;

    auto kind = acc::engine::IdentifyPanel(activePanel);

    // Equip slot tooltip: when the focused chain target is one of the 9
    // slot buttons (BTN_INV_*) on the main equip screen, read the
    // description of the item equipped in that slot. Runs BEFORE the
    // picker-listbox path because the same panel kind (InGameEquip)
    // hosts both surfaces — the picker's items_listbox is empty until
    // the user drills into a slot, so the picker handler would otherwise
    // win and silently no-op on slot focus.
    if (kind == acc::engine::PanelKind::InGameEquip && focusedControl) {
        if (const EquipSlotPeekInfo* slotInfo =
                FindEquipSlotByControl(focusedControl)) {
            HandleEquipSlotTooltip(activePanel, *slotInfo);
            return true;
        }
    }

    // Try the item-tooltip path first (Container, Equip picker). These
    // panels don't fit the inline-description-listbox shape — see the
    // ItemTooltipPanelInfo block above. Always consume the key when the
    // panel matches so plain Up/Down nav doesn't fire on top of a
    // Shift+arrow that found no focused item.
    if (const ItemTooltipPanelInfo* itemInfo = LookupItemTooltipPanel(kind)) {
        HandleItemTooltip(kind, *itemInfo, activePanel);
        return true;
    }

    const PanelPeekInfo* info = LookupPanel(kind);
    if (!info) return false;  // unknown / unsupported panel; pass through

    // Re-stage the description for the focused row before reading.
    // SEH-guarded — OnControlEntered & friends dereference the
    // current item entry / row, which can be stale during a panel
    // teardown frame; absorbing the fault and reading whatever the
    // listbox already had is preferable to crashing.
    if (info->refresh) {
        __try {
            info->refresh(activePanel, focusedControl);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("Peek", "panel=%s refresh SEH (focused=%p); "
                          "reading stale rows",
                          acc::engine::PanelKindName(kind), focusedControl);
        }
    }

    // Description listbox is inline at a known offset within the
    // panel struct, not an entry in panel.controls. Take the address
    // and treat it as the listbox base (no FindControlById lookup
    // needed — the offset IS the location).
    void* lb = reinterpret_cast<unsigned char*>(activePanel) +
               info->descListBoxOffset;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;

    if (rowCount <= 0) {
        // Nothing to peek. Consume the key anyway so plain Up/Down
        // doesn't fire on top of a Shift+arrow the user thought went
        // somewhere — predictable behaviour beats "Shift+arrow does
        // nothing here, but the un-shifted nav fires anyway".
        acclog::Write("Peek", "panel=%s lb=%p rowCount=0; silent",
                      acc::engine::PanelKindName(kind), lb);
        return true;
    }

    // Advance the block cursor.
    bool down = (param_1 == kInputNavDown);
    int prev = g_blockIdx;
    if (g_blockIdx == kCursorReset) {
        // First press after a release: speak block 0 regardless of
        // direction — Shift+Up here means "start reading the first
        // block", not "navigate before the first".
        g_blockIdx = 0;
    } else if (down) {
        if (g_blockIdx < rowCount - 1) ++g_blockIdx;
    } else {
        if (g_blockIdx > 0) --g_blockIdx;
    }

    // Clamp against current row count (panel may have re-populated
    // with a smaller description while we were peeking).
    if (g_blockIdx >= rowCount) g_blockIdx = rowCount - 1;

    void* row = lbList->data[g_blockIdx];
    char text[1024];
    const char* src = row ? ReadRowText(row, text, sizeof(text)) : nullptr;
    if (src) {
        // Interrupt the speech queue: each Shift+arrow is a discrete
        // user-driven read; queueing would let a fast double-press
        // play both blocks back to back, which obscures which one is
        // current.
        tolk::Speak(text, /*interrupt=*/true);
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
