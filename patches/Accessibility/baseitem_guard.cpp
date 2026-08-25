// Out-of-range BaseItem guard (both games).
//
// Every item template (.uti) names a baseitems.2da row via its BaseItem
// field, and the engine resolves that row through one funnel:
// CSWBaseItemArray::GetBaseItem(int id) — K1 0x005b31a0, K2 0x006d5200.
// Decompiled 2026-08-25 (both games, identical shape): count at this+0,
// row array at this+4, 200-byte row stride; returns NULL when the table
// is not loaded, id < 0, or id >= count.
//
// Several engine callers dereference the returned row without a null
// check — K2's GFF item loader (FUN_006014f0) null-checks a FIELD of the
// row one instruction after reading through the row pointer. A third-party
// item whose baseitems.2da row is missing from the effective Override
// therefore crashes the game on every load of the area containing it
// (Kenny's Peragus Fuel Depot crash, docs/known-issues.md).
//
// The guard hooks the accessor mid-function, AFTER the engine's own
// table-loaded check has passed (the not-loaded NULL return is deliberately
// left untouched — startup callers may probe before the 2DA is read) and
// substitutes row 0 for an out-of-range id: a strange stand-in item instead
// of a crash. Hook mechanics differ per game because K1 is compiled
// optimised and K2 is not — see the hook comments in hooks.toml /
// kotor2.hooks.toml.

#include <windows.h>

#include "engine_game.h"
#include "log.h"

// KOTOR 1 — hooked at 0x005b31ab with the engine's entire bounds check
// (TEST EAX,EAX / JL / CMP EAX,[ECX] / JGE, 8 bytes) cut and NOT replayed:
// the two relative jumps in the cut cannot be relocated, so this handler IS
// the bounds check. ECX = CSWBaseItemArray* (count at +0), EAX = id. The
// wrapper excludes EAX from its register restore, so our return value is the
// id the engine resumes with at the row computation (IMUL EAX,EAX,0xc8).
//
// The engine's not-loaded check (JZ on the array pointer) sits BEFORE the
// hook, so the array is non-NULL whenever we run. count <= 0 with a loaded
// array does not occur — the 2DA loader allocates the array and sets count
// together — so clamping to row 0 is always backed by a real row.
extern "C" int __cdecl OnGetBaseItem(void* thisPtr, int id) {
    if (acc::game::IsKotor2()) return id;  // belt and braces; the hook is K1-only

    int count = 0;
    __try {
        count = *reinterpret_cast<int*>(thisPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return id;  // unreadable `this` — engine read it moments ago, cannot happen
    }

    if (id >= 0 && id < count) return id;

    acclog::Trace("BaseItemGuard",
                  "out-of-range BaseItem id=%d (table has %d rows) -- substituting row 0",
                  id, count);
    return 0;
}

// KOTOR 2 — hooked at 0x006d5210, after the prologue and the engine's
// table-loaded check. K2's build is unoptimised: it keeps a frame pointer
// and reloads the id from [EBP+8] at every use, so rewriting that one frame
// slot substitutes the id everywhere downstream. The cut (CMP [EBP+8],0 /
// JL, 6 bytes) is the engine's id<0 check — skipped, because the JL is a
// relative jump; the clamp below covers it, and the engine's own id>=count
// check still runs against the rewritten slot on resume.
//
//   [EBP-4] = this  (CSWBaseItemArray*, count at +0)
//   [EBP+8] = id    (rewritten to 0 when out of range)
extern "C" void __cdecl OnGetBaseItemK2(void* ebp) {
    if (!acc::game::IsKotor2()) return;  // belt and braces; the hook is K2-only
    if (!ebp) return;

    __try {
        char* frame = static_cast<char*>(ebp);
        void* self = *reinterpret_cast<void**>(frame - 4);
        int* idSlot = reinterpret_cast<int*>(frame + 8);
        if (!self) return;

        int count = *reinterpret_cast<int*>(self);
        int id = *idSlot;
        if (id >= 0 && id < count) return;

        acclog::Trace("BaseItemGuard",
                      "out-of-range BaseItem id=%d (table has %d rows) -- substituting row 0",
                      id, count);
        *idSlot = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Frame unreadable — leave the engine to its own NULL path.
    }
}
