// general per-tick monitors.
//
// Post-Step-5 cleanup. See menus_monitors.h for the rationale and
// public surface. Function bodies are unchanged from the inline
// originals in menus.cpp; only namespacing and linkage changed.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "menus_monitors.h"

#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "log.h"
#include "menus.h"            // SetDrilledIntoSubScreen — sole drill-arm site
#include "menus_charsheet.h"
#include "menus_chargen_attr.h"
#include "menus_chargen_skills.h"
#include "menus_chain.h"
#include "menus_extract.h"
#include "menus_internal.h"
#include "menus_journal.h"   // LogEntryCounts — entry-count completeness diagnostic
#include "strings.h"
#include "prism.h"

using namespace acc::engine;

using acc::menus::detail::IsClassSelectionIcon;
using acc::menus::detail::FindListBoxChild;

// g_currentPanel still owns its definition in menus.cpp (set by
// OnSetActiveControl). Read via extern from menus_internal.h.

namespace acc::menus::monitors {

// ============================================================================
// Focus monitor — re-extracts the focused chain entry's text each tick and
// speaks the diff. State (last-seen control + text) is shared with
// AnnounceControl, which keeps it in sync after voluntary speak events.
// ============================================================================

namespace {
void* s_focusMonitorControl = nullptr;
char  s_focusMonitorText[256] = {0};
}

void AnnounceControl(void* control) {
    if (!control) return;

    // Multi-row listbox guard. The engine fires SetActiveControl on the
    // listbox container as part of panel construction (auto-focus on
    // open) before the user has navigated anywhere. The container itself
    // is not a focusable terminal — rows are exposed via the
    // ListBoxPanelSpec / chain handlers as the user arrows into them.
    // Speaking the container's "control N" fallback on panel open is
    // pure noise; the panel-name announce already covers the open event.
    //
    // Single-row listboxes (CSWGuiMessageBox-style modals carrying the
    // dialog text in a single row) DO need to speak — FromControl
    // returns the row text and we fall through to the success path.
    //
    // The "never silence the fallback" rule in MEMORY.md applies to
    // user-driven navigation events where missing the announce makes
    // focus feel frozen; engine-driven panel-open auto-focus on a
    // container is a different class of event.
    if (IsListBox(control)) {
        auto* lb = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(control) + kListBoxControlsOffset);
        if (lb && lb->data && lb->size > 1) {
            return;
        }
    }

    char text[256];
    const char* source = acc::menus::extract::FromControl(control, text, sizeof(text));
    if (source) {
        prism::Speak(text, /*interrupt=*/false);
        // Prime channel-0 dedup so the engine's post-nav SetActive echo
        // (which lands in the pending-announce slot and gets drained next
        // tick) sees the same text as last-spoken and stays silent.
        acc::menus::MarkSpoken(/*channel=*/0, text);
        s_focusMonitorControl = control;
        strncpy_s(s_focusMonitorText, text, _TRUNCATE);
        return;
    }
    if (g_currentPanel && IsClassSelectionIcon(g_currentPanel, control)) {
        return;
    }
    int id = *reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(control) + kControlIdOffset);
    char placeholder[64];
    snprintf(placeholder, sizeof(placeholder), "control %d", id);
    prism::Speak(placeholder, /*interrupt=*/false);
}

