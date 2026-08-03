// First-sight / focus-capture logic for the panel focus hook.
//
// Split out of menus.cpp by the Phase-1 structure pass (refactoring
// candidate 1). OnSetActiveControl stays in menus.cpp and calls into these
// five functions; see menus_focus.h for what each one owns.
//
// The definitions are verbatim from menus.cpp apart from being lifted out
// of file-static scope into acc::menus::focus so the hook can still reach
// them. g_lastTitledPanel moved with SpeakPanelTitleOnFirstSight (its only
// user); the pending-announce slot stays owned by menus.cpp and is written
// here through the externs declared in menus_internal.h.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "prism.h"
#include "menus.h"
#include "menus_focus.h"
#include "menus_internal.h"
#include "menus_extract.h"
#include "menus_chain.h"
#include "menus_monitors.h"
#include "menus_chargen_attr.h"
#include "menus_chargen_skills.h"
#include "menus_powers_levelup.h"
#include "menus_editbox.h"
#include "menus_listbox.h"
#include "menus_store.h"
#include "engine_input.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "engine_rebase.h"
#include "strings.h"
#include "tutorial_hints.h"
#include "tutorial_popup.h"
#include "update_checker.h"
#include "diag_chargen_feats.h"
#include "focus_guard.h"

using namespace acc::engine;

using acc::menus::detail::IsClassSelectionIcon;
using acc::menus::detail::ClassLabelCacheLookup;
using acc::menus::detail::ClassLabelCacheStore;
using acc::menus::chain::WalkChildren;
using acc::menus::chain::FindAdjacentArrow;

// Tracks the last panel for which we spoke the title (AnnouncePanelTitle).
// Re-entering the same panel pointer must not re-announce. A distinct static
// from the s_lastPanel inside OnSetActiveControl — that one drives the
// diagnostic WalkChildren logging.
static void* g_lastTitledPanel = nullptr;

