// Which game and which build are we injected into?
//
// Why this is its own module
// --------------------------
// Two different questions used to be one. `engine_rebase` asked "is this the
// reference build of KOTOR 1, or the 2004 relink?" purely to decide how to map
// an address. With KOTOR 2 in scope there is a second, broader question —
// "which GAME is this?" — whose answers are consumed far outside address
// handling: feature gating for K1-only content (floor_puzzle, minigame_turret,
// endar_softlock, tutorial_hints, ...), per-game struct offsets, and the
// input layer, where K2 has a native controller path K1 has no counterpart for.
//
// Putting `IsKotor2()` in `acc::addr` would name a helper for the place it was
// first needed rather than for what it does. So identity lives here, and
// `engine_rebase` consumes it. There is exactly ONE detection mechanism.
//
// How detection works
// -------------------
// The PE link timestamp of the *game's* image (not this DLL's). Reading it is
// one header walk of already-mapped memory: no file I/O, no allocation, no
// dependency on another global's initialisation. That matters because the
// engine-address constants that consume this are themselves dynamically
// initialised at load time, in unspecified order across translation units —
// the same constraint that shaped `engine_rebase`.
//
// Hashing the file would be more precise and is what the KPatchManager version
// gate already does before we are ever loaded. By the time this runs, the build
// has been vetted; all we need is to tell the vetted builds apart.
//
// Adding a build
// --------------
// Add the timestamp constant and its `Build` enumerator, map it to a `Title`,
// and give it a name. Anything not listed resolves to `Title::Unknown` /
// `Build::Unknown`, which callers must treat as "do nothing engine-specific" —
// see the failure-mode note on `acc::addr::R`.

#pragma once

#include <cstdint>

namespace acc::game {

// The game itself. Distinct from Build: one title has several builds, and
// nearly all feature gating wants the title, not the build.
enum class Title {
    Kotor1,
    Kotor2,
    Unknown,
};

// A specific executable build. Named for what identifies it, not for a
// distribution — the 2004 relink ships as both Allard's Russian 1.72 and the
// official Polish LEM edition from one binary, and Aspyr's 2015 rebuild is
// shared by the Steam and GOG releases.
enum class Build {
    Kotor1Reference,    // Steam / GoG 1.0.3 and the GoG-equivalent CD re-pack
    Kotor1Relink2004,   // 2004-03-05 relink: Allard RU 1.72, LEM PL
    Kotor2Aspyr2015,    // Aspyr's 2015 rebuild: Steam and GOG
    Unknown,
};

// Both are resolved once and cached for the process lifetime.
Title CurrentTitle();
Build CurrentBuild();

// The two questions almost every caller actually has. An unknown build is
// neither, so a `!IsKotor1()` is NOT the same as `IsKotor2()` — gate on the
// one you mean.
inline bool IsKotor1() { return CurrentTitle() == Title::Kotor1; }
inline bool IsKotor2() { return CurrentTitle() == Title::Kotor2; }

// Short, stable names for logs: "kotor1" / "kotor2" / "unknown", and
// "reference" / "relink-2004-03-05" / "aspyr-2015" / "unknown".
const char* TitleName();
const char* BuildName();

}  // namespace acc::game
