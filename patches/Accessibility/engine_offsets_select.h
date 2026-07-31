// Per-game selection for struct field offsets.
//
// Why this exists
// ---------------
// `acc::addr::R()` solves the same problem for .text ADDRESSES, but it cannot
// be reused here, and the reason is worth stating because it is not obvious:
// R() is a table keyed on the reference address, which works only because
// addresses are globally unique. Offsets are not. `+0x4` is CAppManager.Client
// AND CGameObject.Id AND a dozen others, so there is no key to look up. The
// per-game value has to be chosen at the declaration site, where the field's
// identity is known.
//
// Hence three markers rather than a table. Each constant initialises itself at
// load time from its own declaration, exactly as the R()-wrapped addresses do,
// so there is no cross-translation-unit initialisation-order problem: the only
// shared state is acc::game's function-local static.
//
// Choosing a marker
// -----------------
//   Same(x)      — verified IDENTICAL in both games. constexpr, so free.
//   Pick(k1, k2) — verified DIFFERENT.
//   Todo(k1)     — KOTOR 1 value known, KOTOR 2 value not yet established.
//
// `Same` and `Todo` look the same from KOTOR 1 and differ only on KOTOR 2, and
// that distinction is the entire point: "checked, and it happens to match" must
// not be confusable with "never checked". Do not use Same() to silence a Todo()
// you have not actually verified — that converts an honest gap into a silent
// wrong read.
//
// `grep -c "Todo("` is the remaining-work counter for the port.
//
// What is NOT here
// ----------------
// engine_offsets_values.h holds constants that are not field offsets — vtable
// slot indices, TLK strrefs, action-type enum bytes, panel input codes. Those
// vary between the games along different axes (a strref differs because the
// TLK differs, not because a struct grew) and are handled separately.
//
// Expected shape of the KOTOR 2 answers
// -------------------------------------
// Measured against the 21 offsets both seeded AddressDatabases share: base and
// shallow classes are identical (CAppManager, CExoString, CGameObject,
// CSWBaseItem), while derived classes shift by a CONSTANT delta per class —
// every CSWSObject field by 4, every CSWSCreatureStats field by 4, both
// CSWSCreature fields by the same larger amount. That is field insertion near
// the top of a base class. So verifying two fields of a class usually carries
// the rest — but the delta is piecewise (identity below the insertion point,
// constant above it), so confirm rather than extrapolate blindly.

#pragma once

#include <cstddef>

#include "engine_game.h"

namespace acc::off {

// Returned by Todo() on KOTOR 2. Deliberately huge and recognisable: added to
// any base pointer it lands far outside anything the process has mapped, so the
// read faults instead of silently returning a neighbouring field's bytes.
//
// Most reads here run under SEH, so in practice this surfaces as a failed read
// that the caller already handles — which is the graceful outcome. It is a
// BACKSTOP, not a guarantee (no single offset can fault for every base), and it
// is not what makes KOTOR 2 safe. Gating engine-touching code on
// acc::game::IsKotor1() is. See docs/kotor2-port.md.
inline constexpr size_t kUnportedOffset = 0x7BAD0000;

// Verified identical in both games. constexpr: resolves at compile time and
// costs nothing at run time.
constexpr size_t Same(size_t both) { return both; }

// Verified different between the games.
inline size_t Pick(size_t kotor1, size_t kotor2) {
    return acc::game::IsKotor2() ? kotor2 : kotor1;
}

// KOTOR 1 value known; KOTOR 2 value not yet reverse-engineered. Poisons on
// KOTOR 2 so a premature read fails loudly rather than reading the wrong field.
inline size_t Todo(size_t kotor1) {
    return acc::game::IsKotor2() ? kUnportedOffset : kotor1;
}

// The field belongs to a subsystem KOTOR 2 does not have, so there is no K2
// value to find — the turret minigame, for instance. Behaves exactly like
// Todo() at run time; the difference is bookkeeping, and it matters: it keeps
// `grep -c "Todo("` counting work that actually remains instead of work that
// will never happen. The module itself should also be gated on
// acc::game::IsKotor1(), which is what really stops these being read.
inline size_t Kotor1Only(size_t kotor1) {
    return acc::game::IsKotor2() ? kUnportedOffset : kotor1;
}

// Is this offset usable on the running game? False for the poison value.
//
// Needed because the poison's fault-loudly contract only holds for a READ.
// `base + poison` is itself a perfectly ordinary arithmetic result — a wild
// but decidedly NON-NULL pointer — so any code that forms such a pointer and
// hands it onward defeats every `if (!p)` check between there and the
// eventual dereference. That is how an in-world arrow key crashed KOTOR 2
// (2026-08-01): a dialogue panel's reply-listbox pointer was
// `panel + Todo(0x19c4)`, the null check passed, and an unguarded helper
// dereferenced it. Full record in docs/kotor2-port.md.
inline bool Ok(size_t off) { return off != kUnportedOffset; }

// Form an interior pointer, or nullptr when the offset is unported. Use this
// wherever `base + kSomeOffset` is RETURNED or STORED rather than immediately
// read under SEH — it converts a silent wild pointer into an honest null that
// every existing guard already handles.
inline void* Ptr(void* base, size_t off) {
    if (!base || !Ok(off)) return nullptr;
    return static_cast<unsigned char*>(base) + off;
}

}  // namespace acc::off