// First focus into a panel speaks the panel's "title" — the first label-like
// child we can find — so the user knows which menu they're in. Subsequent
// per-control announcements still fire from OnSetActiveControl as the user
// navigates, so this is just the entry banner, not a layout dump.
//
// Heuristic: walk panel.controls in order, return the text of the first
// CSWGuiLabel / CSWGuiLabelHilight child with announceable text. Buttons and
// other interactive controls are skipped — they get announced through the
// regular focus path. If no label exists we stay silent and rely on the
// SetActiveControl announcement of the focused child to orient the user.
static void AnnouncePanelTitle(void* panel) {
    if (!panel) return;

    // Diagnostic: one-shot dump of CSWGuiFeatsCharGen structure (chart
    // rows × cols, the four feat lists). No-ops on non-feat panels +
    // dedups per panel pointer. Used to plan main-panel accessibility
    // before the picker spec gets extended.
    acc::diag::chargen_feats::DumpStructureIfNeeded(panel);

    // Listbox-spec title override: any spec in menus_listbox.cpp can
    // declare its own title speech (used when the panel's .gui-baked
    // title is wrong — e.g. SkillInfoBox carries a BioWare placeholder
    // the chargen flow doesn't overwrite). Returns nullptr when no spec
    // matches or the matched spec has no override, in which case the
    // generic label-walk below runs.
    if (const char* override =
            acc::menus::listbox::GetTitleOverride(panel)) {
        acclog::Write("Menus.PanelWalk",
                      "title parent=%p (spec override) text=\"%s\"",
                      panel, override);
        prism::Speak(override, /*interrupt=*/false);
        return;
    }

    // Same hook for editbox-owning panels — CSWGuiNameChargen substitutes
    // subtitle_label ("Name") for the stale main_title_label
    // ("CHARAKTERAUSWAHL") that the generic walk would otherwise pick.
    if (const char* override =
            acc::menus::editbox::GetTitleOverride(panel)) {
        acclog::Write("Menus.PanelWalk",
                      "title parent=%p (editbox spec override) text=\"%s\"",
                      panel, override);
        prism::Speak(override, /*interrupt=*/false);
        return;
    }

    // CSWGuiPowersLevelUp shares the pwrlvlup.gui placeholder
    // ("CHARAKTERAUSWAHL" at id 0) — same problem chargen Name had.
    // Substitute the sub_title_label ("Kr�fte" at id 1).
    if (const char* override =
            acc::menus::powers_levelup::GetTitleOverride(panel)) {
        acclog::Write("Menus.PanelWalk",
                      "title parent=%p (powers_levelup override) text=\"%s\"",
                      panel, override);
        prism::Speak(override, /*interrupt=*/false);
        return;
    }

    // Title-screen main menu: the only label-like child is the optional
    // "New downloadable content is available…" Steam notice, which the
    // generic walk would pick. User-reported in
    // patch-20260530-191714.log — the DLC notice spoke instead of any
    // useful panel title, leaving the user disoriented on entry.
    //
    // This is also the bringup gate for the deferred background update
    // check: the engine is demonstrably past intro movies + DirectInput
    // init by the time MainMenu first-sights, so it's safe to start
    // WinHTTP I/O without racing Bink playback for COM/message-loop
    // state. The mark + StartBackgroundCheck calls are idempotent on
    // re-entry (we land here once per launch in practice; both functions
    // self-guard against repeats).
    if (IdentifyPanel(panel) == PanelKind::MainMenu) {
        acclog::BringupMark("main_menu_first_sight");
        // Cold-start DirectInput wake-up. On a fresh launch the engine reaches
        // the main menu with its DirectInput keyboard unacquired, so menu input
        // is dead until the user alt-tabs (which forces a SetActive(0)->(1)
        // edge). We replicate that edge here, at the first guaranteed-
        // interactive post-load moment.
        //
        // Must be the full 0->1 CYCLE, not a bare SetActive(1): on some
        // machines the engine arrives with active already a STALE 1 (flag set,
        // devices not actually acquired). A plain SetActive(1) no-ops against
        // the stuck flag and the keyboard stays dead — observed on tester
        // kenny's en-US build (patch-20260612-164351.log): EnsureInputAcquired
        // fired at first-sight yet the input pump did not go live for ~20s,
        // until his own alt-tab drove the 0->1 edge. ForceReacquireInput's
        // leading SetActive(0) guarantees the edge regardless of the prior flag
        // state, so the keyboard acquires immediately without an alt-tab.
        //
        // This is the cold-start edge only. Gate it on actually owning the
        // foreground: input must mirror foreground (the game holds the keyboard
        // only while it's in front), so acquiring here while an overlay/another
        // window is in front would bleed nav keys into the background game. If
        // we're not foreground yet, the regain edge (WM_ACTIVATEAPP active=1 ->
        // RequestInputReacquire) acquires when the game actually comes forward.
        if (acc::focus_guard::GameOwnsForeground()) {
            acc::engine::ForceReacquireInput();
        } else {
            acclog::Write("EngineInput",
                "cold-start acquire skipped: game not foreground at "
                "first-sight; the focus-regain edge will acquire");
        }
        acc::update_checker::StartBackgroundCheck();
        // Belt-and-braces — the polling thread spawned at OnRulesInit
        // should already have subclassed every game window by now, but
        // re-calling is a no-op and keeps the safety net in place in
        // case the thread didn't start (CreateThread failure logged).
        acc::focus_guard::StartFocusProbe();
        // Foreground-reclaim guard retired: input now mirrors foreground
        // (acquire on the regain edge, release on focus-loss — see
        // engine_input.h ReleaseInput / RequestInputRelease), so the keyboard
        // recovers on its own when the user returns to the game, with no need
        // to forcibly steal the window back. Stealing foreground also fought
        // screen-reader users who deliberately switch to another window. The
        // overlay-at-launch case (Game Bar) self-heals: while the overlay is in
        // front the game stays released (correct), and re-acquires the moment
        // it regains the foreground. ArmStartupForegroundGuard is intentionally
        // no longer called.
        const char* title = acc::strings::Get(acc::strings::Id::PanelTitleMainMenu);
        acclog::Write("Menus.PanelWalk",
                      "title parent=%p (main menu override) text=\"%s\"",
                      panel, title);
        prism::Speak(title, /*interrupt=*/false);
        return;
    }

    // Skip short numeric labels (tab indicators "1".."6", stat values "12",
    // skill values, etc.). They're never panel titles and pre-empt the real
    // ones — patch-20260519-154827.log frame 12586 hit this on the chargen
    // tab strip (1484E768): the first label child was "1" (the tab number
    // next to "Portrait"), so the user heard just "1" after submitting the
    // name. Bound: text up to 3 chars, every byte ASCII-digit.
    auto isShortNumeric = [](const char* s) {
        size_t n = 0;
        for (; s[n]; ++n) {
            if (n >= 3 || s[n] < '0' || s[n] > '9') return false;
        }
        return n > 0;
    };

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* child = list->data[i];
        if (!child) continue;
        if (CallDowncast(child, kVtableAsLabel) == nullptr &&
            CallDowncast(child, kVtableAsLabelHilight) == nullptr) {
            continue;
        }
        char text[256];
        if (acc::menus::extract::FromControl(child, text, sizeof(text), panel)) {
            if (isShortNumeric(text)) {
                acclog::Write("Menus.PanelWalk",
                              "title parent=%p label=%p text=\"%s\" "
                              "(skipped: short numeric)",
                              panel, child, text);
                continue;
            }
            acclog::Write("Menus.PanelWalk", "title parent=%p label=%p text=\"%s\"",
                          panel, child, text);
            prism::Speak(text, /*interrupt=*/false);
            return;
        }
    }
}

