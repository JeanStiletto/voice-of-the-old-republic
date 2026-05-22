// KOTOR Accessibility — listbox-driven panel input dispatcher.
//
// Step 4 of the menus.cpp refactor (with post-Step-5 monitor co-location).
// See menus_listbox.h for the motivation. This file holds:
//
//   * The ListBoxPanelSpec struct (private) — one entry per panel kind.
//   * Three spec entries: Container, SaveLoad, EquipPicker. Each carries
//     ~5-6 small static callbacks that capture its quirks (announce
//     format, button IDs, custom Enter dispatch, etc.).
//   * The dispatcher TryHandleInput that walks the table.
//   * The EquipPicker armed/panel state + accessors.
//   * The 3 subsystem-paired monitors (container, equip-picker, container
//     give-mode key poll) — co-located with the spec entries that own the
//     state they watch.

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "menus_listbox.h"

#include "engine_input.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "hotkeys.h"
#include "log.h"
#include "menus.h"           // SpeakIfChanged (empty-state dedup)
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
using acc::menus::detail::ListBoxNavOp;
using acc::menus::detail::ListBoxNavResult;
using acc::menus::detail::QueueButtonByIdActivate;

// .gui-time IDs (Container loot panel + SaveLoad dialog) — see menus.cpp
// for the full table. Duplicated locally because the spec entries need
// them at file scope and menus.cpp's copies are static. The IDs are
// .gui-resource-baked, stable across localizations.
namespace {
constexpr int kContainerBtnOkId      = 3;
constexpr int kContainerBtnGiveId    = 4;
constexpr int kContainerBtnCancelId  = 5;

constexpr int kSaveLoadLbGamesId     =  0;
constexpr int kSaveLoadBtnBackId     = 12;
constexpr int kSaveLoadBtnSaveLoadId = 14;
}

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

    // Optional per-row enrichment, fired AFTER `announce`. Used when the
    // spec needs to fetch supplementary speech text from an auxiliary
    // engine source — e.g. the SkillInfoBox spec calls
    // CSWGuiFeatsCharGen::OnEnterFeat(featId) on the underlying main panel
    // to repopulate its description_listbox, then reads + speaks it.
    //
    // Why split off `announce`: the row-name-and-position speech is a
    // common shape across all specs, but the "fetch from elsewhere and
    // speak extra context" path is intentionally a side-channel — it can
    // call into the engine, can fault, can early-return, and shouldn't
    // pollute the simpler announce path. Keeping the two callbacks
    // separate means a spec that just wants to speak a row title (Container)
    // doesn't have to think about enrichment, and vice versa.
    //
    // nullptr = no enrichment (most specs).
    void (*enrichRow)(void* panel, const ListBoxNavResult& r);

    // Optional extra text appended to the standard nav log line.
    // SaveLoad uses it to log planet/area; others leave it nullptr.
    void (*logExtra)(char* out, size_t outN, const ListBoxNavResult& r);

    // Handle Enter. Returns true if consumed. Called only when armed and
    // the key is Enter1/Enter2.
    bool (*onEnter)(void* panel);

    // Handle Esc. Returns true if consumed. nullptr = don't handle Esc.
    bool (*onEsc)(void* panel);

    // Optional title-speech override. Called from menus.cpp's
    // AnnouncePanelTitle when a panel that this spec matches gets focus
    // for the first time. Returning a non-null string substitutes that
    // string for the generic title-walk speech; returning nullptr falls
    // back to the generic walk (first announceable label child).
    //
    // Used by the SkillInfoBox spec to replace the BioWare-leftover
    // placeholder ("Items Available to Place in Container and blah blah
    // blah") that the chargen flow doesn't override at runtime.
    //
    // nullptr = no override (most specs — the generic title walk works).
    const char* (*titleOverride)(void* panel);

    // Optional spoken phrase when the user navigates an empty listbox.
    // The dispatcher currently just logs `lb=... empty; nav ignored` and
    // stays silent; for panels where empty is a meaningful state worth
    // announcing (e.g. workbench items-picker when the player has no
    // upgradable weapons in the chosen category) this speaks the phrase
    // once per Up/Down press. Dedup happens on the speech channel so
    // mashing arrows doesn't spam the user.
    //
    // Count_ = no announcement (default).
    acc::strings::Id emptyStateId;

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
        // Append stack count when the row holds a stackable item with
        // more than one copy. Stays silent on stack_size <= 1 so weapons
        // / armour don't drag "1 Stück" through every announce.
        int stack = acc::engine::ReadItemRowStackCount(r.row);
        if (stack > 1) {
            char suffix[64];
            snprintf(suffix, sizeof(suffix),
                     acc::strings::Get(acc::strings::Id::FmtItemStackSuffix),
                     stack);
            tolk::Speak(suffix, /*interrupt=*/false);
        }
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
    /*enrichRow*/               nullptr,
    /*logExtra*/                nullptr,
    /*onEnter*/                 ContainerOnEnter,
    /*onEsc*/                   ContainerOnEsc,
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::Count_,  // empty containers don't open at all
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
    /*enrichRow*/               nullptr,
    /*logExtra*/                SaveLoadLogExtra,
    /*onEnter*/                 SaveLoadOnEnter,
    /*onEsc*/                   SaveLoadOnEsc,
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::Count_,
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
    /*enrichRow*/               nullptr,
    /*logExtra*/                EquipPickerLogExtra,
    /*onEnter*/                 EquipPickerOnEnter,
    /*onEsc*/                   EquipPickerOnEsc,
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::Count_,
    /*alwaysReturnFromHandler*/ false,  // fall through so slot-zone nav still works
};

