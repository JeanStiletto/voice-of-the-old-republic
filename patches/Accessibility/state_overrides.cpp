#include "state_overrides.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"   // kObjectTagOffset
#include "engine_reads.h"  // ReadCExoString
#include "strings.h"

namespace acc::state {

namespace {

struct LabelEntry {
    int         value;
    const char* labelDe;
    const char* labelEn;
};

struct StateOverride {
    const char*       tag;
    size_t            offset;
    // Sentinel-terminated by labelDe == nullptr.
    const LabelEntry* labels;
};

// Taris Sith-base "Lights Out" wall switches (Duros release puzzle in
// the cage module). Position lives at CSWSPlaceable+0x260 as a dword
// toggling between 0 and 1. Derived empirically 2026-05-26 by diffing
// hex windows across two activations of wall3: +0x260 went 0→1→0 on
// wall3 itself and 1→0→1 on wall4 in lock-step, validating the
// Duros's in-dialog hint that "moving one switch moves its neighbour".
//
// Label mapping is the conservative initial guess: 0 = Aus (red, the
// target state Duros names), 1 = An. If the in-game speech turns out
// inverted on the panels whose initial position we can verify, the
// map is the only thing that needs swapping.
constexpr LabelEntry kWallSwitchLabels[] = {
    {0, "Aus", "off"},
    {1, "An",  "on"},
    {0, nullptr, nullptr},
};

constexpr StateOverride kOverrides[] = {
    {"wall1", 0x260, kWallSwitchLabels},
    {"wall2", 0x260, kWallSwitchLabels},
    {"wall3", 0x260, kWallSwitchLabels},
    {"wall4", 0x260, kWallSwitchLabels},
    {"wall5", 0x260, kWallSwitchLabels},
};

const StateOverride* FindOverride(const char* tag) {
    if (!tag || !tag[0]) return nullptr;
    for (const auto& o : kOverrides) {
        if (std::strcmp(o.tag, tag) == 0) return &o;
    }
    return nullptr;
}

const char* PickLabel(const LabelEntry* labels, int value) {
    const bool german =
        acc::strings::GetLanguage() == acc::strings::Lang::De;
    for (const LabelEntry* p = labels; p->labelDe; ++p) {
        if (p->value == value) return german ? p->labelDe : p->labelEn;
    }
    return nullptr;
}

}  // namespace

bool AppendStateLabel(void* gameObject, char* outBuf, size_t bufSize) {
    if (!gameObject || !outBuf || bufSize < 4 || outBuf[0] == '\0') {
        return false;
    }

    char tag[64] = "";
    __try {
        acc::engine::ReadCExoString(gameObject, kObjectTagOffset,
                                    tag, sizeof(tag));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (tag[0] == '\0') return false;

    const StateOverride* ov = FindOverride(tag);
    if (!ov) return false;

    int value = 0;
    __try {
        value = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(gameObject) + ov->offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    const char* label = PickLabel(ov->labels, value);
    if (!label) return false;

    size_t curLen = std::strlen(outBuf);
    if (curLen + std::strlen(label) + 3 >= bufSize) return false;
    std::snprintf(outBuf + curLen, bufSize - curLen, ", %s", label);
    return true;
}

}  // namespace acc::state
