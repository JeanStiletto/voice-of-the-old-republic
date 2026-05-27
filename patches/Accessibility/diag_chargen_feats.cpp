// See diag_chargen_feats.h for purpose + removal note.

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "diag_chargen_feats.h"

#include "engine_offsets.h"
#include "log.h"

namespace acc::diag::chargen_feats {

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

void DumpStructureIfNeeded(void* panel) {
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

}  // namespace acc::diag::chargen_feats