// ============================================================================
// SkillInfoBox — engine slot for "info popups" with a row list and OK.
//
// skillinfo.gui mounted on the engine's SkillInfoBox slot. Three live
// controls: title label (id=0), LB_SKILLS listbox (id=2), BTN_OK (id=4).
// The .gui's title is hard-baked to the BioWare placeholder "Items
// Available to Place in Container and blah blah blah" — the chargen flow
// doesn't override it at runtime, so we substitute via titleOverride.
//
// In the chargen Talente flow this surfaces as the "ShowGranted" popup —
// CSWGuiFeatsCharGen mounts skillinfo.gui to dump the class's auto-granted
// feats (different per class — Soldat, Schurke, Späher) before letting the
// user proceed to actual feat selection on the underlying main panel.
// Each row carries only icon + name strref; the feat ID itself isn't
// stored on the row, so we recover it by reverse-lookup against the
// rules' CSWFeat[] array (matching name strrefs).
//
// The underlying CSWGuiFeatsCharGen panel sits below this overlay on the
// modal stack — calling its OnEnterFeat(featId) repopulates its
// description_listbox.controls[0] with the wrapped feat description,
// which we then read for the per-row enrichment speech.
// ============================================================================

constexpr int kSkillInfoBoxLbSkillsId = 2;
constexpr int kSkillInfoBoxBtnOkId    = 4;

bool SkillInfoBoxMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::SkillInfoBox;
}

void* SkillInfoBoxFindLb(void* p) {
    return FindControlById(p, kSkillInfoBoxLbSkillsId);
}

// Walk the manager's panels[] for a CSWGuiFeatsCharGen instance. Returns
// nullptr when none is mounted (e.g. SkillInfoBox shown from a different
// chargen substep). Cheap — panels.size is ≤16 in practice and we only
// fire from picker arrow steps.
void* FindFeatsCharGenPanel() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    void** panelsData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    int    panelsSize = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    if (!panelsData || panelsSize <= 0) return nullptr;
    int n = panelsSize > 32 ? 32 : panelsSize;
    for (int i = 0; i < n; ++i) {
        void* panel = panelsData[i];
        if (!panel) continue;
        void** vt = nullptr;
        __try {
            vt = *reinterpret_cast<void***>(panel);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiFeatsCharGen) {
            return panel;
        }
    }
    return nullptr;
}