// Chargen class-icon cache pump. The hook fires at the very entry of
// CSWGuiPanel::SetActiveControl — before the function writes the new
// active_control and before any OnEnterButton runs for the new icon. At
// this moment panel.active_control still points at the OUTGOING icon
// (the one the user is leaving) and class_label still carries that
// icon's class string. Caching (outgoing -> label) on every transition
// closes the gap that the per-frame monitor leaves under rapid arrow
// input: the monitor only catches icons the user dwells on, but every
// SetActiveControl event fires regardless of duration.
void acc::menus::focus::PrefillClassIconCacheOnTransition(void* panel, void* newControl) {
    if (!panel) return;
    void** pVt = *reinterpret_cast<void***>(panel);
    if (reinterpret_cast<uintptr_t>(pVt) != kVtableCSWGuiClassSelection) return;
    void* outgoing = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(panel) + kPanelActiveControlOffset);
    if (!outgoing || outgoing == newControl) return;
    if (!IsClassSelectionIcon(panel, outgoing)) return;
    if (ClassLabelCacheLookup(panel, outgoing) != nullptr) return;
    void* classLabel = reinterpret_cast<unsigned char*>(panel) +
                       kClassSelectionClassLabelOffset;
    char name[256];
    if (ExtractTextOrStrRefIndirect(classLabel,
                                    kLabelTextOffset,
                                    kLabelStrRefOffset,
                                    kLabelTextObjectOffset,
                                    name, sizeof(name)) &&
        name[0] != '\0') {
        // Compose the description in the same breath. The cache is
        // first-write-wins, so a prefill that stored the name alone would
        // lock the entry and the blurb could never be added later.
        char text[acc::menus::detail::kAnnounceTextMax];
        acc::menus::detail::ComposeClassAnnounce(panel, name, text, sizeof(text));
        ClassLabelCacheStore(panel, outgoing, text);
        acclog::Write("Menus.PerKind",
                      "ClassSelection prefill outgoing=%p -> \"%s\"",
                      outgoing, text);
    }
}

// Update g_currentPanel on panel transitions. Tabbed-mode tab-cluster
// state DELIBERATELY persists across transitions into sub-dialogs of the
// tabbed panel — the tab strip lives on the parent panel (Options) and is
// still the right thing while the user is inside one of its sub-dialogs.
// ValidateTabbedPanel drops the cluster state when the engine frees the
// underlying panel.
void acc::menus::focus::UpdateFocusedPanelState(void* panel) {
    g_currentPanel = panel;
}

