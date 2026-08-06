// KOTOR 2 pad action menu suppression — see pad_actionmenu.h.

#include "pad_actionmenu.h"

#include <windows.h>
#include <cstdint>

#include "engine_game.h"
#include "engine_rebase.h"
#include "log.h"

namespace acc::pad::actionmenu {

namespace {

// CSWGuiActionMenuIos' open-category global: the index of the category
// currently expanded, -1 for none. Written by the per-category active test in
// FUN_00746950 and read by the menu's draw and input paths. KOTOR 2 only —
// KOTOR 1 has no such class, so PickGlobal resolves it to 0 there and every
// access below declines.
const uintptr_t kAddrPadActionMenuCategory =
    acc::addr::PickGlobal(0, 0x00a10340);

// The engine's own "nothing open" value. Writing it is not a poke into
// undocumented state: it is the value the engine itself parks there, and the
// draw and input paths already have to behave correctly when they see it.
constexpr int kNoCategory = -1;

bool g_wasOpen = false;

bool ReadCategory(int& out) {
    if (!acc::addr::Ok(kAddrPadActionMenuCategory)) return false;
    __try {
        out = *reinterpret_cast<int*>(kAddrPadActionMenuCategory);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

bool CloseEngineMenu() {
    if (!acc::addr::Ok(kAddrPadActionMenuCategory)) return false;
    __try {
        *reinterpret_cast<int*>(kAddrPadActionMenuCategory) = kNoCategory;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

}  // namespace

bool EngineMenuOpen() { return g_wasOpen; }

void Tick() {
    if (!acc::game::IsKotor2()) return;

    int category = kNoCategory;
    if (!ReadCategory(category)) return;

    const bool open = (category != kNoCategory);
    if (open == g_wasOpen) return;

    if (!open) {
        acclog::Write("Pad.ActionMenu", "engine menu closed");
        g_wasOpen = false;
        return;
    }

    // Open edge. Close it and say so.
    //
    // No longer opens ours in its place. The D-Pad is the mod's action surface
    // now (pad_input.cpp), and it opens the unified menu in LIVE mode on the
    // player's own press — so arming a second one from here would fight the
    // one the user is already navigating. This is a backstop, not a route in:
    // the D-Pad codes are consumed before the engine sees them, so the engine
    // menu should never open at all, and a Pad.ActionMenu line in a log means
    // something got past that.
    acclog::Write("Pad.ActionMenu",
                  "engine menu OPENED on category %d (0-5 personal column, "
                  "6-8 target row) — closing it; the D-Pad drives ours",
                  category);
    g_wasOpen = true;

    const bool closed = CloseEngineMenu();
    g_wasOpen = !closed;
}

}  // namespace acc::pad::actionmenu