// Best-effort read of a CSWGuiLabel's rendered text via the engine's
// resolved gui_string path with a strref/exostring fallback. Same shape
// the chargen Skills helpers use.
bool ReadLabelText(void* label, char* out, size_t outN) {
    if (!label || !out || outN == 0) return false;
    out[0] = '\0';
    __try {
        if (acc::engine::ReadGuiString(label, kLabelGuiStringPtrOffset,
                                       out, outN) && out[0] != '\0') {
            return true;
        }
        if (acc::engine::ExtractTextOrStrRefIndirect(
                label, kLabelTextOffset, kLabelStrRefOffset,
                kLabelTextObjectOffset, out, outN) && out[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
    }
    return false;
}

// Reverse-lookup: given the strref the engine wrote onto a SkillEntry row
// (kLabelStrRefOffset within the SkillEntry — its label_hilight.label.text
// .text_params.str_ref), find the matching feat in Rules->feats[] and
// return the feat ID. Returns -1 on miss (row strref unset, lookup fault,
// or no feat with matching name strref — happens in non-feat contexts
// where SkillInfoBox would be reused for skills/force-powers).
int ResolveFeatIdFromRowStrref(void* row, int& outRowStrref) {
    outRowStrref = 0;
    if (!row) return -1;
    __try {
        outRowStrref = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(row) + kLabelStrRefOffset);
        if (outRowStrref == -1 || outRowStrref == 0) return -1;
        void* rules = *reinterpret_cast<void**>(kAddrRulesGlobal);
        if (!rules) return -1;
        auto* rulesBase = reinterpret_cast<unsigned char*>(rules);
        auto* feats = *reinterpret_cast<unsigned char**>(
            rulesBase + kRulesFeatsArrayOffset);
        int featCount = *reinterpret_cast<unsigned short*>(
            rulesBase + kRulesFeatCountOffset);
        if (!feats || featCount <= 0 || featCount > 0x4000) return -1;
        for (int i = 0; i < featCount; ++i) {
            int s = *reinterpret_cast<int*>(
                feats + i * kFeatStructSize + kFeatNameStrRefOffset);
            if (s == outRowStrref) return i;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return -1;
}

// Standard per-row speech: just "feat name, N of M". The
// fetch-and-speak-description side-channel runs in the enrichRow callback
// below.
void SkillInfoBoxAnnounce(void* /*lb*/, const ListBoxNavResult& r) {
    if (!r.row) return;
    char rowText[128];
    rowText[0] = '\0';
    if (!acc::menus::extract::FromControl(r.row, rowText, sizeof(rowText))) {
        return;
    }
    char head[200];
    snprintf(head, sizeof(head),
             acc::strings::Get(acc::strings::Id::FmtChargenFeatGrantedRow),
             rowText, r.newSel + 1, r.rowCount);
    tolk::Speak(head, /*interrupt=*/false);
}

// Per-row enrichment: refresh the underlying CSWGuiFeatsCharGen's
// description_listbox via OnEnterFeat(featId), then read + speak that
// description. Quietly no-ops in non-feat contexts (no main panel
// mounted, or row strref doesn't resolve to a known feat).
void SkillInfoBoxEnrichRow(void* /*panel*/, const ListBoxNavResult& r) {
    if (!r.row) return;

    void* fcp = FindFeatsCharGenPanel();
    if (!fcp) {
        // SkillInfoBox shown from a non-feat context — silent skip.
        return;
    }

    int rowStrref = 0;
    int featIdx = ResolveFeatIdFromRowStrref(r.row, rowStrref);
    if (featIdx < 0) {
        acclog::Write("SkillInfoBox",
                      "feat-id lookup miss fcp=%p sel=%d strref=%d",
                      fcp, r.newSel, rowStrref);
        return;
    }
    unsigned short featId = static_cast<unsigned short>(featIdx);

    // Synchronous engine call — fires DetermineFeat → SetDescription →
    // ClearItems + AddControls on description_listbox. After this returns,
    // description_listbox.controls[0] holds the wrapped text for featId.
    typedef void (__thiscall* PFN_OnEnterFeat)(void* this_,
                                               unsigned short featId);
    __try {
        auto fn = reinterpret_cast<PFN_OnEnterFeat>(
            kAddrCSWGuiFeatsCharGenOnEnterFeat);
        fn(fcp, featId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("SkillInfoBox",
                      "OnEnterFeat faulted fcp=%p featId=%u",
                      fcp, (unsigned)featId);
        return;
    }

    auto* fcpBase = reinterpret_cast<unsigned char*>(fcp);
    void* descLb  = fcpBase + kFeatsCharGenDescriptionListBoxOffset;
    auto* descList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(descLb) + kListBoxControlsOffset);
    void* descRow = nullptr;
    __try {
        if (descList && descList->data && descList->size > 0) {
            descRow = descList->data[0];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        descRow = nullptr;
    }

    char desc[1024];
    if (descRow && ReadLabelText(descRow, desc, sizeof(desc)) &&
        desc[0] != '\0') {
        tolk::Speak(desc, /*interrupt=*/false);
        // Newline-flatten for the diagnostic log line.
        char dump[1024];
        size_t dn = strnlen(desc, sizeof(desc) - 1);
        if (dn >= sizeof(dump)) dn = sizeof(dump) - 1;
        for (size_t i = 0; i < dn; ++i) {
            char c = desc[i];
            dump[i] = (c == '\n' || c == '\r') ? ' ' : c;
        }
        dump[dn] = '\0';
        acclog::Write("SkillInfoBox",
                      "desc fcp=%p featId=%u (first 300 chars: \"%.300s\")",
                      fcp, (unsigned)featId, dump);
    } else {
        acclog::Write("SkillInfoBox",
                      "desc fcp=%p featId=%u empty or unreadable",
                      fcp, (unsigned)featId);
    }
}

bool SkillInfoBoxOnEnter(void* panel) {
    QueueButtonByIdActivate(panel, kSkillInfoBoxBtnOkId,
                            "SkillInfoBox: Enter -> BTN_OK");
    return true;
}

// Title override: substitute the localised "Du erhältst diese Talente"
// string for the BioWare placeholder text baked into skillinfo.gui.
// Only applies in the chargen Feats flow — gated by FindFeatsCharGenPanel
// so future Force-Powers / Skills reuse can layer on different titles.
const char* SkillInfoBoxTitleOverride(void* /*panel*/) {
    if (FindFeatsCharGenPanel()) {
        return acc::strings::Get(acc::strings::Id::ChargenFeatGrantedTitle);
    }
    return nullptr;  // unknown SkillInfoBox host — let the generic walk run
}

constexpr ListBoxPanelSpec kSkillInfoBoxSpec = {
    /*logTag*/                  "SkillInfoBox",
    /*matches*/                 SkillInfoBoxMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             SkillInfoBoxFindLb,
    /*minSel*/                  0,
    /*announce*/                SkillInfoBoxAnnounce,
    /*enrichRow*/               SkillInfoBoxEnrichRow,
    /*logExtra*/                nullptr,
    /*onEnter*/                 SkillInfoBoxOnEnter,
    /*onEsc*/                   nullptr,            // no Cancel button on this overlay
    /*titleOverride*/           SkillInfoBoxTitleOverride,
    /*emptyStateId*/            acc::strings::Id::Count_,
    /*alwaysReturnFromHandler*/ true,               // modal popup — don't fall through
};

// ============================================================================
// InGameMessages — combat-feedback log + dialog history. Two listboxes
// inside the same panel (messages_listbox @+0x64, dialog_listbox @+0x344);
// the user toggles which is shown via show_button @+0x76c.
//
// Phase 1C of the combat-system plan. Skeleton routes Up/Down to
// messages_listbox by default — switching to dialog_listbox requires a
// state bit we don't yet capture from the toggle button. The user can
// still navigate the active view via the engine's mouse-driven toggle;
// the spec-driven keyboard nav follows the messages_listbox until that
// state-tracking lands.
// ============================================================================

bool InGameMessagesMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::InGameMessages;
}

void* InGameMessagesFindLb(void* p) {
    if (!p) return nullptr;
    return reinterpret_cast<unsigned char*>(p) +
           kInGameMessagesMessagesListBoxOffset;
}

void InGameMessagesAnnounce(void* /*lb*/, const ListBoxNavResult& r) {
    if (!r.row) return;
    char rowText[512];
    if (!acc::menus::extract::FromControl(r.row, rowText, sizeof(rowText))) {
        return;
    }
    char msg[640];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, r.newSel + 1, r.rowCount);
    tolk::Speak(msg, /*interrupt=*/false);
}

const char* InGameMessagesTitleOverride(void* /*panel*/) {
    return acc::strings::Get(acc::strings::Id::MessagesTitleCombatLog);
}

constexpr ListBoxPanelSpec kInGameMessagesSpec = {
    /*logTag*/                  "InGameMessages",
    /*matches*/                 InGameMessagesMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             InGameMessagesFindLb,
    /*minSel*/                  0,
    /*announce*/                InGameMessagesAnnounce,
    /*enrichRow*/               nullptr,
    /*logExtra*/                nullptr,
    /*onEnter*/                 nullptr,
    /*onEsc*/                   nullptr,
    /*titleOverride*/           InGameMessagesTitleOverride,
    /*emptyStateId*/            acc::strings::Id::Count_,
    /*alwaysReturnFromHandler*/ false,
};

// ============================================================================
// CSWGuiDialog — replies_listbox @+0x19c4. Phase 1D of the combat-system
// plan. Same shape applies to DialogCinematic / DialogCinematicCopy /
// DialogComputer / DialogComputerCamera variants — registered as four
// matchers all pointing at the shared listbox locator.
// ============================================================================

bool DialogCinematicMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::DialogCinematic;
}
bool DialogCinematicCopyMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::DialogCinematicCopy;
}
bool DialogComputerMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::DialogComputer;
}
bool DialogComputerCameraMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::DialogComputerCamera;
}