// First focus event into a previously-unseen panel: dump every child
// control + capture cycle-button category text. Cycle widgets carry their
// localized category in their CExoString at panel construction time; the
// engine replaces it with the persisted value (e.g. "Difficulty" -> "Normal")
// shortly after, and our FireActivate calls overwrite it again on each
// cycle. SetActiveControl's first fire on a new panel is the earliest
// reachable capture point.
void acc::menus::focus::WalkAndCaptureOnFirstSight(void* panel) {
    static void* s_lastPanel = nullptr;
    if (!panel || panel == s_lastPanel) return;
    s_lastPanel = panel;

    LogManagerStack(*reinterpret_cast<void**>(kAddrGuiManagerPtr),
                    "panel-walk");
    PanelKind kind = IdentifyPanel(panel);
    // Header (panel=/kind=) is folded into the walk block by WalkChildren so
    // the whole snapshot collapses as one unit on identical re-walks.
    WalkChildren("Menus.PanelWalk", panel, kPanelControlsOffset,
                 PanelKindName(kind));

    // Capture cycle-button categories before any activation rewrites the
    // value-display button's CExoString. The cache lives in
    // menus_extract.cpp; we write through its public setter.
    acc::menus::extract::ResetCycleCategoryCache();
    auto* plist = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!plist || !plist->data || plist->size <= 0) return;

    int pn = plist->size > 256 ? 256 : plist->size;
    for (int i = 0; i < pn; ++i) {
        void* c = plist->data[i];
        if (!c) continue;
        if (CallDowncast(c, kVtableAsButton) == nullptr) continue;
        if (IsToggle(c)) continue;
        void* leftN  = FindAdjacentArrow(panel, c, /*toRight=*/false);
        void* rightN = FindAdjacentArrow(panel, c, /*toRight=*/true);
        if (!leftN && !rightN) continue;

        char text[128];
        bool gotText = false;
        uint32_t strref = ReadU32(c, kButtonStrRefOffset);
        if (LookupTlk(strref, text, sizeof(text))) {
            gotText = true;
        } else if (ReadCExoString(c, kButtonTextOffset, text, sizeof(text))) {
            gotText = true;
        }
        if (gotText) {
            acc::menus::extract::CaptureCycleCategory(c, text);
            acclog::Write("Menus.CycleCategory", "control=%p text=\"%s\" strref=%u",
                          c, text, strref);
        }
    }

    // Chargen "Attribute" panel: each value button has an inline text "8"
    // (the current value), so the generic capture above resolves the
    // category to the value itself (useless). Override here by reading
    // ability_labels[i] for each ability_buttons[i] and binding the pair
    // into the cycle-category cache so FromControl produces "Stärke, 8".
    acc::menus::chargen_attr::CaptureLabelsIfApplicable(panel);

    // Same for the Skills panel — value buttons all read "0" initially
    // and would otherwise resolve to "0" as the category. Bind the
    // skill_labels[i] text in instead.
    acc::menus::chargen_skills::CaptureLabelsIfApplicable(panel);

    // Chargen class-selection panel: snapshot LBL_DESC's authored text
    // before the user hovers anything. Both games ship it with a
    // placeholder (empty on KOTOR 1, "This is just a test line." repeated
    // on KOTOR 2) and fill in the real class description on hover, so this
    // snapshot is what lets the announce tell content from default. This
    // walk is the last moment that is guaranteed to precede the engine's
    // first OnEnterButton.
    if (acc::engine::HasVtable(panel, kVtableCSWGuiClassSelection)) {
        void* descLabel = acc::menus::detail::FindControlById(panel, kClassSelDescLabelId);
        char desc[384] = {0};
        if (descLabel) {
            acc::engine::ReadLabelText(descLabel, desc, sizeof(desc));
        }
        acc::menus::detail::ClassDescBaselineCapture(panel, desc);
        acclog::Write("Menus.PerKind",
                      "ClassSelection LBL_DESC baseline label=%p text=\"%.120s\"",
                      descLabel, desc);
    }
}

// First focus into a new panel: speak its title once. The focused
// control's announcement still fires after, so the user hears
// "<panel title>, <focused control>" on entry.
//
// In-game sub-screens (Inventar, Karte, Optionen, …) are skipped here:
// AnnounceNewSubScreens in menus_monitors.cpp announces them with the
// TLK-localized strref name on push. Doing both produces "Optionen,
// Spiel speichern, Optionen" on Options open (panel title + focus +
// sub-screen monitor) instead of the intended "Optionen, Spiel
// speichern" sequence.
void acc::menus::focus::SpeakPanelTitleOnFirstSight(void* panel) {
    if (!panel || panel == g_lastTitledPanel) return;
    g_lastTitledPanel = panel;
    PanelKind kind = IdentifyPanel(panel);
    if (acc::menus::monitors::IsInGameSubScreenKind(kind)) {
        return;
    }
    // Dialog panels: dialog_speech.cpp is the authoritative announcer
    // (with race/language suppression). The generic label-walk would
    // speak the dialog text as the "title" the first time SetActive
    // fires on the panel — bypassing suppression.
    if (kind == PanelKind::DialogCinematicCopy ||
        kind == PanelKind::DialogLetterbox1 ||
        kind == PanelKind::DialogLetterbox2 ||
        kind == PanelKind::DialogLetterbox3) {
        return;
    }
    AnnouncePanelTitle(panel);
}

