#include "cycle_input.h"

#include "cycle_state.h"
#include "engine_input.h"
#include "engine_player.h"
#include "filter_objects.h"
#include "log.h"

namespace acc::cycle_input {

namespace {

// Shift-held flag, mutated by shift up/down events. Tracked inside our
// hook because the manager doesn't bake shift into a modifier flag —
// DirectInput delivers each key as its own event.
bool g_shiftHeld = false;

void LogFocus(const char* keyName, const acc::cycle::CategoryListing& l) {
    auto& s = acc::cycle::GetState();
    const char* cat = acc::filter::CategoryName(s.category);
    if (s.focusedObj) {
        acclog::Write("Cycle: %s%s -> category=%s index=%d/%d obj=%p dist=%.2fm",
                      g_shiftHeld ? "Shift+" : "", keyName, cat,
                      s.focusedIndex + 1, l.count, s.focusedObj,
                      l.distances[s.focusedIndex]);
    } else {
        acclog::Write("Cycle: %s%s -> category=%s (empty)",
                      g_shiftHeld ? "Shift+" : "", keyName, cat);
    }
}

}  // namespace

bool TryHandleEvent(int param_1, int param_2) {
    // Shift state tracking — fires on both press and release, never consumed.
    if (param_1 == kInputKbLeftShift || param_1 == kInputKbRightShift) {
        g_shiftHeld = (param_2 != 0);
        return false;
    }

    // Press only beyond this point.
    if (param_2 == 0) return false;

    // Filter to the four codes we care about before any further work.
    if (param_1 != kInputKbComma  &&
        param_1 != kInputKbPeriod &&
        param_1 != kInputKbMinus) {
        return false;
    }

    // Gate on "is the player actually in-game?". GetPlayerPosition returns
    // false on menus / chargen pre-spawn / area-load mid-flight, in which
    // case we let the key pass through to its normal engine handler.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return false;

    acc::cycle::CategoryListing listing;

    if (param_1 == kInputKbComma) {
        if (g_shiftHeld) {
            acc::cycle::CyclePrevCategory(listing);
            LogFocus(",", listing);
        } else {
            acc::cycle::RefreshCurrentListing(listing);
            acc::cycle::CyclePrevItem(listing);
            LogFocus(",", listing);
        }
        return true;
    }

    if (param_1 == kInputKbPeriod) {
        if (g_shiftHeld) {
            acc::cycle::CycleNextCategory(listing);
            LogFocus(".", listing);
        } else {
            acc::cycle::RefreshCurrentListing(listing);
            acc::cycle::CycleNextItem(listing);
            LogFocus(".", listing);
        }
        return true;
    }

    if (param_1 == kInputKbMinus) {
        // `-` = announce (lay-off 4), `Shift+-` = pathfind (lay-off 6).
        // Stub for now: refresh the listing so the focused-object pointer
        // is current, then log what we would have spoken / pathed-to.
        acc::cycle::RefreshCurrentListing(listing);
        auto& s = acc::cycle::GetState();
        const char* cat = acc::filter::CategoryName(s.category);
        const char* action = g_shiftHeld ? "pathfind" : "announce";
        if (s.focusedObj && s.focusedIndex >= 0 &&
            s.focusedIndex < listing.count) {
            const Vector& p = listing.positions[s.focusedIndex];
            acclog::Write("Cycle: %s- -> %s focused=%p dist=%.2fm "
                          "pos=(%.2f,%.2f,%.2f) [%s stub]",
                          g_shiftHeld ? "Shift+" : "", cat, s.focusedObj,
                          listing.distances[s.focusedIndex],
                          p.x, p.y, p.z, action);
        } else {
            acclog::Write("Cycle: %s- -> %s (no focus) [%s stub]",
                          g_shiftHeld ? "Shift+" : "", cat, action);
        }
        return true;
    }

    return false;
}

}  // namespace acc::cycle_input
