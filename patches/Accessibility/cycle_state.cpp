#include "cycle_state.h"

#include <cmath>
#include <cstdint>

#include "discovery.h"
#include "engine_area.h"
#include "engine_player.h"
#include "log.h"
#include "map_user_markers.h"
#include "menus_modsettings.h"

namespace acc::cycle {

namespace {

// Insertion sort by distance ascending. Reorders objs / positions /
// distances in lockstep. Stable; cheap at N≤64 (worst case ~2K compares,
// same order as a single SEH frame).
void SortByDistanceAscending(CategoryListing& l) {
    for (int i = 1; i < l.count; ++i) {
        void*  saveObj = l.objs[i];
        Vector savePos = l.positions[i];
        float  saveDst = l.distances[i];
        bool   saveIsPin = l.isPin[i];
        int j = i - 1;
        while (j >= 0 && l.distances[j] > saveDst) {
            l.objs[j + 1]      = l.objs[j];
            l.positions[j + 1] = l.positions[j];
            l.distances[j + 1] = l.distances[j];
            l.isPin[j + 1]     = l.isPin[j];
            --j;
        }
        l.objs[j + 1]      = saveObj;
        l.positions[j + 1] = savePos;
        l.distances[j + 1] = saveDst;
        l.isPin[j + 1]     = saveIsPin;
    }
}

// Squared horizontal distance — Z deliberately ignored. Pillar 4
// announcement is for "where is the thing relative to the player on the
// floor"; vertical separation (e.g. multi-storey rooms) doesn't matter for
// the clock-position + metres TTS. If a future feature needs Z, change here.
float HorizontalDistance(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    // Take the actual sqrt so callers can present metres directly. ~30
    // sqrt calls per scan is below noise.
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

CycleState& GetState(acc::filter::CycleContext ctx) {
    static CycleState sWorld;
    // Map context: only Landmark (Map hint) is engine-cycleable, so the
    // default starts there rather than at the struct-default Door. Without
    // this the first `,`/`.` after opening the map speaks "no doors in
    // range" — Doors are filtered out by IsMapCycleable before lookup.
    static CycleState sMap = []{
        CycleState s;
        s.category = acc::filter::CycleCategory::Landmark;
        return s;
    }();
    return ctx == acc::filter::CycleContext::Map ? sMap : sWorld;
}

bool BuildCategoryListing(acc::filter::CycleCategory category,
                          CategoryListing& out,
                          acc::filter::CycleContext ctx) {
    out.count = 0;

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return false;

    void* area = acc::engine::GetCurrentArea();
    if (!area) {
        acclog::Write("Cycle", "BuildListing area=NULL");
        return false;
    }

    // Map-context guard: bias categories the in-game map doesn't render
    // hard-empty so the cycle-category loop skips them silently. Saves
    // a full area scan + sort per disallowed step on Shift+,/.
    bool mapCtx = (ctx == acc::filter::CycleContext::Map);

    // Discovery tier (the default for the in-world cycle): restrict candidates
    // to objects the player has organically discovered, UNLESS the "Extended
    // cycling" mod setting is on — then the cycle widens back to everything in
    // the area. Map context keeps its own fog-of-war gating and is unaffected.
    bool discoveryFilter =
        !mapCtx &&
        !acc::menus::modsettings::GetToggle(
            acc::menus::modsettings::Option::ExtendedCycling);

    if (mapCtx && !acc::filter::IsMapCycleable(category)) {
        acclog::Write("Cycle",
                      "BuildListing(map) category=%s not-map-cycleable",
                      acc::filter::CategoryName(category));
        return true;
    }

    bool overflowed = false;
    int  scanned = 0;
    int  kindCounts[16] = {0};

    // Map+Landmark sources the authoritative waypoint map-note list with
    // the engine's exact gate (map_note_enabled + IsWorldPointExplored),
    // then folds in the mod's own saved markers below. It deliberately does
    // NOT read CSWCArea.map_pins[] for the engine's notes: those pins mirror
    // the waypoints but carry no fog state of their own (the area-map
    // renderer CSWGuiMapHider::Draw and GetNext/PrevMapNote read the
    // waypoint and apply fog there), so surfacing the pin array leaked
    // unexplored notes. Reading the waypoint matches what a sighted player
    // sees and grows correctly as the player reveals the map.
    bool mapLandmark =
        mapCtx && category == acc::filter::CycleCategory::Landmark;
    void* areaMapForFog = mapLandmark ? acc::engine::GetAreaMap() : nullptr;

    {
        acc::engine::AreaObjectIterator it(area);
        while (void* obj = it.Next()) {
            ++scanned;
            int k = acc::engine::GetObjectKind(obj);
            if (k >= 0 && k < 16) kindCounts[k]++;
            if (!acc::filter::ObjectMatches(obj, category)) continue;

            if (mapLandmark) {
                // has_map_note already checked by ObjectMatches(Landmark);
                // add the engine's two remaining gates.
                if (!acc::engine::IsMapNoteEnabled(obj)) continue;
            } else if (discoveryFilter && !acc::discovery::IsDiscovered(obj)) {
                continue;
            }

            Vector pos;
            if (!acc::engine::GetObjectPosition(obj, pos)) continue;

            // Fog-of-war gate — map context only, applied after position is
            // resolved. Spoiler-correct by construction: never surface a
            // note for a cell the player hasn't revealed.
            if (mapLandmark &&
                !acc::engine::IsWorldPointExplored(areaMapForFog, pos)) {
                continue;
            }

            if (out.count >= CategoryListing::kMaxObjects) {
                overflowed = true;
                continue;
            }
            out.objs[out.count]      = obj;
            out.positions[out.count] = pos;
            out.distances[out.count] = HorizontalDistance(pos, playerPos);
            out.isPin[out.count]     = false;
            ++out.count;
        }
    }

    // Map context + Landmark: fold the mod's own saved markers into the
    // same listing. Markers are identified by IDENTITY (the registry of
    // pins we created), not by a reference-number bit test: engine map-note
    // pins are keyed by the waypoint's client object id, which always has
    // the 0x80000000 high bit, so the old `flags & 0x80000000` test
    // misclassified every engine note pin as a user marker and leaked
    // unexplored notes. Our markers legitimately skip the fog gate — the
    // player placed them, so revealing the spot to themselves isn't a
    // spoiler.
    int pinUser = 0;
    int pinSkippedNonUser = 0;
    int pinSkippedDisabled = 0;
    if (mapLandmark) {
        void* clientArea = acc::engine::GetClientArea(area);
        int   pinCount   = acc::engine::GetMapPinCount(clientArea);
        for (int i = 0; i < pinCount; ++i) {
            void* pin = acc::engine::GetMapPinAt(clientArea, i);
            if (!pin) continue;
            if (!acc::map_user_markers::IsUserMarkerPin(pin)) {
                ++pinSkippedNonUser;
                continue;
            }
            if (!acc::engine::IsMapPinEnabled(pin)) {
                ++pinSkippedDisabled;
                continue;
            }
            Vector pos;
            if (!acc::engine::GetMapPinPosition(pin, pos)) continue;
            if (out.count >= CategoryListing::kMaxObjects) {
                overflowed = true;
                continue;
            }
            out.objs[out.count]      = pin;
            out.positions[out.count] = pos;
            out.distances[out.count] = HorizontalDistance(pos, playerPos);
            out.isPin[out.count]     = true;
            ++out.count;
            ++pinUser;
        }
        if (pinUser > 0 || pinSkippedNonUser > 0) {
            acclog::Write("Cycle",
                          "BuildListing(map) pin-merge user=%d non-user-skip=%d "
                          "disabled-skip=%d",
                          pinUser, pinSkippedNonUser, pinSkippedDisabled);
        }
    }

    SortByDistanceAscending(out);

    // One-shot per category-rebuild diagnostic: dump area + kind histogram
    // when a build returns empty. Helps localise "no objects found" failures
    // (wrong area, wrong iterator offsets, sub-state filter too tight, etc.).
    if (out.count == 0) {
        acclog::Write("Cycle", "BuildListing area=%p ctx=%s category=%s "
                      "scanned=%d "
                      "kinds[Creature=5]=%d [Item=6]=%d [Trigger=7]=%d "
                      "[Placeable=9]=%d [Door=10]=%d [Waypoint=12]=%d",
                      area, mapCtx ? "Map" : "World",
                      acc::filter::CategoryName(category),
                      scanned,
                      kindCounts[5], kindCounts[6], kindCounts[7],
                      kindCounts[9], kindCounts[10], kindCounts[12]);
    }

    if (overflowed) {
        // One-time per build; not throttled because this should be rare in
        // practice (only crowded named scenes hit it). When it does fire,
        // we want to know the exact category that overflowed.
        acclog::Write("Cycle", "%s listing truncated to %d objects (cap)",
                      acc::filter::CategoryName(category),
                      CategoryListing::kMaxObjects);
    }
    return true;
}

void* CycleNextItem(const CategoryListing& listing,
                    acc::filter::CycleContext ctx) {
    auto& s = GetState(ctx);
    if (listing.count == 0) {
        s.focusedIndex = -1;
        s.focusedObj   = nullptr;
        return nullptr;
    }
    int next = s.focusedIndex + 1;
    if (next >= listing.count) next = listing.count - 1;  // clamp
    if (next < 0) next = 0;
    s.focusedIndex = next;
    s.focusedObj   = listing.objs[next];
    return s.focusedObj;
}

void* CyclePrevItem(const CategoryListing& listing,
                    acc::filter::CycleContext ctx) {
    auto& s = GetState(ctx);
    if (listing.count == 0) {
        s.focusedIndex = -1;
        s.focusedObj   = nullptr;
        return nullptr;
    }
    int prev = s.focusedIndex - 1;
    if (prev < 0) prev = 0;  // clamp
    s.focusedIndex = prev;
    s.focusedObj   = listing.objs[prev];
    return s.focusedObj;
}

void* CycleFirstItem(const CategoryListing& listing,
                     acc::filter::CycleContext ctx) {
    auto& s = GetState(ctx);
    if (listing.count == 0) {
        s.focusedIndex = -1;
        s.focusedObj   = nullptr;
        return nullptr;
    }
    s.focusedIndex = 0;
    s.focusedObj   = listing.objs[0];
    return s.focusedObj;
}

void* CycleLastItem(const CategoryListing& listing,
                    acc::filter::CycleContext ctx) {
    auto& s = GetState(ctx);
    if (listing.count == 0) {
        s.focusedIndex = -1;
        s.focusedObj   = nullptr;
        return nullptr;
    }
    int last = listing.count - 1;
    s.focusedIndex = last;
    s.focusedObj   = listing.objs[last];
    return s.focusedObj;
}

namespace {

bool CycleCategoryDirectional(CategoryListing& outListing,
                              bool forward,
                              acc::filter::CycleContext ctx) {
    auto& s = GetState(ctx);
    auto start = s.category;
    int n = int(acc::filter::CycleCategory::Count_);
    for (int tries = 0; tries < n; ++tries) {
        s.category = forward ? acc::filter::NextCategory(s.category)
                             : acc::filter::PrevCategory(s.category);
        if (BuildCategoryListing(s.category, outListing, ctx) &&
            outListing.count > 0) {
            s.focusedIndex = 0;
            s.focusedObj   = outListing.objs[0];
            return true;
        }
    }
    s.category     = start;
    s.focusedIndex = -1;
    s.focusedObj   = nullptr;
    outListing.count = 0;
    return false;
}

}  // namespace

bool CycleNextCategory(CategoryListing& outListing,
                       acc::filter::CycleContext ctx) {
    return CycleCategoryDirectional(outListing, /*forward=*/true, ctx);
}

bool CyclePrevCategory(CategoryListing& outListing,
                       acc::filter::CycleContext ctx) {
    return CycleCategoryDirectional(outListing, /*forward=*/false, ctx);
}

bool RefreshCurrentListing(CategoryListing& outListing,
                           acc::filter::CycleContext ctx) {
    auto& s = GetState(ctx);
    if (!BuildCategoryListing(s.category, outListing, ctx)) return false;

    if (outListing.count == 0) {
        s.focusedIndex = -1;
        s.focusedObj   = nullptr;
        return true;
    }

    // Try to keep the previously-focused object in focus across the rebuild.
    if (s.focusedObj) {
        for (int i = 0; i < outListing.count; ++i) {
            if (outListing.objs[i] == s.focusedObj) {
                s.focusedIndex = i;
                return true;
            }
        }
    }

    // Fall through: previous focus gone (object removed / out of scan
    // scope) — reset to closest.
    s.focusedIndex = 0;
    s.focusedObj   = outListing.objs[0];
    return true;
}

}  // namespace acc::cycle
