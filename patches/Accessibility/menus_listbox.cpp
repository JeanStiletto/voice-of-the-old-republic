// KOTOR Accessibility — listbox-driven panel input dispatcher.
//
// Step 4 of the menus.cpp refactor. See menus_listbox.h for the
// motivation. This file holds:
//
//   * The ListBoxPanelSpec struct (private) — one entry per panel kind.
//   * Three spec entries: Container, SaveLoad, EquipPicker. Each carries
//     ~5-6 small static callbacks that capture its quirks (announce
//     format, button IDs, custom Enter dispatch, etc.).
//   * The dispatcher TryHandleInput that walks the table.
//   * The EquipPicker armed/panel state + accessors (state ownership
//     follows the handler that primarily uses it; menus.cpp's two outside
//     touch sites — slot-arm + monitor-disarm — go through accessors).

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "menus_listbox.h"

#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_extract.h"
#include "menus_internal.h"
#include "menus_pending.h"
#include "strings.h"
#include "tolk.h"

using namespace acc::engine;  // IdentifyPanel, PanelKind, kInput*, etc.

using acc::menus::detail::FindControlById;
using acc::menus::detail::FindListBoxChild;
using acc::menus::detail::IsSaveLoadPanel;
using acc::menus::detail::ReadSaveLoadEntryString;
using acc::menus::detail::DriveListBoxSelection;
using acc::menus::detail::ListBoxNavResult;
using acc::menus::detail::QueueButtonByIdActivate;

// .gui-time IDs (Container loot panel + SaveLoad dialog) — see menus.cpp
// for the full table. Duplicated locally because the spec entries need
// them at file scope and menus.cpp's copies are static. The IDs are
// .gui-resource-baked, stable across localizations.
namespace {
constexpr int kContainerBtnOkId      = 3;
constexpr int kContainerBtnCancelId  = 5;

constexpr int kSaveLoadLbGamesId     =  0;
constexpr int kSaveLoadBtnBackId     = 12;
constexpr int kSaveLoadBtnSaveLoadId = 14;
}

// Speech-suppress budget shared with menus.cpp. Defined there; we set it
// from the EquipPicker commit callback so the post-activation focus echo
// doesn't double-speak. Mirrors how menus.cpp's other queueing sites
// behave.
extern int g_navSpeechSuppressBudget;

namespace acc::menus::listbox {

// ============================================================================
// EquipPicker zone state. Single-panel arming flag + the panel pointer
// it's bound to. State ownership is here because the picker handler is
// the primary mutator; menus.cpp's two outside sites (slot-Enter arm,
// monitor-disarm) go through accessors.
// ============================================================================

namespace {
bool  s_equipPickerActive = false;
void* s_equipPickerPanel  = nullptr;
}  // namespace

bool  IsEquipPickerArmed() { return s_equipPickerActive; }
void* EquipPickerPanel()   { return s_equipPickerPanel; }

void ArmEquipPicker(void* panel) {
    s_equipPickerActive = true;
    s_equipPickerPanel  = panel;
}

void DisarmEquipPicker() {
    s_equipPickerActive = false;
    s_equipPickerPanel  = nullptr;
}

// ============================================================================
// Spec struct. One value per panel kind. All variation flows through the
// callback fields — the dispatcher itself is panel-agnostic.
// ============================================================================

namespace {

struct ListBoxPanelSpec {
    // Diagnostic tag for log lines ("Container" / "SaveLoad" / "EquipPicker").
    const char* logTag;

    // True if this spec applies to `panel`. Panel-kind comparison or a
    // structural matcher (SaveLoad uses control-ID signature).
    bool (*matches)(void* panel);

    // True if the spec is currently armed. nullptr = always armed when
    // matches. EquipPicker uses the s_equipPickerActive flag.
    bool (*armed)();

    // Optional pre-dispatch hook to clean up stale state when the panel
    // pointer drifted (re-open). EquipPicker disarms if armed against an
    // older panel pointer. nullptr = no-op.
    void (*resetStale)(void* panel);

    // Locate the listbox we drive. Container uses FindListBoxChild,
    // SaveLoad/EquipPicker use FindControlById with a known +0x50 ID.
    void* (*findListBox)(void* panel);

