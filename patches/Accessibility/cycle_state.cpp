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

CycleState& GetState() {
    static CycleState s;
    return s;
}

bool BuildCategoryListing(acc::filter::CycleCategory category,
                          CategoryListing& out) {
    out.count = 0;

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return false;

    void* area = acc::engine::GetCurrentArea();
    if (!area) return false;

    bool overflowed = false;
    acc::engine::AreaObjectIterator it(area);
    while (void* obj = it.Next()) {
        if (!acc::filter::ObjectMatches(obj, category)) continue;

        Vector pos;
        if (!acc::engine::GetObjectPosition(obj, pos)) continue;

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

    if (overflowed) {
        // One-time per build; not throttled because this should be rare in
        // practice (only crowded named scenes hit it). When it does fire,
        // we want to know the exact category that overflowed.
        acclog::Write("Cycle: %s listing truncated to %d objects (cap)",
                      acc::filter::CategoryName(category),
                      CategoryListing::kMaxObjects);
    }
    return true;
}

void* CycleNextItem(const CategoryListing& listing) {
    auto& s = GetState();
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

void* CyclePrevItem(const CategoryListing& listing) {
    auto& s = GetState();
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

bool CycleCategoryDirectional(CategoryListing& outListing, bool forward) {
    auto& s = GetState();
    auto start = s.category;
    int n = int(acc::filter::CycleCategory::Count_);
    for (int tries = 0; tries < n; ++tries) {
        s.category = forward ? acc::filter::NextCategory(s.category)
                             : acc::filter::PrevCategory(s.category);
        if (BuildCategoryListing(s.category, outListing) &&
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

bool CycleNextCategory(CategoryListing& outListing) {
    return CycleCategoryDirectional(outListing, /*forward=*/true);
}

bool CyclePrevCategory(CategoryListing& outListing) {
    return CycleCategoryDirectional(outListing, /*forward=*/false);
}

bool RefreshCurrentListing(CategoryListing& outListing) {
    auto& s = GetState();
    if (!BuildCategoryListing(s.category, outListing)) return false;

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
