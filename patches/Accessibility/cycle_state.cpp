#include "cycle_state.h"

#include <cmath>
#include <cstdint>

#include "engine_area.h"
#include "engine_player.h"
#include "log.h"

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
        int j = i - 1;
        while (j >= 0 && l.distances[j] > saveDst) {
            l.objs[j + 1]      = l.objs[j];
            l.positions[j + 1] = l.positions[j];
            l.distances[j + 1] = l.distances[j];
            --j;
        }
        l.objs[j + 1]      = saveObj;
        l.positions[j + 1] = savePos;
        l.distances[j + 1] = saveDst;
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
    static CycleState sMap;
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
    if (mapCtx && !acc::filter::IsMapCycleable(category)) {
        acclog::Write("Cycle",
                      "BuildListing(map) category=%s not-map-cycleable",
                      acc::filter::CategoryName(category));
        return true;
    }

    // Resolve the per-area map struct once per build for the fog gate.
    // Read-only over engine state; if the lookup faults we degrade to
    // "no fog gate" rather than hiding everything.
    void* areaMap = mapCtx ? acc::engine::GetAreaMap() : nullptr;
    int   fogFiltered = 0;

    // MapPin lives on the client-area's dynamic pin array, NOT in
    // CSWSArea.game_objects[]. Out-of-band iteration in map context;
    // World context skips this category entirely (returns empty so the
    // category-loop's silent-skip kicks in).
    if (category == acc::filter::CycleCategory::MapPin) {
        if (!mapCtx) {
            // In-world Shift+,/. should not surface map pins (they're
            // a map-UI affordance only). Return success with empty.
            return true;
        }
        void* clientArea = acc::engine::GetClientArea(area);
        int   pinCount    = acc::engine::GetMapPinCount(clientArea);
        int   pinFog      = 0;
        int   pinDisabled = 0;
        int   pinUser     = 0;  // count of user-placed markers (refnum >= 0x80000000)
        bool  pinOverflow = false;
        for (int i = 0; i < pinCount; ++i) {
            void* pin = acc::engine::GetMapPinAt(clientArea, i);
            if (!pin) continue;
            if (!acc::engine::IsMapPinEnabled(pin)) {
                ++pinDisabled;
                continue;
            }
            Vector pos;
            if (!acc::engine::GetMapPinPosition(pin, pos)) continue;
            // Skip fog gate for user-placed markers: the player
            // explicitly dropped them, so there's no spoiler concern,
            // and gating them out makes the dropped pin un-refindable
            // when the cursor landed in an unrevealed cell at drop time
            // (observed live 2026-05-18 — see logs/patch-...-193939.log
            // line 2525 vs line 3684 in the diagnosis trail).
            uint32_t flags = acc::engine::GetMapPinFlags(pin);
            bool isUserPin = (flags & 0x80000000u) != 0;
            if (isUserPin) ++pinUser;
            if (!isUserPin &&
                areaMap && !acc::engine::IsWorldPointExplored(areaMap, pos)) {
                ++pinFog;
                continue;
            }
            if (out.count >= CategoryListing::kMaxObjects) {
                pinOverflow = true;
                continue;
            }
            out.objs[out.count]      = pin;
            out.positions[out.count] = pos;
            out.distances[out.count] = HorizontalDistance(pos, playerPos);
            ++out.count;
        }
        SortByDistanceAscending(out);
        acclog::Write("Cycle",
                      "BuildListing(map) MapPin clientArea=%p pinCount=%d "
                      "disabled=%d fog=%d user=%d kept=%d overflow=%d",
                      clientArea, pinCount, pinDisabled, pinFog,
                      pinUser, out.count, (int)pinOverflow);
        return true;
    }

    bool overflowed = false;
    int  mapNoteDisabledFiltered = 0;
    acc::engine::AreaObjectIterator it(area);
    int scanned = 0;
    int kindCounts[16] = {0};
    while (void* obj = it.Next()) {
        ++scanned;
        int k = acc::engine::GetObjectKind(obj);
        if (k >= 0 && k < 16) kindCounts[k]++;
        if (!acc::filter::ObjectMatches(obj, category)) continue;

        Vector pos;
        if (!acc::engine::GetObjectPosition(obj, pos)) continue;

        // Map-context fog-of-war gate. Spoiler-correct by construction:
        // a landmark in an unexplored cell stays out of the cycle until
        // the player walks within map-reveal range.
        if (mapCtx && areaMap &&
            !acc::engine::IsWorldPointExplored(areaMap, pos)) {
            ++fogFiltered;
            continue;
        }

        // Map-context "map hint" curation. CSWGuiMapHider::Draw only
        // renders waypoints whose map_note_enabled flag (+0x22c) is set
        // — quest scripts toggle this dynamically so the icon turns up
        // when relevant. Match the engine's curated subset on the map
        // cycle so blind players hear the same set sighted players see
        // (and same set the up/down "Hinweis" buttons cycle).
        if (mapCtx &&
            category == acc::filter::CycleCategory::Landmark &&
            !acc::engine::IsMapNoteEnabled(obj)) {
            ++mapNoteDisabledFiltered;
            continue;
        }

        if (out.count >= CategoryListing::kMaxObjects) {
            overflowed = true;
            continue;
        }
        out.objs[out.count]      = obj;
        out.positions[out.count] = pos;
        out.distances[out.count] = HorizontalDistance(pos, playerPos);
        ++out.count;
    }

    SortByDistanceAscending(out);

    // One-shot per category-rebuild diagnostic: dump area + kind histogram
    // when a build returns empty. Helps localise "no objects found" failures
    // (wrong area, wrong iterator offsets, sub-state filter too tight, etc.).
    if (out.count == 0) {
        acclog::Write("Cycle", "BuildListing area=%p ctx=%s category=%s "
                      "snapshotSize=%d scanned=%d fogFiltered=%d "
                      "mapNoteDisabled=%d "
                      "kinds[Creature=5]=%d [Item=6]=%d [Trigger=7]=%d "
                      "[Placeable=9]=%d [Door=10]=%d [Waypoint=12]=%d",
                      area, mapCtx ? "Map" : "World",
                      acc::filter::CategoryName(category),
                      it.SnapshotSize(), scanned, fogFiltered,
                      mapNoteDisabledFiltered,
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