// Record the new focused control into the pending-announce slot for the
// drain to read on the next tick. Logs the event diagnostically — the
// announce itself happens in DrainPendingAnnounce. Container-listbox
// suppression stays at write-time: drowning the per-row container monitor
// would still be wrong, so we just don't queue.
void acc::menus::focus::AnnounceNewFocusedControl(int n, void* panel, void* newControl) {
    // Mapped tutorial popups (Surface 1) and our synthetic Trask popup (Surface
    // 2): the message body is spoken as a keyboard hint by the content-fingerprint
    // monitor, and the buttons read on arrow-nav. Suppress the engine-focus-driven
    // announce of the raw control here so neither the mouse body nor a button
    // (e.g. "Abbrechen") is read on the popup's first-sight SetActiveControl.
    if (IdentifyPanel(panel) == PanelKind::TutorialBox &&
        (acc::tutorial_popup::SyntheticActive() ||
         acc::tutorial_hints::HintForTutorialRow(
             acc::tutorial_hints::ReadTutorialRow(panel)) != nullptr)) {
        return;
    }

    int id = *reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(newControl) + kControlIdOffset);

    char text[256];
    const char* source = acc::menus::extract::FromControl(
        newControl, text, sizeof(text), panel);

    bool suppressForContainer =
        IsListBox(newControl) &&
        IdentifyPanel(panel) == PanelKind::Container;

    // Store mode flip (ShowBuyGUI / ShowSellGUI) ends with the engine
    // calling SetActiveControl on whichever item-listbox just became
    // visible. FromControl on a listbox returns the concatenation of
    // every row label — for a merchant with 30 items that's a full
    // inventory dump after every G press. We already announce "Modus
    // Kaufen" / "Modus Verkaufen" from TickMonitorMode and rebind the
    // chain; the user can Up/Down to hear individual items. Suppress
    // the listbox-blob speech the same way Container does.
    bool suppressForStore =
        IsListBox(newControl) &&
        acc::menus::store::IsStorePanel(panel);

    if (source) {
        acclog::Write("Menus.SetActive", "#%d panel=%p new=%p id=%d src=%s text=\"%s\"",
                      n, panel, newControl, id, source, text);
    } else {
        char vtbl[160];
        DumpControlVtable(newControl, vtbl, sizeof(vtbl));
        acclog::Write("Menus.SetActive", "#%d panel=%p new=%p id=%d src=none %s",
                      n, panel, newControl, id, vtbl);
    }

    if (suppressForContainer || suppressForStore) return;

    // Cross-panel overwrite guard. The slot-collapse model assumes the
    // intra-tick burst stays on one panel — engine fires NULL → first →
    // settled on the just-opened panel, last write wins, drain speaks the
    // settled control. But MessageBox-open same-tick also fires a follow-up
    // SetActive on the underlying panel's listbox (engine refresh of the
    // now-visible row). Without this guard that fourth fire overwrites the
    // MessageBox's Abbrechen with the listbox's row text, which dedups
    // against what was already spoken and the popup announces nothing.
    //
    // Fix: when the new SetActive lands on a different panel than the
    // current pending, flush the previous pending first. Same-panel bursts
    // still collapse (the common case stays cheap). Drain re-uses the full
    // path including chain-coherence drop.
    if (acc::menus::s_pendingAnnouncePanel != nullptr &&
        acc::menus::s_pendingAnnouncePanel != panel) {
        acc::menus::DrainPendingAnnounce();
    }

    acc::menus::s_pendingAnnouncePanel   = panel;
    acc::menus::s_pendingAnnounceControl = newControl;
}