    // First valid selection index. 0 normally; 1 for EquipPicker (skips
    // the row 0 protoitem template).
    int minSel;

    // Format + speak the focused row. Called on every successful Up/Down
    // step (and on no-op clamps so the user gets feedback at boundaries).
    // Callback decides internally whether to actually speak — Container/
    // EquipPicker speak only on clamp (per-tick monitor handles normal
    // moves), SaveLoad speaks on every step (no monitor watches it).
    void (*announce)(void* lb, const ListBoxNavResult& r);

    // Optional extra text appended to the standard nav log line.
    // SaveLoad uses it to log planet/area; others leave it nullptr.
    void (*logExtra)(char* out, size_t outN, const ListBoxNavResult& r);

    // Handle Enter. Returns true if consumed. Called only when armed and
    // the key is Enter1/Enter2.
    bool (*onEnter)(void* panel);

    // Handle Esc. Returns true if consumed. nullptr = don't handle Esc.
    bool (*onEsc)(void* panel);

    // After dispatch, if the event was NOT consumed, should the caller
    // still return (skipping all subsequent handlers)?
    //
    //   * Container = true: arrow Left/Right and other unhandled keys on
    //     the loot panel should NOT fall through to chain navigation.
    //   * SaveLoad = false: unhandled keys should fall through so chain
    //     nav can still reach the Delete button.
    //   * EquipPicker = false: unhandled keys fall through to slot-zone
    //     navigation (the rest of OnHandleInputEvent).
    bool alwaysReturnFromHandler;
};

// ============================================================================
// Container — loot chest / corpse panel ("Plündern").
// ============================================================================

bool ContainerMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::Container;
}

void* ContainerFindLb(void* p) {
    return FindListBoxChild(p);
}

// Inline announce only on a no-op clamp (boundary). Normal moves are
// caught by MonitorContainerSelection on the next tick — see menus.cpp.
void ContainerAnnounce(void* /*lb*/, const ListBoxNavResult& r) {
    if (r.newSel != r.oldSel) return;
    if (!r.row) return;
    char rowText[256];
    if (acc::menus::extract::FromControl(r.row, rowText, sizeof(rowText))) {
        char msg[320];
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
                 rowText, r.newSel + 1, r.rowCount);
        tolk::Speak(msg, /*interrupt=*/false);
    }
}

bool ContainerOnEnter(void* panel) {
    // Container per-item take is currently UNRESOLVED. Both tested
    // primitives fail:
    //   * vtable[15] FireActivate on the row → engine doesn't translate
    //     to "take this row" — rowCount stays unchanged.
    //   * Click-sim at row.GetControlCenter() coords → cursor hits dead
    //     space (Down=0, Up=0). Row extents are listbox-local, not
    //     screen-absolute; we'd need parent offset accumulation.
    //
    // Until we identify the engine's row-take primitive (likely embedded
    // in CSWGuiContainer::HandleInputEvent at 0x006b92f0 or the protoitem's
    // onClick), Enter dispatches BTN_OK unconditionally — the working
    // "take-all" gesture. Per-item take = lost feature, deferred. See
    // docs/equip-flow-investigation.md for the parallel investigation
    // that landed the same shape on equip.
    QueueButtonByIdActivate(panel, kContainerBtnOkId,
                            "Container: Enter -> BTN_OK (take-all; "
                            "per-item take deferred)");
    return true;
}

bool ContainerOnEsc(void* panel) {
    QueueButtonByIdActivate(panel, kContainerBtnCancelId,
                            "Container: Esc -> BTN_CANCEL");
    return true;
}

constexpr ListBoxPanelSpec kContainerSpec = {
    /*logTag*/                  "Container",
    /*matches*/                 ContainerMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             ContainerFindLb,
    /*minSel*/                  0,
    /*announce*/                ContainerAnnounce,
    /*logExtra*/                nullptr,
    /*onEnter*/                 ContainerOnEnter,
    /*onEsc*/                   ContainerOnEsc,
    /*alwaysReturnFromHandler*/ true,
};

// ============================================================================
// SaveLoad — Spiel laden / Spiel speichern dialog.
// ============================================================================

bool SaveLoadMatches(void* p) { return IsSaveLoadPanel(p); }

