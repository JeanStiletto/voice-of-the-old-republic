// Engine-address rebasing for non-reference builds of swkotor.exe.
//
// Why
// ---
// Every engine address in this patch is written as its value in the reference
// build (Steam/GoG 1.0.3). Other builds of the same BioWare source exist — the
// Allard Russian translation ships one — where the functions are byte-identical
// but the linker placed them elsewhere (measured -320..+640 bytes, varying per
// function and not monotonic, so no single offset works).
//
// Rather than duplicate ~190 constants per build, each constant is written once
// against the reference and passed through R(). On the reference build R() is
// the identity function; on a known other build it maps through a generated
// table (engine_rebase_table.inc, produced by `kdev sigscan`).
//
// Scope: only .text addresses need this. .data addresses are byte-stable
// across these builds and are left alone. .rdata is NOT stable, but we hold no
// bare .rdata constants — only vtable references inside functions, which move
// with their function.
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

// Short name of the detected build, for logs: "reference", "allard-1.72", or
// "unknown". Stable for the process lifetime.
const char* ActiveBuildName();

// Guard for addresses that may be unresolved on a rebased build. Use at call
// sites for any address sigscan could not place:
//     if (!acc::addr::Ok(kAddrFoo)) return;
inline bool Ok(uintptr_t va) { return va != 0; }

}  // namespace acc::addr
