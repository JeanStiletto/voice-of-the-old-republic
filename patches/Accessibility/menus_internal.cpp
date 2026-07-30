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
#include "menus_pending.h"   // QueueButtonByIdActivate defers the activate
#include "engine_offsets.h"
#include "engine_panels.h"   // HasVtable
#include "engine_reads.h"
#include "engine_rebase.h"

using namespace acc::engine;

// CSWGuiSaveLoad control IDs from saveload.gui (verified against chain logs:
// patch-20260505-160124.log lines 45-65 et al). Stable across save and load
// contexts — both render through the same .gui file.
//
//   id=0   games_listbox       (CSWGuiListBox; rows are CSWGuiSaveLoadEntry)
//   id=11  delete_button       ("L\xF6schen" / "Delete")
//   id=12  back_button         ("Abbrechen" / "Cancel")
//   id=14  saveload_button     ("Laden" / "Save" / etc.)
constexpr int kSaveLoadLbGamesId    =  0;
constexpr int kSaveLoadBtnDeleteId  = 11;
constexpr int kSaveLoadBtnBackId    = 12;
constexpr int kSaveLoadBtnSaveLoadId = 14;

// Center pixel of a control's hit area. Returns false on null control or
// degenerate extent (zero/negative width/height — sometimes seen on hidden
// panels and templated control prototypes).
bool acc::menus::detail::GetControlCenter(void* control, int& outCx, int& outCy) {
    if (!control) return false;
    auto* ext = reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(control) + kControlExtentOffset);
    int width  = ext[2];
    int height = ext[3];
    if (width <= 0 || height <= 0) return false;
    outCx = ext[0] + width  / 2;
    outCy = ext[1] + height / 2;
    return true;
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
    return nullptr;
}

// Detect the CSWGuiSaveLoad panel (the "Spiel laden" / "Spiel speichern"
// dialog). The panel is allocated dynamically when the user activates the
// load/save action, has no slot in CGuiInGame, and so doesn't show up via
// IdentifyPanel.
//
// We classify by a *structural* signature — the four .gui-time control IDs
// the saveload.gui resource declares:
//
//   - id=0  games_listbox       (CSWGuiListBox)
//   - id=11 delete_button       (CSWGuiButton)
//   - id=12 back_button         (CSWGuiButton)
//   - id=14 saveload_button     (CSWGuiButton)
//
// .gui-time IDs are baked into the resource at build time and identical
// between the save and load contexts (both render through the same
// CSWGuiSaveLoad layout). They're language-independent — only the rendered
// label text is localised, not the IDs — so this matches every locale
// without enumerating titles. The combined four-ID tuple is specific
// enough that no other panel we've observed in the chain logs collides.
//
// We deliberately do NOT generalise this to "any panel that has a listbox":
// listbox row semantics vary. Options sub-dialogs render settings as
// listbox-row buttons whose onClick toggles state directly, dialog replies
// have engine-bound arrow keys that mutate selection_index already, and
// description listboxes are read-only. The select-then-confirm-via-button
// pattern is shared by Container, Equip-picker, and SaveLoad — all three
// kinds detected per-panel today.
bool acc::menus::detail::IsSaveLoadPanel(void* panel) {
    if (!panel) return false;

    void* lb = FindControlById(panel, kSaveLoadLbGamesId);
    if (!lb) return false;
    void** lbVtable = *reinterpret_cast<void***>(lb);
    if (reinterpret_cast<uintptr_t>(lbVtable) != kVtableListBox) return false;

    // Tighten: require IDs 11/12/14 to all be Buttons. Mirrors the
    // engine-layer IsSaveLoadStructural — the workbench upgrade panel
    // (upgrade.gui) coincidentally has the same {0, 11, 12, 14} ID
    // quartet but its ID 11 is a LabelHilight (LBL_UPGRADE44), not a
    // Button. Without the vtable check the SaveLoad listbox-spec handler
    // hijacks all input on the workbench upgrade panel (Enter dispatches
    // ID 14 = BTN_UPGRADE33, Esc dispatches ID 12 = BTN_UPGRADE31),
    // breaking navigation entirely.
    auto isBtn = [](void* c) -> bool {
        if (!c) return false;
        __try {
            void** vt = *reinterpret_cast<void***>(c);
            return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiButton;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    };
    return isBtn(FindControlById(panel, kSaveLoadBtnSaveLoadId)) &&
           isBtn(FindControlById(panel, kSaveLoadBtnBackId))     &&
           isBtn(FindControlById(panel, kSaveLoadBtnDeleteId));
}

// Read the user-visible text of a CExoString-style field on a control. Returns
// nullptr if the field is empty or the c_string pointer is null. The two
// fields we care about on CSWGuiSaveLoadEntry (areaname, lastmodule) are plain
// CExoStrings populated from the save GFF — no TLK indirection, no engine
// rendering callback needed. Output is borrowed from the engine; valid until
// the entry is freed (we use it inline within a single input event).
const char* acc::menus::detail::ReadSaveLoadEntryString(void* entry, size_t fieldOffset) {
    if (!entry) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(entry);
    auto* str  = reinterpret_cast<CExoString*>(base + fieldOffset);
    if (!str || !str->c_string || str->length == 0) return nullptr;
    return str->c_string;
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
bool acc::menus::detail::DriveListBoxSelection(void* listbox, ListBoxNavOp op,
                                               short minSel,
                                               ListBoxNavResult& out)
{
    out = {};
    if (!listbox) return false;

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

bool acc::menus::detail::DriveListBoxSelectionEngine(void* listbox,
                                                     ListBoxNavOp op,
                                                     short minSel,
                                                     ListBoxNavResult& out)
{
    out = {};
    if (!listbox) return false;

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
struct ClassLabelCacheEntry {
    void* panel;
    void* icon;
    char  text[64];
};
static constexpr int kClassLabelCacheSize = 8;
static ClassLabelCacheEntry g_classLabelCache[kClassLabelCacheSize];

const char* acc::menus::detail::ClassLabelCacheLookup(void* panel, void* icon) {
    for (int i = 0; i < kClassLabelCacheSize; ++i) {
        const auto& e = g_classLabelCache[i];
        if (e.panel == panel && e.icon == icon && e.text[0] != '\0') {
            return e.text;
        }
    }
    return nullptr;
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

