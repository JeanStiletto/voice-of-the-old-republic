#include "weapon_set_watch.h"

#include <windows.h>  // SEH, GetTickCount64
#include <cstdio>
#include <cstring>

#include "combat_query.h"    // ReadEquippedItemName
#include "engine_game.h"     // IsKotor2
#include "engine_offsets.h"  // kCreatureInventoryOffset, kInventory*HandleOffset
#include "engine_player.h"   // GetPlayerServerCreature
#include "log.h"
#include "prism.h"
#include "strings.h"

namespace acc::weapon_set_watch {

namespace {

// The four weapon-slot handles, empty slots normalised to 0 so the
// exchange compare doesn't care which empty sentinel the engine used.
struct SlotState {
    uint32_t main;  // slot 4  (right weapon, active pair)
    uint32_t off;   // slot 5  (left weapon, active pair)
    uint32_t main2; // slot 18 (right weapon, secondary pair)
    uint32_t off2;  // slot 19 (left weapon, secondary pair)
};

void* g_prevLeader = nullptr;  // baseline owner — re-baseline on leader change
bool  g_baselined  = false;
SlotState g_prev{};

// A swap's two per-pair equips can land on different server ticks, which
// would fire the per-pair detection twice; collapse announcements inside
// this window.
constexpr ULONGLONG kReAnnounceGapMs = 800;
ULONGLONG g_lastSpokeMs = 0;

uint32_t NormalizeHandle(uint32_t h) {
    return (h == 0xFFFFFFFFu || h == 0x7F000000u) ? 0u : h;
}

// Reads the four slot handles off the leader's CSWInventory. False when
// any hop is unreadable (no inventory yet — loading screens).
bool ReadSlots(void* leader, SlotState& out) {
    if (!leader) return false;
    void* inventory = nullptr;
    __try {
        inventory = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(leader) +
            kCreatureInventoryOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!inventory) return false;
    __try {
        unsigned char* base = reinterpret_cast<unsigned char*>(inventory);
        out.main  = NormalizeHandle(*reinterpret_cast<uint32_t*>(
            base + kInventoryRightWeaponHandleOffset));
        out.off   = NormalizeHandle(*reinterpret_cast<uint32_t*>(
            base + kInventoryLeftWeaponHandleOffset));
        out.main2 = NormalizeHandle(*reinterpret_cast<uint32_t*>(
            base + kInventoryRightWeapon2HandleOffset));
        out.off2  = NormalizeHandle(*reinterpret_cast<uint32_t*>(
            base + kInventoryLeftWeapon2HandleOffset));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SameState(const SlotState& a, const SlotState& b) {
    return a.main == b.main && a.off == b.off &&
           a.main2 == b.main2 && a.off2 == b.off2;
}

// The engine's swap exchanges a pair's contents: what was in the active
// slot is now in the secondary slot and vice versa. Per-pair, so a swap
// whose two pairs land on different ticks still matches each tick.
bool PairExchanged(uint32_t prevA, uint32_t prevB,
                   uint32_t newA, uint32_t newB) {
    return newA == prevB && newB == prevA && (prevA != 0 || prevB != 0);
}

void SpeakNewLoadout(void* leader) {
    char mainName[128];
    char offName[128];
    bool haveMain = acc::combat::query::ReadEquippedItemName(
        leader, kInventoryRightWeaponHandleOffset, "weapon-swap-main",
        mainName, sizeof(mainName));
    bool haveOff = acc::combat::query::ReadEquippedItemName(
        leader, kInventoryLeftWeaponHandleOffset, "weapon-swap-off",
        offName, sizeof(offName));

    char buf[320];
    if (haveMain && haveOff) {
        std::snprintf(buf, sizeof(buf),
                      acc::strings::Get(acc::strings::Id::FmtWeaponSwitchedTwo),
                      mainName, offName);
    } else if (haveMain || haveOff) {
        std::snprintf(buf, sizeof(buf),
                      acc::strings::Get(acc::strings::Id::FmtWeaponSwitchedOne),
                      haveMain ? mainName : offName);
    } else {
        // Swapped to an empty pair (bare hands), or names unresolvable.
        std::snprintf(buf, sizeof(buf), "%s",
                      acc::strings::Get(acc::strings::Id::WeaponSwitchedBare));
    }
    prism::Speak(buf, /*interrupt=*/true);
}

}  // namespace

void Tick() {
    if (!acc::game::IsKotor2()) return;

    void* leader = acc::engine::GetPlayerServerCreature();
    SlotState now{};
    if (!ReadSlots(leader, now)) {
        g_prevLeader = nullptr;
        g_baselined  = false;
        return;
    }

    // Leader changed (Tab, party swap, load) — take a silent baseline so
    // switching to a companion never announces their standing loadout.
    if (leader != g_prevLeader || !g_baselined) {
        g_prevLeader = leader;
        g_baselined  = true;
        g_prev       = now;
        return;
    }

    if (SameState(g_prev, now)) return;

    bool mainSwap = PairExchanged(g_prev.main, g_prev.main2, now.main, now.main2);
    bool offSwap  = PairExchanged(g_prev.off,  g_prev.off2,  now.off,  now.off2);
    // The pair that isn't part of the exchange must be untouched this tick,
    // otherwise this is some other simultaneous gear change.
    bool mainQuiet = now.main == g_prev.main && now.main2 == g_prev.main2;
    bool offQuiet  = now.off  == g_prev.off  && now.off2  == g_prev.off2;
    bool swapped = (mainSwap && offSwap) || (mainSwap && offQuiet) ||
                   (offSwap && mainQuiet);

    if (swapped) {
        acclog::Write("WeaponSetWatch",
                      "swap: main 0x%08x<->0x%08x off 0x%08x<->0x%08x",
                      g_prev.main, now.main, g_prev.off, now.off);
        ULONGLONG t = GetTickCount64();
        if (g_lastSpokeMs == 0 || t - g_lastSpokeMs >= kReAnnounceGapMs) {
            SpeakNewLoadout(leader);
            g_lastSpokeMs = t;
        }
    }

    g_prev = now;
}

}  // namespace acc::weapon_set_watch
