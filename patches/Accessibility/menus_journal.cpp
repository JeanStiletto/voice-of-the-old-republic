#include "menus_journal.h"

#include "engine_offsets.h"
#include "engine_reads.h"
#include "log.h"
#include "prism.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <windows.h>

namespace acc::menus::journal {

namespace {

// CSWGuiInGameJournal::OnControlEntered @ 0x00645100. Outer gate is
// `if (param_1 != NULL)` only (decompiled — see peek_description.cpp's
// RefreshJournal for the same call). Idempotent w.r.t. screen state: just
// clears+repopulates item_description_label for the passed-in row.
constexpr std::uintptr_t kAddrJournalOnControlEntered = 0x00645100;

// CSWGuiInGameJournal.item_description_label @ +0x1a4 (a CSWGuiListBox with
// exactly one row whose label holds the planet-prefixed entry text).
constexpr std::size_t    kJournalDescriptionListBoxOffset = 0x1a4;

typedef void (__thiscall* PFN_PanelOnControl)(void* panel, void* control);
typedef void (__thiscall* PFN_PanelThiscall)(void* panel);

// Embedded button objects live AT panel+offset (the ctor constructs them in
// place via &this->sort_button etc.), so the chain captures &button == that
// address. Identifying by struct offset is locale-independent and was verified
// live (sort = panel+0xc2c, swap = panel+0xa68 in patch-20260603-090028.log).
bool IsButtonAtOffset(void* panel, void* control, std::size_t offset) {
    if (!panel || !control) return false;
    return reinterpret_cast<unsigned char*>(panel) + offset ==
           reinterpret_cast<unsigned char*>(control);
}

bool ReadRowText(void* row, char* outBuf, std::size_t bufSize) {
    if (!row || !outBuf || bufSize < 2) return false;
    // gui_string is the rendered text; SetText updates it synchronously
    // before OnControlEntered returns, so the read here sees the entry's
    // text rather than a stale paint. Fall back to inline CExoString /
    // strref / text_object only when gui_string is empty (extra defense
    // in case a future engine path writes the label text without going
    // through the CAurGUIStringInternal).
    if (acc::engine::ReadGuiString(row, kLabelGuiStringPtrOffset,
                                   outBuf, bufSize)) {
        return outBuf[0] != '\0';
    }
    if (acc::engine::ExtractTextOrStrRefIndirect(
            row, kLabelTextOffset, kLabelStrRefOffset,
            kLabelTextObjectOffset, outBuf, bufSize)) {
        return outBuf[0] != '\0';
    }
    return false;
}

}  // namespace

bool IsJournalEntry(void* control) {
    if (!control) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<std::uintptr_t>(vt) == kVtableCSWGuiJournalItemEntry;
}

void SpeakDescription(void* panel, void* focusedRow) {
    if (!panel || !focusedRow) return;

    __try {
        auto fn = reinterpret_cast<PFN_PanelOnControl>(
            kAddrJournalOnControlEntered);
        fn(panel, focusedRow);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Journal",
                      "OnControlEntered SEH (panel=%p row=%p); reading "
                      "whatever the description listbox already held",
                      panel, focusedRow);
    }

    void* lb = reinterpret_cast<unsigned char*>(panel) +
               kJournalDescriptionListBoxOffset;
    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);

    int rowCount = 0;
    void* row = nullptr;
    __try {
        if (lbList && lbList->data) {
            rowCount = lbList->size;
            if (rowCount > 0) row = lbList->data[0];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Journal",
                      "description listbox read SEH (panel=%p)", panel);
        return;
    }

    if (!row) {
        acclog::Write("Menus.Journal",
                      "no description row (panel=%p focused=%p rowCount=%d)",
                      panel, focusedRow, rowCount);
        return;
    }

    char text[2048];
    text[0] = '\0';
    bool gotText = false;
    __try {
        gotText = ReadRowText(row, text, sizeof(text));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Journal",
                      "row-text read SEH (panel=%p row=%p)", panel, row);
        return;
    }

    if (!gotText) {
        acclog::Write("Menus.Journal",
                      "description row had no readable text (panel=%p row=%p)",
                      panel, row);
        return;
    }

    prism::Speak(text, /*interrupt=*/true);

    // Single-line log: collapse embedded newlines so the German
    // "<Planet>:\n<body>" form fits on one line for grep.
    char logbuf[2048];
    std::size_t n = strnlen(text, sizeof(text) - 1);
    if (n >= sizeof(logbuf)) n = sizeof(logbuf) - 1;
    for (std::size_t i = 0; i < n; ++i) {
        char c = text[i];
        logbuf[i] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    logbuf[n] = '\0';
    acclog::Write("Menus.Journal",
                  "Enter on quest row=%p (first 400 chars: \"%.400s\")",
                  focusedRow, logbuf);
}

bool IsSortButton(void* panel, void* control) {
    return IsButtonAtOffset(panel, control, kJournalSortButtonOffset);
}

bool IsSwapButton(void* panel, void* control) {
    return IsButtonAtOffset(panel, control, kJournalSwapTextButtonOffset);
}

void LogEntryCounts(void* panel) {
    // Cross-check: read the raw CSWCJournal counts (active + done) directly,
    // independent of the current display mode, and the live items_listbox row
    // count. If active/done counts match the rows the user navigates, we are
    // displaying every entry the engine has. Diagnostic only.
    //
    //   AppManager (0x007A39FC) → client (+0x4)
    //   CClientExoApp::GetQuestJournal @0x005ed320 → CSWCJournal*
    //     +0x04 num_done_entries
    //     +0x24 journal_entries.journal_entries.size  (active count)
    //   CClientExoApp::GetInGameGui   @0x005ed690 → CGuiInGame*
    //     +0xbc4 bit0 = done-view mode
    typedef void* (__thiscall* PFN_ClientGetter)(void* client);
    __try {
        void* appMgr = *reinterpret_cast<void**>(0x007A39FC);
        if (!appMgr) return;
        void* client = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appMgr) + 0x4);
        if (!client) return;

        auto getJournal = reinterpret_cast<PFN_ClientGetter>(0x005ed320);
        auto getGui     = reinterpret_cast<PFN_ClientGetter>(0x005ed690);
        void* journal = getJournal(client);
        void* gui     = getGui(client);
        if (!journal) return;

        int doneCount   = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(journal) + 0x4);
        int activeCount = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(journal) + 0x24);
        int mode = 0;
        if (gui) {
            mode = *reinterpret_cast<unsigned int*>(
                reinterpret_cast<unsigned char*>(gui) + 0xbc4) & 1;
        }

        int listRows = -1;
        if (panel) {
            void* lb = reinterpret_cast<unsigned char*>(panel) + 0x5c4;
            auto* lbList = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
            if (lbList) listRows = lbList->size;
        }

        acclog::Write("Menus.Journal",
                      "EntryCounts: active=%d done=%d (engine data) | "
                      "current view=%s listbox rows=%d",
                      activeCount, doneCount,
                      mode ? "DONE" : "ACTIVE", listRows);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Journal", "EntryCounts SEH (panel=%p)", panel);
    }
}

void ForceRepopulate(void* panel) {
    if (!panel) return;
    __try {
        auto fn = reinterpret_cast<PFN_PanelThiscall>(
            kAddrJournalPopulateItemListBox);
        fn(panel);
        acclog::Write("Menus.Journal",
                      "ForceRepopulate: PopulateItemListBox(panel=%p)", panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Journal",
                      "ForceRepopulate SEH (panel=%p)", panel);
    }
}

}  // namespace acc::menus::journal