void* DialogFindRepliesLb(void* p) {
    if (!p) return nullptr;
    return reinterpret_cast<unsigned char*>(p) + kDialogRepliesListBoxOffset;
}

void DialogReplyAnnounce(void* /*lb*/, const ListBoxNavResult& r) {
    if (!r.row) return;
    char rowText[512];
    if (!acc::menus::extract::FromControl(r.row, rowText, sizeof(rowText))) {
        return;
    }
    // is_active gate (CSWGuiControl.is_active @+0x4c) — when non-zero the
    // reply is selectable; zero means the engine greyed it out
    // (skill-check / alignment-locked). Append a "(unavailable)" suffix
    // so the user knows.
    bool active = true;
    __try {
        active = *(reinterpret_cast<unsigned char*>(r.row) +
                   kControlIsActiveOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        active = true;
    }
    char msg[640];
    if (active) {
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
                 rowText, r.newSel + 1, r.rowCount);
    } else {
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(
                     acc::strings::Id::FmtDialogReplyUnavailableRow),
                 rowText,
                 acc::strings::Get(
                     acc::strings::Id::DialogReplyUnavailable),
                 r.newSel + 1, r.rowCount);
    }
    tolk::Speak(msg, /*interrupt=*/false);
}

constexpr ListBoxPanelSpec kDialogCinematicSpec = {
    /*logTag*/                  "DialogCinematic",
    /*matches*/                 DialogCinematicMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             DialogFindRepliesLb,
    /*minSel*/                  0,
    /*announce*/                DialogReplyAnnounce,
    /*enrichRow*/               nullptr,
    /*logExtra*/                nullptr,
    /*onEnter*/                 nullptr,
    /*onEsc*/                   nullptr,
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::Count_,
    /*alwaysReturnFromHandler*/ false,
};
constexpr ListBoxPanelSpec kDialogCinematicCopySpec = {
    /*logTag*/                  "DialogCinematicCopy",
    /*matches*/                 DialogCinematicCopyMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             DialogFindRepliesLb,
    /*minSel*/                  0,
    /*announce*/                DialogReplyAnnounce,
    /*enrichRow*/               nullptr,
    /*logExtra*/                nullptr,
    /*onEnter*/                 nullptr,
    /*onEsc*/                   nullptr,
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::Count_,
    /*alwaysReturnFromHandler*/ false,
};
constexpr ListBoxPanelSpec kDialogComputerSpec = {
    /*logTag*/                  "DialogComputer",
    /*matches*/                 DialogComputerMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             DialogFindRepliesLb,
    /*minSel*/                  0,
    /*announce*/                DialogReplyAnnounce,
    /*enrichRow*/               nullptr,
    /*logExtra*/                nullptr,
    /*onEnter*/                 nullptr,
    /*onEsc*/                   nullptr,
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::Count_,
    /*alwaysReturnFromHandler*/ false,
};
constexpr ListBoxPanelSpec kDialogComputerCameraSpec = {
    /*logTag*/                  "DialogComputerCamera",
    /*matches*/                 DialogComputerCameraMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             DialogFindRepliesLb,
    /*minSel*/                  0,
    /*announce*/                DialogReplyAnnounce,
    /*enrichRow*/               nullptr,
    /*logExtra*/                nullptr,
    /*onEnter*/                 nullptr,
    /*onEsc*/                   nullptr,
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::Count_,
    /*alwaysReturnFromHandler*/ false,
};

// ============================================================================
// WorkbenchItems — per-category item picker (upgradeitems.gui).
// LB_ITEMS at ID 0 holds the player's upgradable weapons in the chosen
// category. Enter → BTN_UPGRADEITEM (ID 4, "Aufwerten") commits the
// selection and opens the slot-detail panel. Esc → BTN_BACK (ID 5,
// "Schliess.") closes back to upgradesel.gui.
// ============================================================================

namespace {
constexpr int kWorkbenchItemsLbId        = 0;
constexpr int kWorkbenchItemsBtnUpgrade  = 4;
constexpr int kWorkbenchItemsBtnBack     = 5;
}  // namespace

bool WorkbenchItemsMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::WorkbenchItems;
}

void* WorkbenchItemsFindLb(void* p) {
    return FindControlById(p, kWorkbenchItemsLbId);
}

// Speak the focused weapon row + position. No per-tick monitor watches
// this listbox so we speak on every step (including clamp).
void WorkbenchItemsAnnounce(void* /*lb*/, const ListBoxNavResult& r) {
    if (!r.row || r.rowCount <= 0) return;
    char rowText[256];
    if (!acc::menus::extract::FromControl(r.row, rowText, sizeof(rowText))) {
        return;
    }
    char msg[320];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, r.newSel + 1, r.rowCount);
    tolk::Speak(msg, /*interrupt=*/false);
}

bool WorkbenchItemsOnEnter(void* panel) {
    QueueButtonByIdActivate(panel, kWorkbenchItemsBtnUpgrade,
                            "WorkbenchItems: Enter -> BTN_UPGRADEITEM");
    return true;
}

bool WorkbenchItemsOnEsc(void* panel) {
    QueueButtonByIdActivate(panel, kWorkbenchItemsBtnBack,
                            "WorkbenchItems: Esc -> BTN_BACK");
    return true;
}

constexpr ListBoxPanelSpec kWorkbenchItemsSpec = {
    /*logTag*/                  "WorkbenchItems",
    /*matches*/                 WorkbenchItemsMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             WorkbenchItemsFindLb,
    /*minSel*/                  0,
    /*announce*/                WorkbenchItemsAnnounce,
    /*enrichRow*/               nullptr,
    /*logExtra*/                nullptr,
    /*onEnter*/                 WorkbenchItemsOnEnter,
    /*onEsc*/                   WorkbenchItemsOnEsc,
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::WorkbenchItemsEmpty,
    /*alwaysReturnFromHandler*/ false,  // fall through so chain nav reaches buttons
};

