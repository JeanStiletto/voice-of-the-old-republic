#include "engine_game.h"

#include <windows.h>

namespace acc::game {

namespace {

// PE link timestamps. See the header for why the timestamp and not the hash.
//
// The two KOTOR 1 values were already load-bearing in engine_rebase.cpp, which
// now consumes this module rather than reading the header itself.
constexpr uint32_t kTsKotor1Reference  = 0x402BC2D9;  // 2004-02-12 18:15:53Z
constexpr uint32_t kTsKotor1Relink2004 = 0x4047CD47;  // 2004-03-05 00:43:51Z

// Aspyr's rebuild, 2015-09-23 19:41:17Z. Verified against the installed Steam
// copy, whose SHA-256 also matches the `kotor2_steam_aspyr` entry in
// KPatchManager's AddressDatabases — i.e. the framework and we agree on which
// binary this is. The GOG Aspyr release is treated as the same build; both
// carry identical seeded addresses upstream. If a GOG copy ever reports a
// different timestamp it needs its own enumerator, not a widened match.
constexpr uint32_t kTsKotor2Aspyr2015  = 0x5603005D;  // 2015-09-23 19:41:17Z

uint32_t ReadGameLinkTimestamp() {
    // GetModuleHandle(nullptr) is the EXE, not this DLL. The identity we want
    // is the game's, and this module is loaded into it.
    auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return 0;

    // Deliberately defensive: this runs before anything has validated the
    // image, and a fault here would happen during static initialisation, which
    // is painful to diagnose. Every step is bounds-checked.
    if (base[0] != 'M' || base[1] != 'Z') return 0;
    int32_t lfanew = *reinterpret_cast<const int32_t*>(base + 0x3C);
    if (lfanew <= 0 || lfanew > 0x1000) return 0;
    const uint8_t* pe = base + lfanew;
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0) return 0;
    return *reinterpret_cast<const uint32_t*>(pe + 8);
}

Build DetectBuild() {
    switch (ReadGameLinkTimestamp()) {
        case kTsKotor1Reference:  return Build::Kotor1Reference;
        case kTsKotor1Relink2004: return Build::Kotor1Relink2004;
        case kTsKotor2Aspyr2015:  return Build::Kotor2Aspyr2015;
        default:                  return Build::Unknown;
    }
}

Title TitleOf(Build b) {
    switch (b) {
        case Build::Kotor1Reference:
        case Build::Kotor1Relink2004:
            return Title::Kotor1;
        case Build::Kotor2Aspyr2015:
            return Title::Kotor2;
        default:
            return Title::Unknown;
    }
}

}  // namespace

// Function-local static: initialised on first use under the C++11 magic-statics
// rule, with no dependency on any other global's initialisation order. That is
// the point — the engine-address constants that reach this are themselves
// dynamically initialised at load time, in unspecified order across TUs.
Build CurrentBuild() {
    static const Build b = DetectBuild();
    return b;
}

Title CurrentTitle() {
    static const Title t = TitleOf(CurrentBuild());
    return t;
}

const char* TitleName() {
    switch (CurrentTitle()) {
        case Title::Kotor1: return "kotor1";
        case Title::Kotor2: return "kotor2";
        default:            return "unknown";
    }
}

const char* BuildName() {
    switch (CurrentBuild()) {
        case Build::Kotor1Reference:  return "reference";
        case Build::Kotor1Relink2004: return "relink-2004-03-05";
        case Build::Kotor2Aspyr2015:  return "aspyr-2015";
        default:                      return "unknown";
    }
}

}  // namespace acc::game
