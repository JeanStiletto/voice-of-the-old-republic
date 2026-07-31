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
// Detection reads the PE link timestamp out of our own mapped image. No file
// I/O and no allocation, so it is safe from a static initialiser and from
// DllMain — which matters, because the constants that call R() are themselves
// initialised at load time.
//
// Failure mode: an address that is NOT in the table on a rebased build returns
// 0 rather than the reference value. A call through a null pointer faults at
// address 0 and is instantly recognisable in a crash dump; jumping to a stale
// address lands in the middle of an unrelated function and corrupts state
// first. Guard such call sites with Ok().

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

}  // namespace acc::addr
