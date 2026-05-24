#include "dialog_speech.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_manager.h"   // kAddrGuiManagerPtr, kMgrPanels*
#include "engine_offsets.h"
#include "engine_panels.h"    // PanelKind, IdentifyPanel
#include "engine_reads.h"
#include "log.h"
#include "menus_extract.h"    // FromControl
#include "strings.h"
#include "prism.h"

namespace acc::dialog_speech {

namespace {

// Find the foreground active dialog panel — first match for any of the
// dialog kinds. Returns the panel pointer + which kind matched.
struct DialogPanelMatch {
    void*                 panel;
    acc::engine::PanelKind kind;
};

DialogPanelMatch FindActiveDialogPanel() {
    DialogPanelMatch out{nullptr, acc::engine::PanelKind::Unknown};
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return out;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return out;
    int n = panelCount > 16 ? 16 : panelCount;
    for (int i = 0; i < n; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        auto k = acc::engine::IdentifyPanel(p);
        switch (k) {
        case acc::engine::PanelKind::DialogCinematic:
        case acc::engine::PanelKind::DialogCinematicCopy:
        case acc::engine::PanelKind::DialogComputer:
        case acc::engine::PanelKind::DialogComputerCamera:
            out.panel = p;
            out.kind  = k;
            return out;
        default:
            break;
        }
    }
    return out;
}

// Find a BarkBubble panel (independent lifecycle from main dialog).
void* FindBarkBubblePanel() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return nullptr;
    int n = panelCount > 16 ? 16 : panelCount;
    for (int i = 0; i < n; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        if (acc::engine::IdentifyPanel(p) ==
            acc::engine::PanelKind::BarkBubble) {
            return p;
        }
    }
    return nullptr;
}

// Read a CSWGuiLabel's rendered text via the engine's gui_string path
// (with the inline CExoString / strref fallback). Returns "" on miss.
bool ReadLabelText(void* panel, size_t labelOffset,
                   char* outBuf, size_t bufSize) {
    if (!panel || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    auto* label = reinterpret_cast<unsigned char*>(panel) + labelOffset;
    __try {
        if (acc::engine::ReadGuiString(label, kLabelGuiStringPtrOffset,
                                       outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
        if (acc::engine::ExtractTextOrStrRefIndirect(
                label, kLabelTextOffset, kLabelStrRefOffset,
                kLabelTextObjectOffset, outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
    return false;
}

// Read row count of a CSWGuiListBox at panel + offset.
int ReadListBoxRowCount(void* panel, size_t lbOffset) {
    if (!panel) return 0;
    __try {
        void* lb = reinterpret_cast<unsigned char*>(panel) + lbOffset;
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(lb) +
            kListBoxControlsOffset);
        if (!lbList || !lbList->data) return 0;
        return lbList->size;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Walk a panel's controls[] for the first child whose text resolves
// non-empty. Used as a fallback for the BarkBubble — its label offset
// isn't documented in the plan, but the panel renders a single visible
// text label which reads via FromControl.
bool ReadFirstVisibleText(void* panel, char* outBuf, size_t bufSize) {
    if (!panel || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    __try {
        auto* base = reinterpret_cast<unsigned char*>(panel);
        auto* controls = reinterpret_cast<CExoArrayList*>(
            base + kPanelControlsOffset);
        if (!controls || !controls->data) return false;
        int sz = controls->size > 16 ? 16 : controls->size;
        for (int i = 0; i < sz; ++i) {
            void* child = controls->data[i];
            if (!child) continue;
            if (acc::menus::extract::FromControl(child, outBuf, bufSize) &&
                outBuf[0] != '\0') {
                return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
    return false;
}

}  // namespace

void Tick() {
    // ---- NPC line + replies count for the main dialog panel. ----
    DialogPanelMatch m = FindActiveDialogPanel();
    static void*       s_lastPanel    = nullptr;
    static char        s_lastNpcLine[512] = {0};
    static int         s_lastReplyCount   = 0;

    if (!m.panel) {
        if (s_lastPanel) {
            acclog::Write("Dialog.Speech", "panel closed");
            s_lastPanel = nullptr;
            s_lastNpcLine[0] = '\0';
            s_lastReplyCount = 0;
        }
    } else {
        // First sight — set baseline silently. Don't replay history on
        // open; the very first NPC line will trigger via the diff path
        // on the next tick when the engine populates the message label.
        if (s_lastPanel != m.panel) {
            s_lastPanel = m.panel;
            s_lastNpcLine[0] = '\0';
            s_lastReplyCount = 0;
        }

        char npc[512] = "";
        bool gotNpc = ReadLabelText(m.panel, kDialogMessageLabelOffset,
                                    npc, sizeof(npc));
        if (gotNpc && std::strcmp(npc, s_lastNpcLine) != 0) {
            prism::Speak(npc, /*interrupt=*/true);
            acclog::Write("Dialog.Speech",
                          "NPC line panel=%p kind=%s -> [%.300s]",
                          m.panel, acc::engine::PanelKindName(m.kind), npc);
            std::strncpy(s_lastNpcLine, npc, sizeof(s_lastNpcLine) - 1);
            s_lastNpcLine[sizeof(s_lastNpcLine) - 1] = '\0';
        }

        int replies = ReadListBoxRowCount(m.panel,
                                          kDialogRepliesListBoxOffset);
        if (replies != s_lastReplyCount) {
            int prev = s_lastReplyCount;
            s_lastReplyCount = replies;
            if (replies > 0) {
                char msg[64];
                std::snprintf(msg, sizeof(msg),
                              acc::strings::Get(
                                  acc::strings::Id::FmtDialogReplies),
                              replies);
                prism::Speak(msg, /*interrupt=*/false);
                acclog::Write("Dialog.Speech",
                              "replies %d -> %d panel=%p [%s]",
                              prev, replies, m.panel, msg);
            }
        }

        // For Computer dialog variant, also speak the message_listbox
        // (terminal output) when its row count grows.
        if (m.kind == acc::engine::PanelKind::DialogComputer ||
            m.kind == acc::engine::PanelKind::DialogComputerCamera) {
            static int s_lastComputerRows = 0;
            int rows = ReadListBoxRowCount(m.panel,
                                           kDialogComputerMessageListBoxOffset);
            if (rows > s_lastComputerRows) {
                // Speak each newly appended terminal line.
                __try {
                    void* lb = reinterpret_cast<unsigned char*>(m.panel) +
                               kDialogComputerMessageListBoxOffset;
                    auto* lbList = reinterpret_cast<CExoArrayList*>(
                        reinterpret_cast<unsigned char*>(lb) +
                        kListBoxControlsOffset);
                    if (lbList && lbList->data) {
                        int cap = rows > lbList->size ? lbList->size : rows;
                        for (int i = s_lastComputerRows;
                             i < cap; ++i) {
                            void* row = lbList->data[i];
                            if (!row) continue;
                            char text[512];
                            if (acc::menus::extract::FromControl(
                                    row, text, sizeof(text)) &&
                                text[0] != '\0') {
                                prism::Speak(text, /*interrupt=*/false);
                                acclog::Write("Dialog.Speech",
                                              "computer row %d [%.200s]",
                                              i, text);
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    // ignore — try again next tick
                }
            }
            if (rows < s_lastComputerRows) {
                // Reset on shrink (panel cleared).
                acclog::Write("Dialog.Speech",
                              "computer rows reset %d -> %d",
                              s_lastComputerRows, rows);
            }
            s_lastComputerRows = rows;
        }
    }

    // ---- BarkBubble — independent lifecycle. ----
    void* bark = FindBarkBubblePanel();
    static void* s_lastBark = nullptr;
    static char  s_lastBarkText[512] = {0};
    if (!bark) {
        if (s_lastBark) {
            acclog::Write("Dialog.Speech", "bark closed");
            s_lastBark = nullptr;
            s_lastBarkText[0] = '\0';
        }
    } else {
        char text[512] = "";
        ReadFirstVisibleText(bark, text, sizeof(text));
        if (text[0] != '\0' && std::strcmp(text, s_lastBarkText) != 0) {
            prism::Speak(text, /*interrupt=*/false);
            acclog::Write("Dialog.Speech",
                          "bark panel=%p -> [%.300s]", bark, text);
            std::strncpy(s_lastBarkText, text, sizeof(s_lastBarkText) - 1);
            s_lastBarkText[sizeof(s_lastBarkText) - 1] = '\0';
        }
        s_lastBark = bark;
    }
}

}  // namespace acc::dialog_speech
