// chargen "Talente" main panel input handler.
// See menus_chargen_feats.h for the design rationale.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "menus_chargen_feats.h"

#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_internal.h"   // FindControlById, QueueButtonByIdActivate
#include "menus_pending.h"
#include "strings.h"
#include "prism.h"

using namespace acc::engine;  // kInput*, ReadGuiString, ExtractTextOrStrRefIndirect

using acc::menus::detail::FindControlById;
using acc::menus::detail::QueueButtonByIdActivate;

namespace acc::menus::chargen_feats {

namespace {

// .gui-time button IDs from ftchrgen.gui (stable across localisations —
// they're baked into the resource at build time).
constexpr int kBtnRecommendedId = 9;
constexpr int kBtnAcceptId      = 11;  // "OK"
constexpr int kBtnBackId        = 12;  // "Abbrechen"

// 2D cursor model — Up/Down moves between rows (feat chains), Left/Right
// moves between cells within a chain (root → successor → master). Empty
// cells (featId == 0xffffffff) are skipped on horizontal moves; the
// vertical move tries to keep the same column, falling back to the
// nearest filled column in the destination row.
//
// The 3 buttons (Empfohlen / OK / Abbrechen) are appended as virtual
// rows after the chart's last row — past the bottom of the chart, Down
// enters the button area where each button is its own one-cell row.

struct ChartRow {
    void*           row;            // CSWGuiSkillFlow*
    int             rowIdx;         // index within the chart
    unsigned short  featId[kSkillFlowColumnsPerRow];  // 0xffff = empty
};

struct ButtonRow {
    int             buttonId;
    const char*     logTag;
};

constexpr ButtonRow kButtonRows[] = {
    { kBtnRecommendedId, "BTN_RECOMMENDED" },
    { kBtnAcceptId,      "BTN_ACCEPT" },
    { kBtnBackId,        "BTN_BACK" },
};
constexpr int kButtonRowCount = sizeof(kButtonRows) / sizeof(kButtonRows[0]);

constexpr int kMaxChartRows = 256;
ChartRow s_chartRows[kMaxChartRows];
int      s_chartRowCount = 0;

// Cursor: (row, col). row covers chart rows then button rows; col is
// 0..2 for chart rows, always 0 for button rows.
int   s_curRow      = 0;
int   s_curCol      = 0;

// Binding signature — used by EnsureBound to detect when the engine has
// re-bound this panel to a different character (e.g. the next slot in
// the auto-level-up queue reuses the same panel pointer with a fresh
// chart rows array). The rows array pointer + size change in that case
// even when the panel pointer stays the same.
void* s_boundPanel     = nullptr;
void* s_boundRowsPtr   = nullptr;
int   s_boundRowsCount = 0;

inline int  TotalRowCount()         { return s_chartRowCount + kButtonRowCount; }
inline bool IsButtonRow(int r)      { return r >= s_chartRowCount; }
inline bool ColFilled(int r, int c) {
    if (r < 0 || r >= s_chartRowCount) return false;
    if (c < 0 || c >= kSkillFlowColumnsPerRow) return false;
    return s_chartRows[r].featId[c] != 0xffff;
}

// First filled column in chart row `r`, or -1 if none.
int FirstFilledCol(int r) {
    for (int c = 0; c < kSkillFlowColumnsPerRow; ++c) {
        if (ColFilled(r, c)) return c;
    }
    return -1;
}

// Pick the column closest to `want` in chart row `r` among filled cols.
// Used by vertical nav to keep the cursor's column when entering a new
// chart row, falling back to the nearest filled cell when the new row
// doesn't have that column.
int NearestFilledCol(int r, int want) {
    if (ColFilled(r, want)) return want;
    for (int d = 1; d < kSkillFlowColumnsPerRow; ++d) {
        if (ColFilled(r, want - d)) return want - d;
        if (ColFilled(r, want + d)) return want + d;
    }
    return FirstFilledCol(r);
}

// Read the engine's current chart binding (rows array data pointer +
// size) for `panel`. Returns false on read failure with outputs cleared.
bool ReadChartBinding(void* panel, void*& outRows, int& outCount) {
    outRows  = nullptr;
    outCount = 0;
    if (!panel) return false;
    auto* chart = reinterpret_cast<unsigned char*>(panel) +
                  kFeatsCharGenChartOffset;
    __try {
        outRows  = *reinterpret_cast<void**>(
            chart + kSkillFlowChartRowsDataOffset);
        outCount = *reinterpret_cast<int*>(
            chart + kSkillFlowChartRowsSizeOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outRows  = nullptr;
        outCount = 0;
        return false;
    }
    return true;
}

// Walk the engine's chart rows array into s_chartRows[]. Idempotent and
// cheap — does NOT touch the cursor. EnsureBound calls this on every
// hit; cursor placement is decided there based on whether the binding
// is new.
void WalkChartRows(void* rows, int nRows) {
    s_chartRowCount = 0;
    if (!rows || nRows < 0 || nRows > kMaxChartRows) nRows = 0;

    auto** rowsArr = reinterpret_cast<void**>(rows);
    for (int r = 0; r < nRows; ++r) {
        void* rowPtr = nullptr;
        __try { rowPtr = rowsArr[r]; }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!rowPtr) continue;

        ChartRow& cr = s_chartRows[s_chartRowCount];
        cr.row    = rowPtr;
        cr.rowIdx = r;
        bool any  = false;
        auto* rb  = reinterpret_cast<unsigned char*>(rowPtr);
        for (int c = 0; c < kSkillFlowColumnsPerRow; ++c) {
            unsigned int v = kFlowSkillStructEmptyFeatId;
            __try {
                auto* cell = rb + kSkillFlowFirstColumnOffset +
                             c * kSkillFlowColumnStride;
                v = *reinterpret_cast<unsigned int*>(
                    cell + kFlowSkillStructFeatIdOffset);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                v = kFlowSkillStructEmptyFeatId;
            }
            if (v == kFlowSkillStructEmptyFeatId) {
                cr.featId[c] = 0xffff;
            } else {
                cr.featId[c] = (unsigned short)v;
                any = true;
            }
        }
        if (!any) continue;  // skip empty rows entirely
        s_chartRowCount++;
        if (s_chartRowCount >= kMaxChartRows) break;
    }
}

// Re-walk the engine's chart on every hit so we never index stale row
// pointers (e.g. when the auto-level-up flow reuses the same panel
// pointer for the next character and the engine swaps in a different
// rows array). Cursor reset only fires on a real binding change —
// otherwise the user's nav position is preserved (clamped on shrink).
void EnsureBound(void* panel) {
    void* rows  = nullptr;
    int   nRows = 0;
    ReadChartBinding(panel, rows, nRows);

    bool newBinding = (panel != s_boundPanel) ||
                      (rows  != s_boundRowsPtr) ||
                      (nRows != s_boundRowsCount);

    WalkChartRows(rows, nRows);

    s_boundPanel     = panel;
    s_boundRowsPtr   = rows;
    s_boundRowsCount = nRows;

    if (newBinding) {
        // Initial cursor: first filled cell of first chart row, falling
        // back to the button area if the chart is empty.
        if (s_chartRowCount > 0) {
            s_curRow = 0;
            s_curCol = FirstFilledCol(0);
            if (s_curCol < 0) s_curCol = 0;
        } else {
            s_curRow = 0;  // first button
            s_curCol = 0;
        }
        acclog::Write("ChargenFeats",
                      "rebuild panel=%p chartRows=%d totalRows=%d "
                      "cursor=(r=%d, c=%d) [new binding]",
                      panel, s_chartRowCount, TotalRowCount(),
                      s_curRow, s_curCol);
    } else {
        // Same binding: keep the cursor, but clamp if the row count
        // shrunk underneath us.
        int total = TotalRowCount();
        if (s_curRow >= total) s_curRow = total > 0 ? total - 1 : 0;
        if (s_curRow < 0)      s_curRow = 0;
    }
}

// Lowest byte of the cell's 0x120 dword — the chart-render status enum
// (0 avail / 1 existing / 2 granted / 3 locked / 4 chosen).
unsigned char ReadCellStatus(int r, int c) {
    if (r < 0 || r >= s_chartRowCount) return 0xff;
    if (c < 0 || c >= kSkillFlowColumnsPerRow) return 0xff;
    auto* rb = reinterpret_cast<unsigned char*>(s_chartRows[r].row);
    unsigned int v = 0;
    __try {
        auto* cell = rb + kSkillFlowFirstColumnOffset +
                     (size_t)c * kSkillFlowColumnStride;
        v = *reinterpret_cast<unsigned int*>(
            cell + kFlowSkillStructStatusOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xff;
    }
    return (unsigned char)(v & 0xff);
}

const char* StatusWord(unsigned char status) {
    using acc::strings::Get;
    using acc::strings::Id;
    switch (status) {
        case 0: return Get(Id::ChargenFeatStatusAvailable);
        case 1: return Get(Id::ChargenFeatStatusExisting);
        case 2: return Get(Id::ChargenFeatStatusGranted);
        case 3: return Get(Id::ChargenFeatStatusLocked);
        case 4: return Get(Id::ChargenFeatStatusChosen);
        default: return "";
    }
}

bool ReadNameLabel(void* panel, char* out, size_t outN) {
    auto* lab = reinterpret_cast<unsigned char*>(panel) +
                kFeatsCharGenNameLabelOffset;
    return ReadLabelText(lab, out, outN);
}

// Read panel.description_listbox.controls[0] rendered text. Engine
// populates it via SetDescription called from OnEnterFeat.
bool ReadDescription(void* panel, char* out, size_t outN) {
    if (!panel || !out || outN == 0) return false;
    out[0] = '\0';
    auto* base = reinterpret_cast<unsigned char*>(panel);
    void* descLb = base + kFeatsCharGenDescriptionListBoxOffset;
    auto* descList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(descLb) + kListBoxControlsOffset);
    void* row = nullptr;
    __try {
        if (descList && descList->data && descList->size > 0) {
            row = descList->data[0];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        row = nullptr;
    }
    if (!row) return false;
    return ReadLabelText(row, out, outN);
}

// SetSelectedSkill on the chart (visual highlight + chart's selected
// (col,row) state) + OnEnterFeat on the panel (refreshes name_label,
// description_listbox, BTN_SELECT label/colour for add/remove).
void DriveEngineSelection(void* panel, unsigned short featId) {
    auto* base  = reinterpret_cast<unsigned char*>(panel);
    void* chart = base + kFeatsCharGenChartOffset;

    typedef void (__thiscall* PFN_SetSelected)(void*, unsigned long);
    typedef void (__thiscall* PFN_OnEnter)(void*, unsigned short);

    __try {
        auto setSel = reinterpret_cast<PFN_SetSelected>(
            kAddrCSWGuiSkillFlowChartSetSelectedSkill);
        setSel(chart, featId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ChargenFeats",
                      "SetSelectedSkill faulted featId=%u",
                      (unsigned)featId);
    }
    __try {
        auto onEnter = reinterpret_cast<PFN_OnEnter>(
            kAddrCSWGuiFeatsCharGenOnEnterFeat);
        onEnter(panel, featId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ChargenFeats",
                      "OnEnterFeat faulted featId=%u",
                      (unsigned)featId);
    }
}

void AnnounceFocused(void* panel) {
    if (s_curRow < 0 || s_curRow >= TotalRowCount()) return;

    if (IsButtonRow(s_curRow)) {
        const ButtonRow& br = kButtonRows[s_curRow - s_chartRowCount];
        void* btn = FindControlById(panel, br.buttonId);
        char btnText[64];
        bool got = btn && ReadButtonText(btn, btnText, sizeof(btnText)) &&
                   btnText[0] != '\0';
        if (!got) {
            snprintf(btnText, sizeof(btnText), "%s",
                     br.logTag ? br.logTag : "?");
        }
        prism::Speak(btnText, /*interrupt=*/false);
        acclog::Write("ChargenFeats",
                      "focus button id=%d text=\"%s\"",
                      br.buttonId, btnText);
        return;
    }

    if (s_curCol < 0 || s_curCol >= kSkillFlowColumnsPerRow) return;
    if (!ColFilled(s_curRow, s_curCol)) return;

    unsigned short featId = s_chartRows[s_curRow].featId[s_curCol];
    DriveEngineSelection(panel, featId);

    char name[128];
    if (!ReadNameLabel(panel, name, sizeof(name)) || name[0] == '\0') {
        snprintf(name, sizeof(name), "Talent %u", (unsigned)featId);
    }
    unsigned char st = ReadCellStatus(s_curRow, s_curCol);
    const char* sw   = StatusWord(st);

    char head[256];
    snprintf(head, sizeof(head),
             acc::strings::Get(
                 acc::strings::Id::FmtChargenFeatChartCell),
             name, sw);
    prism::Speak(head, /*interrupt=*/false);

    char desc[1024];
    if (ReadDescription(panel, desc, sizeof(desc)) && desc[0] != '\0') {
        prism::Speak(desc, /*interrupt=*/false);
    }

    acclog::Write("ChargenFeats",
                  "focus row=%d col=%d featId=%u status=%u name=\"%s\"",
                  s_chartRows[s_curRow].rowIdx, s_curCol,
                  (unsigned)featId, (unsigned)st, name);
}

void NavVertical(void* panel, bool down) {
    int oldR = s_curRow;
    int oldC = s_curCol;
    int total = TotalRowCount();
    int r = s_curRow + (down ? 1 : -1);
    if (r < 0)        r = 0;
    if (r >= total)   r = total - 1;
    s_curRow = r;
    if (IsButtonRow(r)) {
        s_curCol = 0;
    } else {
        int c = NearestFilledCol(r, oldC);
        if (c < 0) c = 0;
        s_curCol = c;
    }
    AnnounceFocused(panel);
    acclog::Write("ChargenFeats",
                  "%s sel=(r=%d,c=%d) -> (r=%d,c=%d) [chartRows=%d, "
                  "totalRows=%d]",
                  down ? "Down" : "Up",
                  oldR, oldC, s_curRow, s_curCol,
                  s_chartRowCount, total);
}

void NavHorizontal(void* panel, bool right) {
    if (IsButtonRow(s_curRow)) {
        // Buttons are single-cell — re-announce on Left/Right press
        // for boundary feedback rather than going silent.
        AnnounceFocused(panel);
        return;
    }
    int oldC = s_curCol;
    int c = oldC;
    int step = right ? 1 : -1;
    while (true) {
        c += step;
        if (c < 0 || c >= kSkillFlowColumnsPerRow) {
            c = oldC;  // clamp
            break;
        }
        if (ColFilled(s_curRow, c)) break;
    }
    s_curCol = c;
    AnnounceFocused(panel);
    acclog::Write("ChargenFeats",
                  "%s sel=(r=%d,c=%d) -> (r=%d,c=%d)",
                  right ? "Right" : "Left",
                  s_curRow, oldC, s_curRow, s_curCol);
}

}  // namespace

bool IsChargenFeatsPanel(void* panel) {
    if (!panel) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiFeatsCharGen;
}

bool HandleInput(int n, void* thisPtr, void* panel,
                 int param_1, int param_2, int& outRv)
{
    if (!IsChargenFeatsPanel(panel)) return false;
    EnsureBound(panel);
    if (s_chartRowCount + kButtonRowCount == 0) return false;

    // Press edges only.
    if (param_2 == 0) return false;

    if (param_1 == kInputNavUp || param_1 == kInputNavDown) {
        NavVertical(panel, /*down=*/param_1 == kInputNavDown);
        outRv = 1;
        return true;
    }

    if (param_1 == kInputNavLeft || param_1 == kInputNavRight) {
        NavHorizontal(panel, /*right=*/param_1 == kInputNavRight);
        outRv = 1;
        return true;
    }

    if (param_1 == kInputEnter1 || param_1 == kInputEnter2) {
        if (IsButtonRow(s_curRow)) {
            const ButtonRow& br = kButtonRows[s_curRow - s_chartRowCount];
            QueueButtonByIdActivate(panel, br.buttonId,
                                    "ChargenFeats: Enter -> button");
        } else if (s_curRow >= 0 && s_curRow < s_chartRowCount &&
                   s_curCol >= 0 && s_curCol < kSkillFlowColumnsPerRow &&
                   ColFilled(s_curRow, s_curCol)) {
            unsigned short featId = s_chartRows[s_curRow].featId[s_curCol];
            if (acc::menus::pending::IsPending()) {
                acclog::Write("ChargenFeats",
                              "Enter -- op already pending; ignoring");
            } else {
                typedef void (__thiscall* PFN_OnFeatPicked)(
                    void*, unsigned long);
                __try {
                    auto fn = reinterpret_cast<PFN_OnFeatPicked>(
                        kAddrCSWGuiFeatsCharGenOnFeatPicked);
                    fn(panel, featId);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    acclog::Write("ChargenFeats",
                                  "OnFeatPicked faulted featId=%u",
                                  (unsigned)featId);
                }
                acclog::Write("ChargenFeats",
                              "OnFeatPicked panel=%p featId=%u",
                              panel, (unsigned)featId);
                // Re-announce: status flips on add/remove. For status
                // 2/3/4 OnFeatPicked opens a "can't change this" message
                // box (a separate panel that AnnouncePanelTitle handles
                // independently); the chart re-announce is harmless in
                // that case — the message-box title speech runs on top.
                AnnounceFocused(panel);
            }
        }
        outRv = 1;
        return true;
    }

    if (param_1 == kInputEsc1 || param_1 == kInputEsc2) {
        QueueButtonByIdActivate(
            panel, kBtnBackId, "ChargenFeats: Esc -> BTN_BACK");
        outRv = 1;
        return true;
    }

    return false;
}

}  // namespace acc::menus::chargen_feats