namespace {

void MonitorFocusedControl() {
    if (acc::menus::chain::g_chainCount <= 0 ||
        acc::menus::chain::g_chainIndex < 0 ||
        acc::menus::chain::g_chainIndex >= acc::menus::chain::g_chainCount) {
        return;
    }
    if (acc::menus::chain::g_chainPanel != g_currentPanel) {
        // CSWGuiPortraitCharGen is pushed by the parent chargen sub-menu
        // (Eigener Charakter) without firing our OnSetActiveControl hook,
        // so g_currentPanel stays on the parent and the gate would
        // suppress text-change re-announce on every cycle. Bypass the
        // gate when the chain panel itself is a PortraitCharGen — its
        // chain rebind is the authoritative signal that we're on it.
        // Without this, the user has to nav-down + nav-up to refocus
        // and force a fresh AnnounceControl just to hear the cycled
        // value.
        void* chainPanel = acc::menus::chain::g_chainPanel;
        if (!chainPanel) return;
        void** vt = *reinterpret_cast<void***>(chainPanel);
        if (reinterpret_cast<uintptr_t>(vt) !=
                kVtableCSWGuiPortraitCharGen) {
            return;
        }
    }
    void* focused = acc::menus::chain::g_chain[acc::menus::chain::g_chainIndex].control;
    if (!focused) return;

    char text[256];
    const char* source = acc::menus::extract::FromControl(
        focused, text, sizeof(text), acc::menus::chain::g_chainPanel);
    if (!source) return;

    if (focused == s_focusMonitorControl) {
        if (strncmp(s_focusMonitorText, text, sizeof(s_focusMonitorText)) != 0) {
            // Chargen Attribute / Skills panels: when a +/- press
            // changes the focused row's value, override the default
            // "{label}, {new_value}" re-announce with the panel's
            // own value-change format. Returns true when a panel-
            // specific override fired; we still update the monitor's
            // last-text snapshot so the next tick doesn't re-fire on
            // the same diff.
            void* chainPanel = acc::menus::chain::g_chainPanel;
            bool handled =
                acc::menus::chargen_attr::AnnounceValueChange(
                    chainPanel, focused) ||
                acc::menus::chargen_skills::AnnounceValueChange(
                    chainPanel, focused);
            if (!handled) {
                prism::Speak(text, /*interrupt=*/false);
            }
            strncpy_s(s_focusMonitorText, text, _TRUNCATE);
            acclog::Write("Monitor", "focused=%p text changed -> \"%s\"%s",
                          focused, text,
                          handled ? " (chargen override)" : "");
        }
    } else {
        s_focusMonitorControl = focused;
        strncpy_s(s_focusMonitorText, text, _TRUNCATE);
        prism::Speak(text, /*interrupt=*/false);
        acclog::Write("Monitor", "focus changed -> %p text=\"%s\"",
                      focused, text);
    }
}

// ============================================================================
// Sub-screen tracking. Spec table + visible-set + announce-new pass. Used
// by MonitorPanelContents (this TU) and the drill router in menus.cpp
// (exposed via FindActiveSubScreenPanel + IsInGameSubScreenKind below).
// ============================================================================

// Spec order = the icon strip's left-to-right visual order, which is what
// Tab / Shift+Tab cycle through. `guiId` is the engine's own sub-screen
// index used by CGuiInGame::SwitchToSWInGameGui — not the same as strip
// position (CGuiInGame allocates its slots in 0x0c..0x28 order, which
// puts Map and Messages in the "wrong" spots from a strip POV). Keeping
// both in one row lets the Tab handler look up the next strip neighbour
// and the engine GUI_id in a single pass.
struct InGameSubScreenSpec {
    PanelKind   kind;
    int         guiId;
    uint32_t    strref;
    const char* literal;
};

const InGameSubScreenSpec k_inGameSubScreens[] = {
    { PanelKind::InGameEquip,     0, 0xFFFFFFFFu, "Ausr\xfcstung" },
    { PanelKind::InGameInventory, 1, 48220u,      "Inventar" },
    { PanelKind::InGameCharacter, 2, 48225u,      "Charakterblatt" },
    { PanelKind::InGameMap,       6, 48221u,      "Karte" },
    { PanelKind::InGameAbilities, 3, 48224u,      "F\xe4higkeiten" },
    { PanelKind::InGameJournal,   5, 48218u,      "Auftr\xe4ge" },
    { PanelKind::InGameOptions,   7, 48222u,      "Optionen" },
    { PanelKind::InGameMessages,  4, 48223u,      "Nachrichten" },
    // Quest-items modal pushed by the journal's "Auftrags-Gegenstände" button.
    // Not a strip sub-screen (no icon → guiId -1); its title is read live from
    // LBL_TITLE in the announce path, so strref/literal are unused sentinels.
    { PanelKind::InGameQuestItems, -1, 0xFFFFFFFFu, nullptr },
};
constexpr int k_inGameSubScreenCount =
    sizeof(k_inGameSubScreens) / sizeof(k_inGameSubScreens[0]);

const InGameSubScreenSpec* FindSpec(PanelKind k) {
    for (const auto& s : k_inGameSubScreens) {
        if (s.kind == k) return &s;
    }
    return nullptr;
}

void* s_visibleSubScreens[16];
int   s_visibleSubScreenCount = 0;

bool IsSubScreenTracked(void* p) {
    for (int i = 0; i < s_visibleSubScreenCount; ++i) {
        if (s_visibleSubScreens[i] == p) return true;
    }
    return false;
}

void AnnounceNewSubScreens(void** panels, int count) {
    void* nowVisible[16];
    int   nowCount = 0;
    bool  newSubScreenDetected = false;
    for (int i = 0; i < count && nowCount < 16; ++i) {
        void* p = panels[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        const InGameSubScreenSpec* spec = FindSpec(k);
        if (!spec) continue;
        nowVisible[nowCount++] = p;
        if (IsSubScreenTracked(p)) continue;
        newSubScreenDetected = true;

        // QuestItem modal has no static strip strref/literal; read its
        // LBL_TITLE (+0x768) live so the announce is correct and localized.
        // Tracked via the normal s_visibleSubScreens machinery, so closing
        // and reopening re-announces.
        if (k == PanelKind::InGameQuestItems) {
            char title[128];
            void* titleLabel = reinterpret_cast<unsigned char*>(p) + 0x768;
            if (ReadGuiString(titleLabel, kLabelGuiStringPtrOffset,
                              title, sizeof(title)) && title[0] != '\0') {
                acclog::Write("Menus.SubScreen",
                              "panel=%p kind=%s title=\"%s\" (dynamic)",
                              p, PanelKindName(k), title);
                prism::Speak(title, /*interrupt=*/false);
            } else {
                acclog::Write("Menus.SubScreen",
                              "panel=%p kind=%s LBL_TITLE empty; silent",
                              p, PanelKindName(k));
            }
            continue;
        }

        char text[128];
        bool spoke = false;
        if (spec->strref != 0xFFFFFFFFu &&
            LookupTlk(spec->strref, text, sizeof(text))) {
            acclog::Write("Menus.SubScreen", "panel=%p kind=%s strref=%u text=\"%s\"",
                          p, PanelKindName(k), spec->strref, text);
            prism::Speak(text, /*interrupt=*/false);
            spoke = true;
        }
        if (!spoke) {
            acclog::Write("Menus.SubScreen", "panel=%p kind=%s text=\"%s\" (literal)",
                          p, PanelKindName(k), spec->literal);
            prism::Speak(spec->literal, /*interrupt=*/false);
        }

        // (Charakterblatt no longer reads the full stat block on open —
        // legacy workaround from when the panel wasn't keyboard-navigable.
        // Stats are now reachable via in-panel arrow navigation; the panel
        // name spoken above is the sole open-announce, matching every
        // other strip sub-screen.)

        // Diagnostic: confirm the journal surfaces every entry the engine has.
        if (k == PanelKind::InGameJournal) {
            acc::menus::journal::LogEntryCounts(p);
        }
    }
    memcpy(s_visibleSubScreens, nowVisible, sizeof(nowVisible));
    s_visibleSubScreenCount = nowCount;

    // Sole drill-arm site. Triggers on any newly-visible sub-screen kind
    // (Inventar, Karte, Auftraege, …) regardless of which engine function
    // opened it — ShowSWInGameGui (cold path, e.g. Esc → Options),
    // SwitchToSWInGameGui (warm path, strip-icon Enter / hotkey while a
    // sub-screen is already up), our own Tab cycle queue, or any future
    // engine path. Polling panels[] is the only single-site approach that
    // covers them all (we tried hooking SwitchToSWInGameGui as the single
    // event source — it missed the cold path entirely and left the strip
    // chain-navigable on first Esc). One-tick latency between push and
    // arm is invisible to the user. Idempotent — Tab cycling re-detects
    // the same sub-screen the next tick but the guard skips.
    if (newSubScreenDetected && !acc::menus::IsDrilledIntoSubScreen()) {
        acc::menus::SetDrilledIntoSubScreen(true);
        acclog::Write("Drill",
                      "auto-armed on new sub-screen in panels[]");
    }
}

// ============================================================================
// Content fingerprint monitor.
// ============================================================================

struct ContentSnapshot {
    void* panel;
    char  text[8192];
};
constexpr int kMaxContentSnapshots = 8;
ContentSnapshot s_contentSnapshots[kMaxContentSnapshots];
int s_contentSnapshotCount = 0;

bool IsContentMonitored(PanelKind k) {
    switch (k) {
    case PanelKind::TutorialBox:
    // Dialog* panels and BarkBubble are owned by dialog_speech.cpp.
    // It reads message_label, replies count, computer-terminal lines,
    // and bark text — and gates the NPC + bark line speech through the
    // HumanSubtitles toggle. Letting this generic content fingerprint
    // also speak the dialog text caused a duplicate-speech race
    // (verified 2026-05-30 — user heard Carth's lines via this path
    // even after dialog_speech suppressed them).
    //
    // InGameMessages (combat log) and Container (loot panels) are owned
    // by the ListBoxPanelSpec paths in menus_listbox.cpp — each focused
    // row is announced once on Up/Down nav (verified live 2026-05-30
    // for Container). The fingerprint path was re-speaking the same
    // rows as generic controls; removing it eliminates the duplicate
    // without losing coverage.
    // InGameInventory and InGameEquip removed 2026-05-30 — the engine
    // populates the stat sidebar one tick AFTER the panel first appears
    // (placeholder header labels "VIT" / "DEF" / "Ausw." flip into real
    // values "6/6" / "15" / "Angriff 0" etc.), which the fingerprint
    // diff path treated as new content and spoke as a batch on every
    // open. Per-row navigation inside the panels covers the rest.
    case PanelKind::MessageBoxModal:
    case PanelKind::AreaTransition:
    // StatusSummary is the engine's generic info popup (quest-progress and
    // journal-entry notices, skill-check results, "you received X"). Its body
    // is a cluster of per-notification labels plus a lone OK button, but it
    // was never content-monitored, so only the chain's OK button got read on
    // nav and the body text was silent (observed live 2026-06-03, StatusSummary
    // popup after entering Taris Südliche Oberstadt). It has no
    // InGameSubScreenSpec, so first-sight speaks the body once. BuildContent
    // Fingerprint filters to the visible row(s) below — see that note.
    case PanelKind::StatusSummary:
    case PanelKind::InGameMap:
    case PanelKind::InGameJournal:
    case PanelKind::InGameCharacter:
        return true;
    // InGameAbilities removed 2026-06-03: the dedicated handler in
    // menus_abilities.cpp owns every announcement on this screen now, so the
    // content fingerprint only double-spoke (the skill stats after a nav) and
    // re-spoke the new tab's first entry over the tab-name announce on a tab
    // switch. The screen-open title still comes from the sub-screen path.
    default:
        return false;
    }
}

void BuildContentFingerprint(void* panel, char* out, size_t outSize) {
    if (outSize == 0) return;
    out[0] = '\0';
    if (!panel) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 32 ? 32 : list->size;
    PanelKind kind = IdentifyPanel(panel);
    size_t off = 0;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        if (CallDowncast(c, kVtableAsButton) != nullptr) continue;
        if (CallDowncast(c, kVtableAsButtonToggle) != nullptr) continue;
        if ((kind == PanelKind::Container ||
             kind == PanelKind::InGameEquip) && IsListBox(c)) continue;
        // StatusSummary lays out one label per notification type and shows
        // only the applicable row(s); hidden templates keep their "<CUSTOM0>"
        // placeholder text. Read only rows carrying the visible bit so speech
        // reflects what's on screen, not all eight templates.
        if (kind == PanelKind::StatusSummary) {
            uint32_t bitFlags = *reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(c) + kControlBitFlagsOffset);
            if ((bitFlags & kControlVisibleBit) == 0) continue;
        }

        char text[4096];
        const char* src = acc::menus::extract::FromControl(c, text, sizeof(text), panel);
        if (!src) continue;
        size_t tlen = strnlen(text, sizeof(text));
        if (tlen == 0) continue;

        bool allWs = true;
        for (size_t k = 0; k < tlen; ++k) {
            char ch = text[k];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                allWs = false; break;
            }
        }
        if (allWs) continue;

