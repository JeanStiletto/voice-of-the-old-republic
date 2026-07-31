// Engine-address rebasing for non-reference builds of swkotor.exe.
//
// Why
// ---
// Every engine address in this patch is written as its value in the reference
// build (Steam/GoG 1.0.3). Another build of the same BioWare source exists —
// the 2004-03-05 relink, shipped by both the Allard Russian translation and the
// official Polish LEM edition — where the functions are byte-identical but the
// linker placed them elsewhere (measured -320..+640 bytes, varying per function
// and not monotonic, so no single offset works).
//
// Rather than duplicate ~190 constants per build, each constant is written once
// against the reference and passed through R(). On the reference build R() is
// the identity function; on a known other build it maps through a generated
// table (engine_rebase_table.inc, produced by `kdev sigscan`).
//
// Scope: .text and .rdata addresses both need this; .data is byte-stable across
// these builds and is left alone. The .rdata half was a late discovery — the
// original reasoning here was that we held no bare .rdata constants, only vtable
// references inside functions that move with their function. That was wrong (44
// of them), and on a rebased build every vtable-identity check silently failed
// until engine_rebase_rdata.inc was added.
//
// Which build we are running in comes from `engine_game.h`, which owns that
// question for the whole patch (it also answers "which GAME", which matters
// far outside address handling). Detection there is a header walk of the
// already-mapped image — no file I/O, no allocation — so it is safe from a
// static initialiser and from DllMain, which matters because the constants
// that call R() are themselves initialised at load time.
//
// Failure mode: an address that is NOT in the table on a rebased build returns
// 0 rather than the reference value. A call through a null pointer faults at
// address 0 and is instantly recognisable in a crash dump; jumping to a stale
// address lands in the middle of an unrelated function and corrupts state
// first. Guard such call sites with Ok().
//
// KOTOR 2 returns 0 for EVERY address, because every constant here is a
// KOTOR 1 address and K2 is a recompile rather than a relink — see the
// Kotor2Aspyr2015 case in R(). Ok() therefore covers the handful of guarded
// sites, but it is NOT the mechanism that makes K2 safe: engine-touching code
// must be gated on acc::game::IsKotor1() before it runs at all.

#pragma once

#include <cstdint>

namespace acc::addr {

// Map a reference-build address to the running build. Identity on the
// reference build. Returns 0 if the running build is known but the address is
// not in its table.
uintptr_t R(uintptr_t referenceVa);

// True when the running executable is not the reference build, i.e. R() is
// actually remapping. Cheap after the first call.
bool IsRebased();

// Short name of the detected build, for logs: "reference", "relink-2004-03-05",
// or "unknown". Stable for the process lifetime. The two distributions sharing
// the relink are not told apart here — they are the same code, and which
// translation is installed is reported separately by the language detection.
const char* ActiveBuildName();

// Guard for addresses that may be unresolved on a rebased build. Use at call
// sites for any address sigscan could not place:
//     if (!acc::addr::Ok(kAddrFoo)) return;
inline bool Ok(uintptr_t va) { return va != 0; }

// --------------------------------------------------------------------------
// KOTOR 2
// --------------------------------------------------------------------------
// Addresses differ from struct offsets in one way that matters here: they are
// globally unique, so R()'s reference-address-keyed table works. But KOTOR 2
// values are hand-curated one at a time (from RTTI, from the seeded upstream
// database, from decompilation) rather than generated wholesale by sigscan, so
// they are written at the DECLARATION, where it is obvious which constant they
// belong to, instead of in an anonymous two-column table.
//
// Note there is no "Same" for an address, unlike acc::off: a recompile
// relocates everything, so a KOTOR 1 address is NEVER valid in KOTOR 2. Every
// address is therefore in exactly one of two states — known (Pick/PickGlobal)
// or not yet found (plain R() / TodoGlobal, both of which resolve to 0).

// .text / .rdata address whose KOTOR 2 value is known. On KOTOR 1 builds this
// is exactly R(), so relink rebasing still applies there.
uintptr_t Pick(uintptr_t referenceVa, uintptr_t kotor2Va);

// .data global pointer whose KOTOR 2 value is known.
//
// Separate from Pick() because .data does NOT go through R(): it is byte-stable
// across the KOTOR 1 builds, so it is never rebased, and feeding it to R()
// would resolve it to 0 on the relink (it is not in that table). Only the
// cross-GAME switch applies.
uintptr_t PickGlobal(uintptr_t kotor1Va, uintptr_t kotor2Va);

// .data global pointer whose KOTOR 2 value is NOT yet known. Returns the
// KOTOR 1 value on KOTOR 1 and 0 on KOTOR 2.
//
// This exists because a bare `constexpr uintptr_t` would hand out the KOTOR 1
// address on KOTOR 2 — a valid-looking pointer into unrelated data, read
// silently and wrongly. Every raw .data address must use this or PickGlobal;
// none may stay constexpr.
uintptr_t TodoGlobal(uintptr_t kotor1Va);

}  // namespace acc::addr