void* SaveLoadFindLb(void* p) {
    return FindControlById(p, kSaveLoadLbGamesId);
}

// Speak on every step (no per-tick monitor watches the SaveLoad listbox).
// Pull planet/area from the row entry's CExoString fields directly — the
// preview labels at id=4/id=6 only refresh through the engine's
// onSelectionChanged callback that DriveListBoxSelection bypasses.
void SaveLoadAnnounce(void* /*lb*/, const ListBoxNavResult& r) {
    if (!r.row) return;
    char rowText[256];
    const char* rowSrc = acc::menus::extract::FromControl(
        r.row, rowText, sizeof(rowText));
    if (!rowSrc) return;
    const char* planet = ReadSaveLoadEntryString(
        r.row, kSaveLoadEntryLastModuleOffset);
    const char* area   = ReadSaveLoadEntryString(
        r.row, kSaveLoadEntryAreaNameOffset);
    char msg[512];
    if (planet || area) {
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(acc::strings::Id::FmtSaveLoadRow),
                 rowText, planet ? planet : "",
                 area ? area : "",
                 r.newSel + 1, r.rowCount);
    } else {
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(acc::strings::Id::FmtSaveLoadRowNoLoc),
                 rowText, r.newSel + 1, r.rowCount);
    }
    tolk::Speak(msg, /*interrupt=*/false);
}

void SaveLoadLogExtra(char* out, size_t outN, const ListBoxNavResult& r) {
    const char* planet = ReadSaveLoadEntryString(
        r.row, kSaveLoadEntryLastModuleOffset);
    const char* area   = ReadSaveLoadEntryString(
        r.row, kSaveLoadEntryAreaNameOffset);
    snprintf(out, outN, " row=%p planet=\"%s\" area=\"%s\"",
             r.row, planet ? planet : "", area ? area : "");
}

bool SaveLoadOnEnter(void* panel) {
    QueueButtonByIdActivate(panel, kSaveLoadBtnSaveLoadId,
                            "SaveLoad: Enter -> saveload_button");
    return true;
}

bool SaveLoadOnEsc(void* panel) {
    QueueButtonByIdActivate(panel, kSaveLoadBtnBackId,
                            "SaveLoad: Esc -> back_button");
    return true;
}

constexpr ListBoxPanelSpec kSaveLoadSpec = {
    /*logTag*/                  "SaveLoad",
    /*matches*/                 SaveLoadMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             SaveLoadFindLb,
    /*minSel*/                  0,
    /*announce*/                SaveLoadAnnounce,
    /*logExtra*/                SaveLoadLogExtra,
    /*onEnter*/                 SaveLoadOnEnter,
    /*onEsc*/                   SaveLoadOnEsc,
    /*alwaysReturnFromHandler*/ false,  // fall through so chain-nav can reach Delete
};

// ============================================================================
// EquipPicker — modal item-pick zone inside the equipment screen.
// Armed when a slot Enter (BTN_INV_*) opens LB_ITEMS for that slot;
// disarmed on row commit, Esc, panel close, or panel-pointer drift.
// ============================================================================

bool EquipPickerMatchesPanel(void* p) {
    return IdentifyPanel(p) == PanelKind::InGameEquip;
}

bool EquipPickerArmed() { return s_equipPickerActive; }

// Stale-reset: if armed against an older panel pointer (re-open), disarm.
// Picker state is per-panel.
void EquipPickerResetStale(void* activePanel) {
    if (s_equipPickerActive && s_equipPickerPanel != activePanel) {
        acclog::Write("EquipPicker", "disarm — panel changed (%p -> %p)",
                      s_equipPickerPanel, activePanel);
        s_equipPickerActive = false;
        s_equipPickerPanel  = nullptr;
    }
}

void* EquipPickerFindLb(void* p) {
    return FindControlById(p, kEquipLbItemsId);
}

// Inline announce only on no-op clamp; normal moves are caught by
// MonitorEquipPickerSelection. NB: rowCount-1 (template at row 0 excluded
// from user-visible totals) and r.newSel directly (not +1) since minSel=1
// already shifts indices to the 1-based user view.
void EquipPickerAnnounce(void* /*lb*/, const ListBoxNavResult& r) {
    if (r.newSel != r.oldSel) return;
    if (!r.row) return;
    char rowText[256];
    if (acc::menus::extract::FromControl(r.row, rowText, sizeof(rowText))) {
        char msg[320];
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
                 rowText, r.newSel, r.rowCount - 1);
        tolk::Speak(msg, /*interrupt=*/false);
    }
}