        size_t needed = (off > 0 ? 3 : 0) + tlen;
        if (off + needed + 1 >= outSize) break;
        if (off > 0) {
            out[off++] = ' ';
            out[off++] = '|';
            out[off++] = ' ';
        }
        memcpy(out + off, text, tlen);
        off += tlen;
        out[off] = '\0';
    }
}

bool FingerprintContainsSegment(const char* hay, size_t hayLen,
                                const char* seg, size_t segLen) {
    if (segLen == 0 || segLen > hayLen) return false;
    const char* sep = " | ";
    const size_t sepLen = 3;
    size_t i = 0;
    while (i + segLen <= hayLen) {
        if (memcmp(hay + i, seg, segLen) == 0) {
            bool startOk = (i == 0) ||
                (i >= sepLen && memcmp(hay + i - sepLen, sep, sepLen) == 0);
            bool endOk = (i + segLen == hayLen) ||
                (i + segLen + sepLen <= hayLen &&
                 memcmp(hay + i + segLen, sep, sepLen) == 0);
            if (startOk && endOk) return true;
        }
        ++i;
    }
    return false;
}

void SpeakNewSegments(const char* prev, const char* curr) {
    const char* sep = " | ";
    const size_t sepLen = 3;
    size_t prevLen = strlen(prev);
    const char* p = curr;
    while (*p) {
        const char* end = strstr(p, sep);
        size_t segLen = end ? (size_t)(end - p) : strlen(p);
        if (segLen > 0 &&
            !FingerprintContainsSegment(prev, prevLen, p, segLen)) {
            char seg[8192];
            size_t cp = segLen < sizeof(seg) - 1 ? segLen : sizeof(seg) - 1;
            memcpy(seg, p, cp);
            seg[cp] = '\0';
            prism::Speak(seg, /*interrupt=*/false);
            acclog::Write("ContentChange", "  spoke \"%s\"", seg);
        }
        if (!end) break;
        p = end + sepLen;
    }
}

