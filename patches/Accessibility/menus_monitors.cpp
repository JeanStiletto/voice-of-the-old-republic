// KOTOR Accessibility — general per-tick monitors.
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
#include "menus_charsheet.h"
#include "menus_chain.h"
#include "menus_extract.h"
#include "menus_internal.h"
#include "strings.h"
#include "tolk.h"

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
    char text[256];
    const char* source = acc::menus::extract::FromControl(control, text, sizeof(text));
    if (source) {
        tolk::Speak(text, /*interrupt=*/false);
        s_focusMonitorControl = control;
        strncpy_s(s_focusMonitorText, text, _TRUNCATE);
        return;
    }
    if (g_currentPanel && IsClassSelectionIcon(g_currentPanel, control)) {
        return;
    }
    int id = *reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(control) + 0x50);
    char placeholder[64];
    snprintf(placeholder, sizeof(placeholder), "control %d", id);
    tolk::Speak(placeholder, /*interrupt=*/false);
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
            tolk::Speak(text, /*interrupt=*/false);
            strncpy_s(s_focusMonitorText, text, _TRUNCATE);
            acclog::Write("Monitor", "focused=%p text changed -> \"%s\"",
                          focused, text);
        }
    } else {
        s_focusMonitorControl = focused;
        strncpy_s(s_focusMonitorText, text, _TRUNCATE);
        tolk::Speak(text, /*interrupt=*/false);
        acclog::Write("Monitor", "focus changed -> %p text=\"%s\"",
                      focused, text);
    }
}

// ============================================================================
// Sub-screen tracking. Spec table + visible-set + announce-new pass. Used
// by MonitorPanelContents (this TU) and the drill router in menus.cpp
// (exposed via FindActiveSubScreenPanel + IsInGameSubScreenKind below).
// ============================================================================

struct InGameSubScreenSpec {
    PanelKind   kind;
    uint32_t    strref;
    const char* literal;
};

const InGameSubScreenSpec k_inGameSubScreens[] = {
    { PanelKind::InGameEquip,     0xFFFFFFFFu, "Ausr\xfcstung" },
    { PanelKind::InGameInventory, 48220u,      "Inventar" },
    { PanelKind::InGameCharacter, 48225u,      "Charakterblatt" },
    { PanelKind::InGameMap,       48221u,      "Karte" },
    { PanelKind::InGameAbilities, 48224u,      "F\xe4higkeiten" },
    { PanelKind::InGameJournal,   48218u,      "Auftr\xe4ge" },
    { PanelKind::InGameOptions,   48222u,      "Optionen" },
    { PanelKind::InGameMessages,  48223u,      "Nachrichten" },
};

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
    for (int i = 0; i < count && nowCount < 16; ++i) {
        void* p = panels[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        const InGameSubScreenSpec* spec = FindSpec(k);
        if (!spec) continue;
        nowVisible[nowCount++] = p;
        if (IsSubScreenTracked(p)) continue;
        char text[128];
        bool spoke = false;
        if (spec->strref != 0xFFFFFFFFu &&
            LookupTlk(spec->strref, text, sizeof(text))) {
            acclog::Write("Menus.SubScreen", "panel=%p kind=%s strref=%u text=\"%s\"",
                          p, PanelKindName(k), spec->strref, text);
            tolk::Speak(text, /*interrupt=*/false);
            spoke = true;
        }
        if (!spoke) {
            acclog::Write("Menus.SubScreen", "panel=%p kind=%s text=\"%s\" (literal)",
                          p, PanelKindName(k), spec->literal);
            tolk::Speak(spec->literal, /*interrupt=*/false);
        }

        if (k == PanelKind::InGameCharacter) {
            acc::menus::charsheet::MaybeAnnounce(p);
        }
    }
    memcpy(s_visibleSubScreens, nowVisible, sizeof(nowVisible));
    s_visibleSubScreenCount = nowCount;
}

// ============================================================================
// Content fingerprint monitor.
// ============================================================================

struct ContentSnapshot {
    void* panel;
    char  text[512];
};
constexpr int kMaxContentSnapshots = 8;
ContentSnapshot s_contentSnapshots[kMaxContentSnapshots];
int s_contentSnapshotCount = 0;

bool IsContentMonitored(PanelKind k) {
    switch (k) {
    case PanelKind::TutorialBox:
    case PanelKind::DialogCinematic:
    case PanelKind::DialogCinematicCopy:
    case PanelKind::DialogComputer:
    case PanelKind::DialogComputerCamera:
    case PanelKind::BarkBubble:
    case PanelKind::MessageBoxModal:
    case PanelKind::AreaTransition:
    case PanelKind::InGameInventory:
    case PanelKind::InGameMap:
    case PanelKind::InGameJournal:
    case PanelKind::InGameCharacter:
    case PanelKind::InGameAbilities:
    case PanelKind::InGameMessages:
    case PanelKind::InGameEquip:
    case PanelKind::Container:
        return true;
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

        char text[256];
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
            char seg[256];
            size_t cp = segLen < sizeof(seg) - 1 ? segLen : sizeof(seg) - 1;
            memcpy(seg, p, cp);
            seg[cp] = '\0';
            tolk::Speak(seg, /*interrupt=*/false);
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

        char fingerprint[512];
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
        tolk::Speak(text, /*interrupt=*/false);
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
    MonitorFocusedControl();
    MonitorPanelContents();
    MonitorDialogReplies();
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