// ============================================================================
// WorkbenchUpgrade — slot detail (upgrade.gui). 29 controls; the LB_ITEMS
// listbox at ID 0 holds compatible upgrade mods from the player's
// inventory for the currently selected slot. Enter on a row stages the
// installable item; the user then navigates to BTN_ASSEMBLE (ID 24,
// "Zusammenbauen") to commit. Esc → BTN_BACK (ID 28, "Abbrechen").
//
// We route Esc explicitly here because the generic FindCancelButton
// fallback would land on BTN_UPGRADE31 (id 12) — the first button by
// .gui ID — which the workbench treats as a slot select. Without the
// explicit ID 28 routing, Esc on the upgrade panel selects a slot
// instead of closing the panel.
// ============================================================================

namespace {
constexpr int kWorkbenchUpgradeLbId       = 0;
constexpr int kWorkbenchUpgradeBtnAssemble = 24;
constexpr int kWorkbenchUpgradeBtnBack    = 28;
}  // namespace

bool WorkbenchUpgradeMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::WorkbenchUpgrade;
}

void* WorkbenchUpgradeFindLb(void* p) {
    return FindControlById(p, kWorkbenchUpgradeLbId);
}

// LB_ITEMS rows are CSWGuiInventoryItemEntry-style — their text comes
// from the item resref's localised name. Same announce shape as the
// items picker.
void WorkbenchUpgradeAnnounce(void* /*lb*/, const ListBoxNavResult& r) {
    if (!r.row || r.rowCount <= 0) return;
    char rowText[256];
    if (!acc::menus::extract::FromControl(r.row, rowText, sizeof(rowText))) {
        return;
    }
    char msg[320];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, r.newSel + 1, r.rowCount);
    tolk::Speak(msg, /*interrupt=*/false);
}

bool WorkbenchUpgradeOnEnter(void* panel) {
    // Enter on the LB_ITEMS listbox is currently "stage this item" — the
    // engine doesn't commit anything until BTN_ASSEMBLE fires, so we just
    // let the engine handle the row-stage via FireActivate on the row.
    // Falling through to chain nav lets the user navigate via Up/Down on
    // LB_ITEMS and Enter to stage; chain-driven Enter on BTN_ASSEMBLE then
    // commits. Returning false here means the outer chain handler picks
    // up the keypress.
    (void)panel;
    return false;
}

bool WorkbenchUpgradeOnEsc(void* panel) {
    QueueButtonByIdActivate(panel, kWorkbenchUpgradeBtnBack,
                            "WorkbenchUpgrade: Esc -> BTN_BACK");
    return true;
}

constexpr ListBoxPanelSpec kWorkbenchUpgradeSpec = {
    /*logTag*/                  "WorkbenchUpgrade",
    /*matches*/                 WorkbenchUpgradeMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             WorkbenchUpgradeFindLb,
    /*minSel*/                  0,
    /*announce*/                WorkbenchUpgradeAnnounce,
    /*enrichRow*/               nullptr,
    /*logExtra*/                nullptr,
    /*onEnter*/                 WorkbenchUpgradeOnEnter,
    /*onEsc*/                   WorkbenchUpgradeOnEsc,
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::WorkbenchUpgradesEmpty,
    /*alwaysReturnFromHandler*/ false,  // fall through so chain nav reaches the slot/assemble buttons
};

// ============================================================================
// Examine — CSWGuiExamine panel opened by Shift+H. Listbox is the embedded
// CSWGuiMessageBox.listbox_message at +0x67c. The engine populates the
// rows from a local object cache when ShowExamineBox(handle, 0) is called
// (vtable[27] on the listbox does the populate-from-object — verified
// 2026-05-22 from the ShowExamineBox decomp). Up/Down speak each row;
// Enter / Esc let the engine's HandleInputEvent handle close natively
// (Schliess. and Abbrechen buttons both call HideExamineBox).
// ============================================================================

bool ExamineMatches(void* p) {
    return IdentifyPanel(p) == PanelKind::Examine;
}

void* ExamineFindLb(void* p) {
    if (!p) return nullptr;
    return reinterpret_cast<unsigned char*>(p) +
           kExaminePanelListBoxOffset;
}

void ExamineAnnounce(void* /*lb*/, const ListBoxNavResult& r) {
    if (!r.row || r.rowCount <= 0) return;
    char rowText[512];
    if (!acc::menus::extract::FromControl(r.row, rowText, sizeof(rowText))) {
        return;
    }
    char msg[640];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, r.newSel + 1, r.rowCount);
    tolk::Speak(msg, /*interrupt=*/false);
}

constexpr ListBoxPanelSpec kExamineSpec = {
    /*logTag*/                  "Examine",
    /*matches*/                 ExamineMatches,
    /*armed*/                   nullptr,
    /*resetStale*/              nullptr,
    /*findListBox*/             ExamineFindLb,
    /*minSel*/                  0,
    /*announce*/                ExamineAnnounce,
    /*enrichRow*/               nullptr,
    /*logExtra*/                nullptr,
    /*onEnter*/                 nullptr,  // engine HandleInputEvent closes via OK button
    /*onEsc*/                   nullptr,  // engine HandleInputEvent closes via Cancel button
    /*titleOverride*/           nullptr,
    /*emptyStateId*/            acc::strings::Id::Count_,
    /*alwaysReturnFromHandler*/ false,
};