char* GetContentSnapshot(void* panel) {
    for (int i = 0; i < s_contentSnapshotCount; ++i) {
        if (s_contentSnapshots[i].panel == panel) {
            return s_contentSnapshots[i].text;
        }
    }
    if (s_contentSnapshotCount >= kMaxContentSnapshots) {
        memmove(s_contentSnapshots, s_contentSnapshots + 1,
                sizeof(s_contentSnapshots[0]) * (kMaxContentSnapshots - 1));
        s_contentSnapshotCount = kMaxContentSnapshots - 1;
    }
    int idx = s_contentSnapshotCount++;
    s_contentSnapshots[idx].panel = panel;
    s_contentSnapshots[idx].text[0] = '\0';
    return s_contentSnapshots[idx].text;
}

void MonitorPanelContents() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return;
    if (panelCount > 16) panelCount = 16;

    AnnounceNewSubScreens(panelData, panelCount);

    for (int i = 0; i < panelCount; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        if (!IsContentMonitored(k)) continue;

        char fingerprint[8192];
        BuildContentFingerprint(p, fingerprint, sizeof(fingerprint));

        char* last = GetContentSnapshot(p);
        if (strncmp(last, fingerprint, sizeof(s_contentSnapshots[0].text)) == 0) {
            continue;
        }

        bool firstSight = (last[0] == '\0');
        bool suppressFirstSight =
            firstSight &&
            (k == PanelKind::Container || FindSpec(k) != nullptr);
        if (suppressFirstSight) {
            strncpy_s(last, sizeof(s_contentSnapshots[0].text),
                      fingerprint, _TRUNCATE);
            acclog::Write("ContentChange", "panel=%p kind=%s first-sight snapshot "
                          "(deferring to kind-name path): \"%.200s\"",
                          p, PanelKindName(k), fingerprint);
            continue;
        }

        if (fingerprint[0] != '\0') {
            acclog::Write("ContentChange", "panel=%p kind=%s",
                          p, PanelKindName(k));
            acclog::Write("ContentChange", "  prev=\"%.300s\"", last);
            acclog::Write("ContentChange", "  curr=\"%.300s\"", fingerprint);
            SpeakNewSegments(last, fingerprint);

            // If the chain is bound to this panel, an engine-driven content
            // change can alter which virtual rows belong in the chain — the
            // character sheet's Force-points row appears/disappears with the
            // displayed character's Jedi-ness on Tab (leader switch). The
            // panel pointer is unchanged across a Tab, so HandleNavStep's
            // "activePanel != g_chainPanel" rebind never fires and the row
            // set goes stale. Rebuild here (preserving the cursor) so the
            // chain tracks live content, matching how the rest of the sheet
            // updates dynamically on Tab.
            if (acc::menus::chain::g_chainPanel == p) {
                acc::menus::chain::RebindChainPreserveIndex(p);
            }
        } else {
            acclog::Write("ContentChange", "panel=%p kind=%s fingerprint cleared "
                          "(prev=\"%.100s\")", p, PanelKindName(k), last);
        }
        strncpy_s(last, sizeof(s_contentSnapshots[0].text),
                  fingerprint, _TRUNCATE);
    }
}

