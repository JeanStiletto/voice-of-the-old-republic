// KOTOR Accessibility — Force-power picker input handler.
// See menus_powers_levelup.h for the design rationale.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "menus_powers_levelup.h"

#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_internal.h"   // FindControlById, QueueButtonByIdActivate
#include "menus_pending.h"
#include "strings.h"
#include "prism.h"

using namespace acc::engine;  // kInput*, IdentifyPanel, PanelKind, ReadGuiString, ExtractTextOrStrRefIndirect

using acc::menus::detail::FindControlById;
using acc::menus::detail::QueueButtonByIdActivate;

namespace acc::menus::powers_levelup {

namespace {

// pwrlvlup.gui control IDs — baked into the resource at build time,
// stable across localisations.
constexpr int kIdSubTitleLabel    = 1;   // "Kr�fte" — substitutes for id 0 placeholder
constexpr int kIdPowersListbox    = 6;   // rows of CSWGuiSkillFlow
constexpr int kIdDescriptionLb    = 7;   // controls[0] = wrapped description label
constexpr int kIdPowerLabel       = 8;   // name of the focused power
constexpr int kBtnRecommendedId   = 9;   // "Empfohlen"
constexpr int kBtnAcceptId        = 11;  // "OK"
constexpr int kBtnBackId          = 12;  // "Abbrechen"

struct ChartRow {
    void*           row;            // CSWGuiSkillFlow*
    int             rowIdx;         // index within powers_listbox.controls
    unsigned short  powerId[kSkillFlowColumnsPerRow];  // 0xffff = empty
};

struct ButtonRow {
    int         buttonId;
    const char* logTag;
};

// Button virtual entries after the chart cells. BTN_SELECT (id 10,
// "Hinzuf. Macht") is intentionally omitted — Enter on a focused cell
// dispatches OnPowerPicked directly, which is the same engine action
// BTN_SELECT click would fire. Mirrors the chargen_feats button list
// (recommended / accept / back).
constexpr ButtonRow kButtonRows[] = {
    { kBtnRecommendedId, "BTN_RECOMMENDED" },
    { kBtnAcceptId,      "BTN_ACCEPT" },
    { kBtnBackId,        "BTN_BACK" },
};
constexpr int kButtonRowCount = sizeof(kButtonRows) / sizeof(kButtonRows[0]);

constexpr int kMaxChartRows = 64;  // observed 17 rows in patch-20260526-074742.log
ChartRow s_chartRows[kMaxChartRows];
int      s_chartRowCount = 0;

int   s_curRow      = 0;
int   s_curCol      = 0;
void* s_boundPanel  = nullptr;

inline int  TotalRowCount()         { return s_chartRowCount + kButtonRowCount; }
inline bool IsButtonRow(int r)      { return r >= s_chartRowCount; }
inline bool ColFilled(int r, int c) {
    if (r < 0 || r >= s_chartRowCount) return false;
    if (c < 0 || c >= kSkillFlowColumnsPerRow) return false;
    return s_chartRows[r].powerId[c] != 0xffff;
}

int FirstFilledCol(int r) {
    for (int c = 0; c < kSkillFlowColumnsPerRow; ++c) {
        if (ColFilled(r, c)) return c;
    }
    return -1;
}

int NearestFilledCol(int r, int want) {
    if (ColFilled(r, want)) return want;
    for (int d = 1; d < kSkillFlowColumnsPerRow; ++d) {
        if (ColFilled(r, want - d)) return want - d;
        if (ColFilled(r, want + d)) return want + d;
    }
    return FirstFilledCol(r);
}

// Iterate powers_listbox.controls — the engine's own row source per
// CSWGuiPowersLevelUp::OnPowerSelectionChanged (decompile @0x006f1940):
//   listbox.GetSelectedControl() returns a CSWGuiSkillFlow*.
// The chart at +0x19fc holds (selected_row, selected_col) state only;
// its rows_data is not populated in the level-up flow.
void RebuildLayout(void* panel) {
    s_chartRowCount = 0;
    s_curRow        = 0;
    s_curCol        = 0;
    s_boundPanel    = panel;

    void* lb = FindControlById(panel, kIdPowersListbox);
    if (!lb) {
        acclog::Write("PowersLevelUp",
                      "rebuild panel=%p — powers_listbox (id %d) not found",
                      panel, kIdPowersListbox);
        return;
    }

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    void** rows  = nullptr;
    int    nRows = 0;
    __try {
        rows  = list ? list->data : nullptr;
        nRows = list ? list->size : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!rows || nRows < 0 || nRows > kMaxChartRows) nRows = 0;

    for (int r = 0; r < nRows; ++r) {
        void* rowPtr = nullptr;
        __try { rowPtr = rows[r]; }
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
                cr.powerId[c] = 0xffff;
            } else {
                cr.powerId[c] = (unsigned short)v;
                any = true;
            }
        }
        if (!any) continue;
        s_chartRowCount++;
        if (s_chartRowCount >= kMaxChartRows) break;
    }