// Spec table. Probe order matters: SaveLoad's structural matcher
// (FindControlById signature check) is a superset that could in principle
// match other panels with the same control IDs — Container and EquipPicker
// have distinct PanelKind values, so they probe first by identity. In
// practice the matchers are disjoint; ordering here is just defensive.
constexpr const ListBoxPanelSpec* kSpecs[] = {
    &kContainerSpec,
    &kSaveLoadSpec,
    &kEquipPickerSpec,
    &kSkillInfoBoxSpec,
    // Combat-system plan, Phase 1C/1D — read-only review screens.
    &kInGameMessagesSpec,
    &kDialogCinematicSpec,
    &kDialogCinematicCopySpec,
    &kDialogComputerSpec,
    &kDialogComputerCameraSpec,
    // Workbench panels (Phase: workbench accessibility).
    &kWorkbenchItemsSpec,
    &kWorkbenchUpgradeSpec,
    // Combat-system plan, Phase 2C — Shift+H Examine engine panel.
    &kExamineSpec,
};
constexpr int kNumSpecs = static_cast<int>(sizeof(kSpecs) / sizeof(kSpecs[0]));

// ============================================================================
// Dispatcher — generic over the spec table. Mirrors the structure of the
// original three inline blocks in menus.cpp's OnHandleInputEvent.
// ============================================================================

bool DispatchKeyDownEdge(const ListBoxPanelSpec& spec, void* panel,
                         int param_1)
{
    // Up / Down / Home / End: drive the listbox cursor + announce +
    // optional enrichment. Home / End are absolute jumps to the first
    // (minSel) / last (rowCount-1) row; Up / Down are ±1 steps. The
    // announce + enrichment + log paths are shape-identical across all
    // four — only the ListBoxNavOp selection and the direction tag differ.
    ListBoxNavOp op;
    const char* dirTag = nullptr;
    if      (param_1 == kInputNavUp)   { op = ListBoxNavOp::StepUp;    dirTag = "Up";   }
    else if (param_1 == kInputNavDown) { op = ListBoxNavOp::StepDown;  dirTag = "Down"; }
    else if (param_1 == kInputHome)    { op = ListBoxNavOp::JumpFirst; dirTag = "Home"; }
    else if (param_1 == kInputEnd)     { op = ListBoxNavOp::JumpLast;  dirTag = "End";  }

    if (dirTag) {
        void* lb = spec.findListBox(panel);
        ListBoxNavResult r;
        if (lb && DriveListBoxSelection(lb, op,
                                        static_cast<short>(spec.minSel), r)) {
            spec.announce(lb, r);
            if (spec.enrichRow) spec.enrichRow(panel, r);
            char extra[128] = {0};
            if (spec.logExtra) spec.logExtra(extra, sizeof(extra), r);
            acclog::Write(spec.logTag, "%s lb=%p sel=%d->%d (rows=%d)%s",
                          dirTag, lb, r.oldSel, r.newSel, r.rowCount, extra);
        } else if (lb) {
            acclog::Write(spec.logTag, "%s lb=%p empty; nav ignored",
                          dirTag, lb);
            // Announce the empty state if the spec provided one. The
            // generic dedup channel collapses repeated arrow presses so
            // the user doesn't hear the same phrase on every keystroke.
            if (spec.emptyStateId != acc::strings::Id::Count_) {
                const char* phrase = acc::strings::Get(spec.emptyStateId);
                if (phrase && phrase[0] != '\0') {
                    acc::menus::SpeakIfChanged(/*channel=*/1, phrase);
                }
            }
        }
        return true;  // never let nav keys leak to the engine here
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

// ============================================================================
// Subsystem-paired monitors. Each watches state owned by one of the spec
// entries above and announces changes per tick. Co-located here as of the
// post-Step-5 cleanup so state + handler + monitor live in one TU. Called
// from TickListboxMonitors below, which is fanned out from menus.cpp's
// TickMonitors.
// ============================================================================

namespace {

struct ContainerSelState {
    void* listBox;
    short lastSelection;
};
ContainerSelState s_containerSelState = { nullptr, -1 };

struct EquipSelState {
    void* listBox;
    short lastSelection;
};
EquipSelState s_equipSelState = { nullptr, -1 };

void MonitorContainerSelection() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);

    void* containerPanel = nullptr;
    if (panelData && panelCount > 0) {
        int n = panelCount > 16 ? 16 : panelCount;
        for (int i = 0; i < n; ++i) {
            void* p = panelData[i];
            if (!p) continue;
            if (IdentifyPanel(p) == PanelKind::Container) {
                containerPanel = p;
                break;
            }
        }
    }

    if (!containerPanel) {
        if (s_containerSelState.listBox) {
            acclog::Write("Menus.Container", "monitor disarmed: no Container panel in stack");
            s_containerSelState.listBox = nullptr;
            s_containerSelState.lastSelection = -1;
        }
        return;
    }

    void* lb = FindListBoxChild(containerPanel);
    if (!lb) return;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;

    short selIdx = *reinterpret_cast<short*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxSelectionIndexOffset);

    if (s_containerSelState.listBox != lb) {
        s_containerSelState.listBox       = lb;
        s_containerSelState.lastSelection = selIdx;
        if (rowCount <= 0) {
            tolk::Speak(acc::strings::Get(acc::strings::Id::ContainerEmpty),
                        /*interrupt=*/false);
            acclog::Write("Menus.Container", "monitor armed: panel=%p lb=%p empty initialSel=%d",
                          containerPanel, lb, selIdx);
        } else if (rowCount == 1) {
            tolk::Speak(acc::strings::Get(acc::strings::Id::ContainerOneItem),
                        /*interrupt=*/false);
            acclog::Write("Menus.Container", "monitor armed: panel=%p lb=%p count=1 initialSel=%d",
                          containerPanel, lb, selIdx);
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg),
                     acc::strings::Get(acc::strings::Id::FmtContainerItems),
                     rowCount);
            tolk::Speak(msg, /*interrupt=*/false);
            acclog::Write("Menus.Container", "monitor armed: panel=%p lb=%p count=%d initialSel=%d",
                          containerPanel, lb, rowCount, selIdx);
        }
        return;
    }

    if (selIdx == s_containerSelState.lastSelection) return;
    short prev = s_containerSelState.lastSelection;
    s_containerSelState.lastSelection = selIdx;

    if (selIdx < 0) {
        acclog::Write("Menus.Container", "selection cleared: lb=%p prev=%d", lb, prev);
        return;
    }
    if (!lbList || !lbList->data || selIdx >= lbList->size) {
        acclog::Write("Menus.Container", "selection out of range: lb=%p sel=%d size=%d",
                      lb, selIdx, lbList ? lbList->size : -1);
        return;
    }
    void* row = lbList->data[selIdx];
    if (!row) return;

    char rowText[256];
    const char* src = acc::menus::extract::FromControl(row, rowText, sizeof(rowText));
    if (!src) {
        acclog::Write("Menus.Container", "row %d (lb=%p) no announceable text", selIdx, lb);
        return;
    }

    char msg[320];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, selIdx + 1, rowCount);
    tolk::Speak(msg, /*interrupt=*/false);
    int stack = acc::engine::ReadItemRowStackCount(row);
    if (stack > 1) {
        char suffix[64];
        snprintf(suffix, sizeof(suffix),
                 acc::strings::Get(acc::strings::Id::FmtItemStackSuffix),
                 stack);
        tolk::Speak(suffix, /*interrupt=*/false);
    }
    acclog::Write("Menus.Container", "row lb=%p sel=%d (was %d) text=\"%s\" stack=%d",
                  lb, selIdx, prev, rowText, stack);
}