// ============================================================================
// Dialog reply selection monitor.
// ============================================================================

struct DialogReplyState {
    void* listBox;
    short lastSelection;
};
DialogReplyState s_dialogReplyState = { nullptr, -1 };

bool IsDialogPanelKind(PanelKind k) {
    switch (k) {
    case PanelKind::DialogCinematic:
    case PanelKind::DialogCinematicCopy:
    case PanelKind::DialogComputer:
    case PanelKind::DialogComputerCamera:
        return true;
    default:
        return false;
    }
}

void MonitorDialogReplies() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;

    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);

    void* dialogPanel = nullptr;
    PanelKind dialogKind = PanelKind::Unknown;
    if (panelData && panelCount > 0) {
        int n = panelCount > 16 ? 16 : panelCount;
        for (int i = 0; i < n; ++i) {
            void* p = panelData[i];
            if (!p) continue;
            PanelKind pk = IdentifyPanel(p);
            if (IsDialogPanelKind(pk)) {
                dialogPanel = p;
                dialogKind  = pk;
                break;
            }
        }
    }

    if (!dialogPanel) {
        if (s_dialogReplyState.listBox) {
            acclog::Write("Menus.DialogReply", "monitor disarmed: no dialog panel in stack");
            s_dialogReplyState.listBox = nullptr;
            s_dialogReplyState.lastSelection = -1;
        }
        return;
    }

    void* lb = FindListBoxChild(dialogPanel);
    if (!lb) return;
    PanelKind k = dialogKind;
    void* fg = dialogPanel;

    short selIdx = *reinterpret_cast<short*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxSelectionIndexOffset);

    if (s_dialogReplyState.listBox != lb) {
        s_dialogReplyState.listBox = lb;
        s_dialogReplyState.lastSelection = selIdx;
        acclog::Write("Menus.DialogReply", "monitor armed: panel=%p kind=%s listbox=%p "
                      "initialSel=%d", fg, PanelKindName(k), lb, selIdx);
        return;
    }

    if (selIdx == s_dialogReplyState.lastSelection) return;
    short prev = s_dialogReplyState.lastSelection;
    s_dialogReplyState.lastSelection = selIdx;

    if (selIdx < 0) {
        acclog::Write("Menus.DialogReply", "selection cleared: listbox=%p prev=%d",
                      lb, prev);
        return;
    }

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    if (!lbList || !lbList->data || selIdx >= lbList->size) {
        acclog::Write("Menus.DialogReply", "selection out of range: listbox=%p sel=%d "
                      "size=%d", lb, selIdx,
                      (lbList ? lbList->size : -1));
        return;
    }

    void* row = lbList->data[selIdx];
    if (!row) return;

    char text[256];
    const char* src = acc::menus::extract::FromControl(row, text, sizeof(text));
    if (src) {
        acclog::Write("Menus.DialogReply", "selected: panel=%p kind=%s listbox=%p "
                      "sel=%d (was %d) src=%s text=\"%s\"",
                      fg, PanelKindName(k), lb, selIdx, prev, src, text);
        prism::Speak(text, /*interrupt=*/false);
    } else {
        char vtbl[160];
        DumpControlVtable(row, vtbl, sizeof(vtbl));
        acclog::Write("Menus.DialogReply", "selected (src=none): panel=%p listbox=%p "
                      "sel=%d row=%p %s", fg, lb, selIdx, row, vtbl);
    }
}

}  // namespace

