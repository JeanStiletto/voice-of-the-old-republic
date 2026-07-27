#include "map_shipped_hints.h"

#include <cstdio>
#include <cstring>

#include "engine_area.h"  // GetCurrentAreaResName

namespace acc::map_shipped_hints {

namespace {

// Positions from the module .git files:
//  - danm14ad: dan14ad_door02, the unmarked Sandral-estate backdoor (the
//    front door carries the vanilla "Anwesen der Sandral" note; this one
//    has none and is the feud-quest sneak entrance).
//  - tar_m05aa: the two lootable "Toter Rebell" containers
//    (tar05_promcorpse) carrying the Promised-Land datapads.
//  - tat_m18ac: centroid of the 12 bantha patrol waypoints — the herd
//    wanders a ~90x160m band, but one bantha in sight is enough to
//    progress Komad Fortuna's quest.
const ShippedHint kHints[] = {
    { "danm14ad",  { 353.7f, 121.7f, 56.3f }, acc::strings::Id::HintBackEntrance },
    { "tar_m05aa", { 252.8f, 159.1f, 28.9f }, acc::strings::Id::HintRebelCorpse },
    { "tar_m05aa", { 272.3f,  47.4f, 21.0f }, acc::strings::Id::HintRebelCorpse },
    { "tat_m18ac", { 230.5f, 191.7f, 81.6f }, acc::strings::Id::HintBanthaHerd },
};
constexpr int kHintCount = sizeof(kHints) / sizeof(kHints[0]);

bool ModuleMatches(const char* module) {
    char current[64] = "";
    return acc::engine::GetCurrentAreaResName(current, sizeof(current)) &&
           _stricmp(current, module) == 0;
}

// Row-pointer identity: exact address of a table element.
const ShippedHint* AsTableRow(const void* p) {
    for (int i = 0; i < kHintCount; ++i) {
        if (p == &kHints[i]) return &kHints[i];
    }
    return nullptr;
}

}  // namespace

int CollectForCurrentModule(const ShippedHint** out, int maxOut) {
    if (!out || maxOut <= 0) return 0;
    char current[64] = "";
    if (!acc::engine::GetCurrentAreaResName(current, sizeof(current)) ||
        current[0] == '\0') {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < kHintCount && n < maxOut; ++i) {
        if (_stricmp(kHints[i].module, current) == 0) {
            out[n++] = &kHints[i];
        }
    }
    return n;
}

bool IsShippedHint(const void* p) {
    const ShippedHint* row = AsTableRow(p);
    return row && ModuleMatches(row->module);
}

bool GetLabel(const void* p, char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    const ShippedHint* row = AsTableRow(p);
    if (!row) return false;
    std::snprintf(outBuf, bufSize, "%s", acc::strings::Get(row->label));
    return outBuf[0] != '\0';
}

}  // namespace acc::map_shipped_hints