void MonitorEquipPickerSelection() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);

    void* equipPanel = nullptr;
    if (panelData && panelCount > 0) {
        int n = panelCount > 16 ? 16 : panelCount;
        for (int i = 0; i < n; ++i) {
            void* p = panelData[i];
            if (!p) continue;
            if (IdentifyPanel(p) == PanelKind::InGameEquip) {
                equipPanel = p;
                break;
            }
        }
    }

    if (!equipPanel) {
        if (s_equipSelState.listBox) {
            acclog::Write("Menus.EquipPicker", "monitor disarmed: no InGameEquip panel in stack");
            s_equipSelState.listBox       = nullptr;
            s_equipSelState.lastSelection = -1;
        }
        if (s_equipPickerActive) {
            acclog::Write("EquipPicker", "disarm — panel gone from panels[]");
            s_equipPickerActive = false;
            s_equipPickerPanel  = nullptr;
        }
        return;
    }

    void* lb = FindControlById(equipPanel, kEquipLbItemsId);
    if (!lb) return;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;

    short selIdx = *reinterpret_cast<short*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxSelectionIndexOffset);

    if (s_equipSelState.listBox != lb) {
        s_equipSelState.listBox       = lb;
        s_equipSelState.lastSelection = selIdx;
        acclog::Write("Menus.EquipPicker", "monitor armed: panel=%p lb=%p rows=%d initialSel=%d",
                      equipPanel, lb, rowCount, selIdx);
        return;
    }

    if (selIdx == s_equipSelState.lastSelection) return;
    short prev = s_equipSelState.lastSelection;
    s_equipSelState.lastSelection = selIdx;

    if (selIdx < 0) {
        acclog::Write("Menus.EquipPicker", "selection cleared: lb=%p prev=%d", lb, prev);
        return;
    }
    if (selIdx == 0) {
        acclog::Write("Menus.EquipPicker", "selection on protoitem (sel=0) lb=%p", lb);
        return;
    }
    if (!lbList || !lbList->data || selIdx >= lbList->size) {
        acclog::Write("Menus.EquipPicker", "selection out of range: lb=%p sel=%d size=%d",
                      lb, selIdx, lbList ? lbList->size : -1);
        return;
    }
    void* row = lbList->data[selIdx];
    if (!row) return;

    char rowText[256];
    const char* src = acc::menus::extract::FromControl(row, rowText, sizeof(rowText));
    if (!src) {
        acclog::Write("Menus.EquipPicker", "row %d (lb=%p) no announceable text", selIdx, lb);
        return;
    }

    int userPos   = selIdx;
    int userTotal = rowCount - 1;
    char msg[320];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, userPos, userTotal);
    tolk::Speak(msg, /*interrupt=*/false);
    acclog::Write("Menus.EquipPicker", "row lb=%p sel=%d (was %d) text=\"%s\"",
                  lb, selIdx, prev, rowText);
}

void PollContainerGiveModeKey() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::ContainerGiveMode)) return;

    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    void* fgPanel = acc::engine::GetForegroundPanel(mgr);
    if (!fgPanel || IdentifyPanel(fgPanel) != PanelKind::Container) return;

    if (acc::menus::pending::IsPending()) {
        acclog::Write("Container", "G (give-mode) -- op already pending; ignoring");
        return;
    }
    void* btn = FindControlById(fgPanel, kContainerBtnGiveId);
    if (!btn) {
        acclog::Write("Container", "G (give-mode) -- BTN_GIVEITEMS not found on panel=%p",
                      fgPanel);
        return;
    }
    acc::menus::pending::QueueActivate(btn);
    acclog::Write("Container", "G (give-mode) -> FireActivate BTN_GIVEITEMS panel=%p target=%p",
                  fgPanel, btn);
}

}  // namespace

void TickListboxMonitors() {
    MonitorContainerSelection();
    MonitorEquipPickerSelection();
    PollContainerGiveModeKey();
}

const char* GetTitleOverride(void* panel) {
    if (!panel) return nullptr;
    for (int i = 0; i < kNumSpecs; ++i) {
        const ListBoxPanelSpec& spec = *kSpecs[i];
        if (!spec.matches(panel)) continue;
        if (!spec.titleOverride) return nullptr;
        return spec.titleOverride(panel);
    }
    return nullptr;
}

// =============================================================================
// CSWGuiFeatsCharGen diagnostic dump — one-shot per panel pointer.
// =============================================================================