    if (s_chartRowCount > 0) {
        s_curRow = 0;
        s_curCol = FirstFilledCol(0);
        if (s_curCol < 0) s_curCol = 0;
    } else {
        s_curRow = 0;
        s_curCol = 0;
    }

    acclog::Write("PowersLevelUp",
                  "rebuild panel=%p chartRows=%d totalRows=%d "
                  "cursor=(r=%d, c=%d)",
                  panel, s_chartRowCount, TotalRowCount(),
                  s_curRow, s_curCol);
}

void EnsureBound(void* panel) {
    if (s_boundPanel != panel) RebuildLayout(panel);
}

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

bool ReadLabelText(void* lab, char* out, size_t outN) {
    if (!lab || !out || outN == 0) return false;
    out[0] = '\0';
    __try {
        if (ReadGuiString(lab, kLabelGuiStringPtrOffset,
                          out, outN) && out[0] != '\0') {
            return true;
        }
        if (ExtractTextOrStrRefIndirect(
                lab, kLabelTextOffset, kLabelStrRefOffset,
                kLabelTextObjectOffset, out, outN) && out[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
    }
    return false;
}

bool ReadButtonText(void* btn, char* out, size_t outN) {
    if (!btn || !out || outN == 0) return false;
    out[0] = '\0';
    __try {
        if (ReadGuiString(btn, kButtonGuiStringPtrOffset,
                          out, outN) && out[0] != '\0') {
            return true;
        }
        if (ExtractTextOrStrRefIndirect(
                btn, kButtonTextOffset, kButtonStrRefOffset,
                kButtonTextObjectOffset, out, outN) && out[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
    }
    return false;
}

bool ReadPowerName(void* panel, char* out, size_t outN) {
    void* lab = FindControlById(panel, kIdPowerLabel);
    return ReadLabelText(lab, out, outN);
}

bool ReadDescription(void* panel, char* out, size_t outN) {
    if (!panel || !out || outN == 0) return false;
    out[0] = '\0';
    void* descLb = FindControlById(panel, kIdDescriptionLb);
    if (!descLb) return false;
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

// SetSelectedSkill on the embedded chart + OnEnterPower on the panel.
// Mirrors chargen_feats::DriveEngineSelection. The chart lives at
// kPowersLevelUpChartOffset and the same CSWGuiSkillFlowChart class
// (so the same SetSelectedSkill address @0x006cdc00 applies).
void DriveEngineSelection(void* panel, unsigned short powerId) {
    auto* base  = reinterpret_cast<unsigned char*>(panel);
    void* chart = base + kPowersLevelUpChartOffset;

    typedef void (__thiscall* PFN_SetSelected)(void*, unsigned long);
    typedef void (__thiscall* PFN_OnEnter)(void*, unsigned long);

    __try {
        auto setSel = reinterpret_cast<PFN_SetSelected>(
            kAddrCSWGuiSkillFlowChartSetSelectedSkill);
        setSel(chart, powerId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("PowersLevelUp",
                      "SetSelectedSkill faulted powerId=%u",
                      (unsigned)powerId);
    }
    __try {
        auto onEnter = reinterpret_cast<PFN_OnEnter>(
            kAddrCSWGuiPowersLevelUpOnEnterPower);
        onEnter(panel, powerId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("PowersLevelUp",
                      "OnEnterPower faulted powerId=%u",
                      (unsigned)powerId);
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
        acclog::Write("PowersLevelUp",
                      "focus button id=%d text=\"%s\"",
                      br.buttonId, btnText);
        return;
    }

    if (s_curCol < 0 || s_curCol >= kSkillFlowColumnsPerRow) return;
    if (!ColFilled(s_curRow, s_curCol)) return;

    unsigned short powerId = s_chartRows[s_curRow].powerId[s_curCol];
    DriveEngineSelection(panel, powerId);

    char name[128];
    if (!ReadPowerName(panel, name, sizeof(name)) || name[0] == '\0') {
        snprintf(name, sizeof(name), "Macht %u", (unsigned)powerId);
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

    acclog::Write("PowersLevelUp",
                  "focus row=%d col=%d powerId=%u status=%u name=\"%s\"",
                  s_chartRows[s_curRow].rowIdx, s_curCol,
                  (unsigned)powerId, (unsigned)st, name);
}

void NavVertical(void* panel, bool down) {
    int oldR = s_curRow;
    int oldC = s_curCol;
    int total = TotalRowCount();
    int r = s_curRow + (down ? 1 : -1);
    if (r < 0)      r = 0;
    if (r >= total) r = total - 1;
    s_curRow = r;
    if (IsButtonRow(r)) {
        s_curCol = 0;
    } else {
        int c = NearestFilledCol(r, oldC);
        if (c < 0) c = 0;
        s_curCol = c;
    }
    AnnounceFocused(panel);
    acclog::Write("PowersLevelUp",
                  "%s sel=(r=%d,c=%d) -> (r=%d,c=%d) [chartRows=%d, "
                  "totalRows=%d]",
                  down ? "Down" : "Up",
                  oldR, oldC, s_curRow, s_curCol,
                  s_chartRowCount, total);
}

void NavHorizontal(void* panel, bool right) {
    if (IsButtonRow(s_curRow)) {
        AnnounceFocused(panel);
        return;
    }
    int oldC = s_curCol;
    int c = oldC;
    int step = right ? 1 : -1;
    while (true) {
        c += step;
        if (c < 0 || c >= kSkillFlowColumnsPerRow) {
            c = oldC;
            break;
        }
        if (ColFilled(s_curRow, c)) break;
    }
    s_curCol = c;
    AnnounceFocused(panel);
    acclog::Write("PowersLevelUp",
                  "%s sel=(r=%d,c=%d) -> (r=%d,c=%d)",
                  right ? "Right" : "Left",
                  s_curRow, oldC, s_curRow, s_curCol);
}

}  // namespace

bool IsPowersLevelUpPanel(void* panel) {
    return IdentifyPanel(panel) == PanelKind::PowersLevelUp;
}

const char* GetTitleOverride(void* panel) {
    if (!IsPowersLevelUpPanel(panel)) return nullptr;
    void* lab = FindControlById(panel, kIdSubTitleLabel);
    if (!lab) return nullptr;
    static thread_local char s_titleBuf[128];
    if (!ReadLabelText(lab, s_titleBuf, sizeof(s_titleBuf))) return nullptr;
    if (s_titleBuf[0] == '\0') return nullptr;
    return s_titleBuf;
}

bool HandleInput(int n, void* thisPtr, void* panel,
                 int param_1, int param_2, int& outRv)
{
    (void)n; (void)thisPtr;
    if (!IsPowersLevelUpPanel(panel)) return false;
    EnsureBound(panel);
    if (s_chartRowCount + kButtonRowCount == 0) return false;

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
                                    "PowersLevelUp: Enter -> button");
        } else if (s_curRow >= 0 && s_curRow < s_chartRowCount &&
                   s_curCol >= 0 && s_curCol < kSkillFlowColumnsPerRow &&
                   ColFilled(s_curRow, s_curCol)) {
            unsigned short powerId = s_chartRows[s_curRow].powerId[s_curCol];
            if (acc::menus::pending::IsPending()) {
                acclog::Write("PowersLevelUp",
                              "Enter -- op already pending; ignoring");
            } else {
                typedef void (__thiscall* PFN_OnPowerPicked)(
                    void*, unsigned long);
                __try {
                    auto fn = reinterpret_cast<PFN_OnPowerPicked>(
                        kAddrCSWGuiPowersLevelUpOnPowerPicked);
                    fn(panel, powerId);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    acclog::Write("PowersLevelUp",
                                  "OnPowerPicked faulted powerId=%u",
                                  (unsigned)powerId);
                }
                acclog::Write("PowersLevelUp",
                              "OnPowerPicked panel=%p powerId=%u",
                              panel, (unsigned)powerId);
                AnnounceFocused(panel);
            }
        }
        outRv = 1;
        return true;
    }

    if (param_1 == kInputEsc1 || param_1 == kInputEsc2) {
        QueueButtonByIdActivate(
            panel, kBtnBackId, "PowersLevelUp: Esc -> BTN_BACK");
        outRv = 1;
        return true;
    }

    return false;
}

}  // namespace acc::menus::powers_levelup