void EquipPickerLogExtra(char* out, size_t outN, const ListBoxNavResult& r) {
    snprintf(out, outN, ", real=%d", r.rowCount - 1);
}

// Custom Enter: lookup row + btn from listbox.selection_index, queue an
// EquipCommit op, disarm. Direct call to OnItemSelected/OnOKPressed
// bypasses click-sim entirely — see docs/equip-flow-investigation.md.
bool EquipPickerOnEnter(void* panel) {
    if (acc::menus::pending::IsPending()) {
        acclog::Write("EquipPicker", "Enter — op already pending; ignoring");
        DisarmEquipPicker();
        return true;
    }
    void* lb  = FindControlById(panel, kEquipLbItemsId);
    void* btn = FindControlById(panel, kEquipBtnEquipId);
    void* row = nullptr;
    short selIdx = -1;
    int   rowCount = 0;
    if (lb) {
        auto* lbBase = reinterpret_cast<unsigned char*>(lb);
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            lbBase + kListBoxControlsOffset);
        rowCount = (lbList && lbList->data) ? lbList->size : 0;
        selIdx = *reinterpret_cast<short*>(
            lbBase + kListBoxSelectionIndexOffset);
        if (lbList && lbList->data &&
            selIdx >= 1 && selIdx < rowCount) {
            row = lbList->data[selIdx];
        }
    }
    if (lb && row && btn) {
        // Both gates that OnItemSelected reads are satisfied here:
        // row->is_active is raised by the queue's drain;
        // description_listbox.bit_flags & 2 was raised by OnSelectSlot's
        // ShowDescription; items_listbox.bit_flags & 8 was raised by
        // OnSelectSlot's SetEnabled.
        acc::menus::pending::QueueEquipCommit(panel, row, btn);
        g_navSpeechSuppressBudget = 2;
        acclog::Write("EquipPicker", "Enter -> commit (row sel=%d %p "
                      "btn_equip=%p panel=%p)",
                      selIdx, row, btn, panel);
    } else {
        acclog::Write("EquipPicker", "Enter -- can't equip "
                      "(lb=%p row=%p btn=%p sel=%d rows=%d) panel=%p",
                      lb, row, btn, selIdx, rowCount, panel);
    }
    DisarmEquipPicker();
    return true;
}

// Custom Esc: just disarm. Chain focus is unchanged so the next arrow
// press resumes slot navigation.
bool EquipPickerOnEsc(void* panel) {
    acclog::Write("EquipPicker", "Esc -> disarm (panel=%p)", panel);
    DisarmEquipPicker();
    return true;
}

constexpr ListBoxPanelSpec kEquipPickerSpec = {
    /*logTag*/                  "EquipPicker",
    /*matches*/                 EquipPickerMatchesPanel,
    /*armed*/                   EquipPickerArmed,
    /*resetStale*/              EquipPickerResetStale,
    /*findListBox*/             EquipPickerFindLb,
    /*minSel*/                  1,  // skip protoitem template at row 0
    /*announce*/                EquipPickerAnnounce,
    /*logExtra*/                EquipPickerLogExtra,
    /*onEnter*/                 EquipPickerOnEnter,
    /*onEsc*/                   EquipPickerOnEsc,
    /*alwaysReturnFromHandler*/ false,  // fall through so slot-zone nav still works
};

// Spec table. Probe order matters: SaveLoad's structural matcher
// (FindControlById signature check) is a superset that could in principle
// match other panels with the same control IDs — Container and EquipPicker
// have distinct PanelKind values, so they probe first by identity. In
// practice the three matchers are disjoint; ordering here is just defensive.
constexpr const ListBoxPanelSpec* kSpecs[] = {
    &kContainerSpec,
    &kSaveLoadSpec,
    &kEquipPickerSpec,
};
constexpr int kNumSpecs = static_cast<int>(sizeof(kSpecs) / sizeof(kSpecs[0]));