namespace {

void* s_loggedFeatsPanel = nullptr;

// Read a CSWFeat.name_strref for `featId` from the rules table. Returns 0
// (= invalid strref) on out-of-range or fault. Used in the dump only to
// give the log a stable cross-reference.
int FeatNameStrref(unsigned short featId) {
    __try {
        void* rules = *reinterpret_cast<void**>(kAddrRulesGlobal);
        if (!rules) return 0;
        auto* rulesBase = reinterpret_cast<unsigned char*>(rules);
        auto* feats = *reinterpret_cast<unsigned char**>(
            rulesBase + kRulesFeatsArrayOffset);
        unsigned short featCount = *reinterpret_cast<unsigned short*>(
            rulesBase + kRulesFeatCountOffset);
        if (!feats || featId >= featCount) return 0;
        return *reinterpret_cast<int*>(
            feats + (size_t)featId * kFeatStructSize +
            kFeatNameStrRefOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void DumpUshortListSEH(unsigned char* base, size_t dataOff, size_t sizeOff,
                       const char* tag) {
    unsigned short* data = nullptr;
    int size = 0;
    __try {
        data = *reinterpret_cast<unsigned short**>(base + dataOff);
        size = *reinterpret_cast<int*>(base + sizeOff);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("FeatsCharGen.Dump", "  %s: read fault", tag);
        return;
    }
    if (!data || size <= 0 || size > 0x4000) {
        acclog::Write("FeatsCharGen.Dump", "  %s: empty (size=%d data=%p)",
                      tag, size, data);
        return;
    }
    char buf[1024];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "  %s (size=%d):", tag, size);
    int dumpCount = size > 96 ? 96 : size;
    for (int i = 0; i < dumpCount && n + 32 < (int)sizeof(buf); ++i) {
        unsigned short v = 0xffff;
        __try {
            v = data[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            v = 0xffff;
        }
        n += snprintf(buf + n, sizeof(buf) - n, " %u(strref=%d)",
                      (unsigned)v, FeatNameStrref(v));
    }
    if (size > dumpCount && n + 8 < (int)sizeof(buf)) {
        snprintf(buf + n, sizeof(buf) - n, " ...");
    }
    acclog::Write("FeatsCharGen.Dump", "%s", buf);
}

void DumpChartCells(void* fcp) {
    auto* base   = reinterpret_cast<unsigned char*>(fcp);
    auto* chart  = base + kFeatsCharGenChartOffset;
    void** rows  = nullptr;
    int   nRows  = 0;
    int   selCol = -1, selRow = -1;
    __try {
        rows = *reinterpret_cast<void***>(
            chart + kSkillFlowChartRowsDataOffset);
        nRows = *reinterpret_cast<int*>(
            chart + kSkillFlowChartRowsSizeOffset);
        selCol = *reinterpret_cast<unsigned char*>(
            chart + kSkillFlowChartSelectedColOffset);
        selRow = *reinterpret_cast<unsigned char*>(
            chart + kSkillFlowChartSelectedRowOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("FeatsCharGen.Dump", "  chart: read fault");
        return;
    }
    acclog::Write("FeatsCharGen.Dump",
                  "  chart rows=%d sel=(row=%d, col=%d)",
                  nRows, selRow, selCol);
    if (!rows || nRows <= 0 || nRows > 256) return;
    for (int r = 0; r < nRows; ++r) {
        void* row = nullptr;
        __try {
            row = rows[r];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (!row) continue;
        auto* rb = reinterpret_cast<unsigned char*>(row);
        unsigned int featId[3]  = { kFlowSkillStructEmptyFeatId,
                                    kFlowSkillStructEmptyFeatId,
                                    kFlowSkillStructEmptyFeatId };
        unsigned int status[3]  = { 0, 0, 0 };
        __try {
            for (int c = 0; c < kSkillFlowColumnsPerRow; ++c) {
                auto* col = rb + kSkillFlowFirstColumnOffset +
                            c * kSkillFlowColumnStride;
                featId[c] = *reinterpret_cast<unsigned int*>(
                    col + kFlowSkillStructFeatIdOffset);
                status[c] = *reinterpret_cast<unsigned int*>(
                    col + kFlowSkillStructStatusOffset);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        char line[512];
        int n = snprintf(line, sizeof(line), "  row[%d] %p:", r, row);
        for (int c = 0; c < kSkillFlowColumnsPerRow; ++c) {
            if (featId[c] == kFlowSkillStructEmptyFeatId) {
                n += snprintf(line + n, sizeof(line) - n,
                              " col%d=empty", c);
            } else {
                n += snprintf(line + n, sizeof(line) - n,
                              " col%d={featId=%u status=%u strref=%d}",
                              c, featId[c], status[c],
                              FeatNameStrref((unsigned short)featId[c]));
            }
        }
        acclog::Write("FeatsCharGen.Dump", "%s", line);
    }
}

}  // namespace

void DumpFeatsCharGenStructureIfNeeded(void* panel) {
    if (!panel || panel == s_loggedFeatsPanel) return;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (reinterpret_cast<uintptr_t>(vt) != kVtableCSWGuiFeatsCharGen) {
        return;
    }
    s_loggedFeatsPanel = panel;

    acclog::Write("FeatsCharGen.Dump",
                  "===== panel=%p (CSWGuiFeatsCharGen) =====", panel);

    auto* base = reinterpret_cast<unsigned char*>(panel);
    DumpUshortListSEH(base, kFeatsCharGenExistingListDataOffset,
                      kFeatsCharGenExistingListSizeOffset,
                      "existing  field19");
    DumpUshortListSEH(base, kFeatsCharGenGrantedListDataOffset,
                      kFeatsCharGenGrantedListSizeOffset,
                      "granted   field20");
    DumpUshortListSEH(base, kFeatsCharGenAvailableListDataOffset,
                      kFeatsCharGenAvailableListSizeOffset,
                      "available field23");
    DumpUshortListSEH(base, kFeatsCharGenChosenListDataOffset,
                      kFeatsCharGenChosenListSizeOffset,
                      "chosen    field26");

    DumpChartCells(panel);

    acclog::Write("FeatsCharGen.Dump", "===== end =====");
}

}  // namespace acc::menus::listbox