// ============================================================================
// Public surface.
// ============================================================================

void TickGeneralMonitors() {
    // First: drain the pending-announce slot. Multiple SetActive events
    // fired within this tick collapsed to the last write — speak it now
    // (or stay silent if the voluntary AnnounceControl path already spoke
    // matching text and primed the channel-0 dedup).
    acc::menus::DrainPendingAnnounce();
    MonitorFocusedControl();
    MonitorPanelContents();
    MonitorDialogReplies();
    // Re-assert chargen Attribute panel's selected_ability against chain
    // focus. The engine's cursor-warp → OnEnterPointsButton path silently
    // overwrites this field between our chain-step sync and the queued
    // FireActivate from Left/Right; this monitor runs before
    // TickPendingOps in the same OnUpdate, so OnPlusButton reads the
    // correct value when the FireActivate fires. No-op on every other
    // panel.
    acc::menus::chargen_attr::SyncSelectedAbilityFromChainFocus();
    // Same defense on the Skills panel — different field
    // (selected_skill_index) but same engine-overwrite race.
    acc::menus::chargen_skills::SyncSelectedSkillFromChainFocus();
}

void* FindActiveSubScreenPanel() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return nullptr;
    if (panelCount > 16) panelCount = 16;
    for (int i = 0; i < panelCount; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        if (FindSpec(k)) return p;
    }
    return nullptr;
}

bool IsInGameSubScreenKind(PanelKind k) {
    return FindSpec(k) != nullptr;
}

}  // namespace acc::menus::monitors