// ============================================================================
// Dispatcher — generic over the spec table. Mirrors the structure of the
// original three inline blocks in menus.cpp's OnHandleInputEvent.
// ============================================================================

bool DispatchKeyDownEdge(const ListBoxPanelSpec& spec, void* panel,
                         int param_1)
{
    // Up / Down: drive the listbox cursor + announce.
    if (param_1 == kInputNavUp || param_1 == kInputNavDown) {
        void* lb = spec.findListBox(panel);
        ListBoxNavResult r;
        if (lb && DriveListBoxSelection(lb, /*navDown=*/param_1 == kInputNavDown,
                                        static_cast<short>(spec.minSel), r)) {
            spec.announce(lb, r);
            char extra[128] = {0};
            if (spec.logExtra) spec.logExtra(extra, sizeof(extra), r);
            acclog::Write(spec.logTag, "%s lb=%p sel=%d->%d (rows=%d)%s",
                          param_1 == kInputNavDown ? "Down" : "Up",
                          lb, r.oldSel, r.newSel, r.rowCount, extra);
        } else if (lb) {
            acclog::Write(spec.logTag, "%s lb=%p empty; nav ignored",
                          param_1 == kInputNavDown ? "Down" : "Up", lb);
        }
        return true;  // never let arrow keys leak to the engine here
    }

    // Enter: dispatch via the spec's onEnter callback.
    if ((param_1 == kInputEnter1 || param_1 == kInputEnter2) && spec.onEnter) {
        return spec.onEnter(panel);
    }

    // Esc: dispatch via the spec's onEsc callback.
    if ((param_1 == kInputEsc1 || param_1 == kInputEsc2) && spec.onEsc) {
        return spec.onEsc(panel);
    }

    // Other keys (Left/Right, etc.): not consumed.
    return false;
}

void LogStandard(int n, void* thisPtr, int param_1, int param_2,
                 bool consumed)
{
    int translated = acc::engine::ManagerTranslateCode(param_1);
    const char* tag = consumed ? " CONSUMED" : "";
    if (translated != param_1) {
        acclog::Write("Menus.Input", "#%d this=%p key=logical(%d) -> %s(%d) val=%d%s",
                      n, thisPtr, param_1,
                      acc::engine::InputIndexName(translated), translated,
                      param_2, tag);
    } else {
        acclog::Write("Menus.Input", "#%d this=%p key=%s(%d) val=%d%s",
                      n, thisPtr, acc::engine::InputIndexName(param_1),
                      param_1, param_2, tag);
    }
}

}  // namespace

bool TryHandleInput(int n, void* thisPtr, void* activePanel,
                    int param_1, int param_2, int& outRv)
{
    if (!activePanel) return false;

    for (int i = 0; i < kNumSpecs; ++i) {
        const ListBoxPanelSpec& spec = *kSpecs[i];
        if (!spec.matches(activePanel)) continue;

        // Match. Run the optional stale-reset hook regardless of armed
        // state (EquipPicker disarms here when the panel pointer drifts).
        if (spec.resetStale) spec.resetStale(activePanel);

        // If the spec is gated by an armed-check and isn't armed, fall
        // through so the rest of OnHandleInputEvent can run (chain nav,
        // slot-zone nav). Don't log here — the caller's outer log path
        // takes over.
        if (spec.armed && !spec.armed()) return false;

        bool consumed = false;
        if (param_2 != 0) {
            consumed = DispatchKeyDownEdge(spec, activePanel, param_1);
        }

        if (consumed) {
            LogStandard(n, thisPtr, param_1, param_2, /*consumed=*/true);
            outRv = 1;
            return true;
        }

        if (spec.alwaysReturnFromHandler) {
            // Container behaviour: even on unhandled keys, return 0
            // without falling through to chain nav. The original code
            // logged the standard line + returned (consumed ? 1 : 0).
            LogStandard(n, thisPtr, param_1, param_2, /*consumed=*/false);
            outRv = 0;
            return true;
        }

        // Not consumed and the spec wants fall-through (SaveLoad,
        // EquipPicker armed-but-unhandled). Caller's outer handler runs
        // and takes responsibility for logging.
        return false;
    }
    return false;
}

}  // namespace acc::menus::listbox
