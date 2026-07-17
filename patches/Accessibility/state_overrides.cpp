#include "state_overrides.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"   // kObjectTagOffset
#include "engine_reads.h"  // ReadCExoString
#include "log.h"
#include "strings.h"

namespace acc::state {

namespace {

struct LabelEntry {
    int               value;
    acc::strings::Id  id;
};

struct StateOverride {
    const char*       tag;
    size_t            offset;
    // Sentinel-terminated by id == acc::strings::Id::Count_.
    const LabelEntry* labels;
    // 0 = read the dword at `offset` verbatim (wall-switch mode).
    // Non-zero = treat the dword as a bitfield and map value to
    // (dword & bitMask) ? 1 : 0 — used for the NWScript local-boolean
    // word (CSWVarTable at CSWSObject+0x110, per
    // docs/llm-docs/persistence-scriptvartable.md).
    uint32_t          bitMask;
};

// Taris Sith-base "Lights Out" wall switches (Duros release puzzle in
// the cage module). Position lives at CSWSPlaceable+0x260 as a dword
// toggling between 0 and 1. Derived empirically 2026-05-26 by diffing
// hex windows across two activations of wall3: +0x260 went 0→1→0 on
// wall3 itself and 1→0→1 on wall4 in lock-step, validating the
// Duros's in-dialog hint that "moving one switch moves its neighbour".
//
// Label mapping is the conservative initial guess: 0 = off (red, the
// target state Duros names), 1 = on. If the in-game speech turns out
// inverted on the panels whose initial position we can verify, the
// map is the only thing that needs swapping.
constexpr LabelEntry kWallSwitchLabels[] = {
    {0, acc::strings::Id::ModSettingStateOff},
    {1, acc::strings::Id::ModSettingStateOn},
    {0, acc::strings::Id::Count_},
};

// Star Forge placeable state — captive Jedi (Malak fight, sta_m45ad) and
// the battle-droid control terminals (sta_m45ab/ac). Module scripts track
// "this placeable has been used up" in an NWScript local boolean on the
// placeable itself (the captive conversation's mal_chk_free/drain/kill
// conditions and the terminals' k_psta_genactive condition all read the
// same GetLocalBoolean slot). Local booleans live in the fixed CSWVarTable
// at CSWSObject+0x110, word 0.
//
// The live bit is **2** (0x4): confirmed patch-20260717-125936.log — on
// freeing a captive Jedi the word went 0x00000000 → 0x00000004 in lockstep
// with the animation flipping 10000 → 10075, and a freed Jedi's action
// columns went engine-empty. (An earlier static read of the ncsdis output
// guessed boolean 19; the disassembly constant belonged to the script's
// globals table, not the GetLocalBoolean argument.)
constexpr uint32_t kLocalBoolUsedMask   = 1u << 2;
constexpr size_t   kLocalBoolWordOffset = 0x110;

constexpr LabelEntry kCaptiveJediLabels[] = {
    {0, acc::strings::Id::PlaceableStateCaptive},
    {1, acc::strings::Id::PlaceableStateFreed},
    {0, acc::strings::Id::Count_},
};

constexpr LabelEntry kSfTerminalLabels[] = {
    {0, acc::strings::Id::PlaceableStateActive},
    {1, acc::strings::Id::PlaceableStateDeactivated},
    {0, acc::strings::Id::Count_},
};

constexpr StateOverride kOverrides[] = {
    {"wall1", 0x260, kWallSwitchLabels, 0},
    {"wall2", 0x260, kWallSwitchLabels, 0},
    {"wall3", 0x260, kWallSwitchLabels, 0},
    {"wall4", 0x260, kWallSwitchLabels, 0},
    {"wall5", 0x260, kWallSwitchLabels, 0},
    // Captive Jedi, Malak fight (sta_m45ad).
    {"sta_plc_captive2", kLocalBoolWordOffset, kCaptiveJediLabels, kLocalBoolUsedMask},
    {"sta_plc_captive3", kLocalBoolWordOffset, kCaptiveJediLabels, kLocalBoolUsedMask},
    {"sta_plc_captive4", kLocalBoolWordOffset, kCaptiveJediLabels, kLocalBoolUsedMask},
    {"sta_plc_captive5", kLocalBoolWordOffset, kCaptiveJediLabels, kLocalBoolUsedMask},
    {"sta_plc_captive6", kLocalBoolWordOffset, kCaptiveJediLabels, kLocalBoolUsedMask},
    {"sta_plc_captive7", kLocalBoolWordOffset, kCaptiveJediLabels, kLocalBoolUsedMask},
    {"sta_plc_captive8", kLocalBoolWordOffset, kCaptiveJediLabels, kLocalBoolUsedMask},
    // Battle-droid terminals, Star Forge decks (Terminal Typ A/D/E/F + MK).
    {"k45_plc_assdroid", kLocalBoolWordOffset, kSfTerminalLabels, kLocalBoolUsedMask},
    {"k45_plc_prbdroid", kLocalBoolWordOffset, kSfTerminalLabels, kLocalBoolUsedMask},
    {"k45_plc_excharge", kLocalBoolWordOffset, kSfTerminalLabels, kLocalBoolUsedMask},
    {"k45_plc_wardroid", kLocalBoolWordOffset, kSfTerminalLabels, kLocalBoolUsedMask},
    {"k45_plc_mk",       kLocalBoolWordOffset, kSfTerminalLabels, kLocalBoolUsedMask},
    // Deck-2 turret-defense computers ("Computerterminal").
    {"sta45_turretcomp", kLocalBoolWordOffset, kSfTerminalLabels, kLocalBoolUsedMask},
};

const StateOverride* FindOverride(const char* tag) {
    if (!tag || !tag[0]) return nullptr;
    for (const auto& o : kOverrides) {
        if (std::strcmp(o.tag, tag) == 0) return &o;
    }
    return nullptr;
}

const char* PickLabel(const LabelEntry* labels, int value) {
    for (const LabelEntry* p = labels;
         p->id != acc::strings::Id::Count_; ++p) {
        if (p->value == value) return acc::strings::Get(p->id);
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
        uint32_t raw = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(gameObject) + ov->offset);
        value = ov->bitMask ? ((raw & ov->bitMask) ? 1 : 0)
                            : static_cast<int>(raw);
        if (ov->bitMask) {
            // Polarity/mechanism breadcrumb for the Star Forge placeables:
            // raw local-boolean word + the usable flag (CSWSPlaceable+0x328)
            // + current animation (CSWSObject+0xd4). If live speech is
            // inverted, this line shows which signal actually flipped.
            int usable = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(gameObject) + 0x328);
            int anim   = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(gameObject) + 0xd4);
            acclog::Write("PlaceableState",
                          "tag=%s bools=0x%08x bit=%d usable=%d anim=%d",
                          tag, raw, value, usable, anim);
        }
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
