#include "peek_description.h"

#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "hotkeys.h"
#include "log.h"
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
    { acc::engine::PanelKind::InGameEquip,      0x33b8, nullptr           },  // CSWGuiInGameEquip.description_listbox
    { acc::engine::PanelKind::InGameInventory,  0x0844, RefreshInventory  },  // CSWGuiInGameInventory.description_listbox
    { acc::engine::PanelKind::InGameJournal,    0x01a4, RefreshJournal    },  // CSWGuiInGameJournal.item_description_label (a CSWGuiListBox)
    { acc::engine::PanelKind::InGameAbilities,  0x33bc, nullptr           },  // CSWGuiInGameAbilities.description_listbox
    { acc::engine::PanelKind::Store,            0x1a40, RefreshStore      },  // CSWGuiStore.description_listbox
};

const PanelPeekInfo* LookupPanel(acc::engine::PanelKind k) {
    for (const auto& p : kPanels) {
        if (p.kind == k) return &p;
    }
    return nullptr;
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
