#include "guidance_description.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine_compass.h"
#include "log.h"
#include "strings.h"
#include "prism.h"

namespace acc::guidance::description {

namespace {

constexpr float kMinSegmentMeters = 1.0f;  // hops shorter than this are
                                           // merged into the next segment
constexpr float kRadToDeg         = 57.29577951308232f;

struct Segment {
    int sector;     // 0..7 (engine_compass convention)
    float dist;     // metres
};

// Compute the 8-point compass sector of a displacement vector. The
// game-world frame matches the engine yaw frame: +X = East, +Y =
// North in compass convention. atan2(dy, dx) gives engine yaw of the
// displacement; EngineYawToCompass + CompassToSector buckets to 0..7.
int CompassSectorOf(float dx, float dy) {
    float engineYaw = std::atan2(dy, dx) * kRadToDeg;
    float compass   = acc::engine::EngineYawToCompass(engineYaw);
    return acc::engine::CompassToSector(compass);
}

// Build the merged segment list from a (player + waypoints) iteration.
// Each call to AddHop folds the hop into the existing tail when the
// sector matches, otherwise appends a new segment. Sub-threshold hops
// (<kMinSegmentMeters) accumulate into a pending distance which is
// either merged when the next hop matches direction or, on a direction
// change, attached to the new segment as an extra metre or two so the
// readout's total still adds up.
class SegmentBuilder {
public:
    void AddHop(float dx, float dy) {
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= 0.0f) return;

        // Very short hop — accumulate into the pending bucket and bail.
        // Direction is unreliable below ~0.5m so we don't even classify.
        if (dist < kMinSegmentMeters) {
            pending_ += dist;
            return;
        }

        int sector = CompassSectorOf(dx, dy);

        // If we have a tail with the same sector, merge.
        if (!segments_.empty() && segments_.back().sector == sector) {
            segments_.back().dist += dist + pending_;
            pending_ = 0.0f;
            return;
        }

        // Otherwise emit a new segment, carrying the pending fraction
        // forward (it walked SOMEWHERE; attach to the new direction
        // since we can't speak it independently).
        Segment seg;
        seg.sector = sector;
        seg.dist   = dist + pending_;
        segments_.push_back(seg);
        pending_ = 0.0f;
    }

    const std::vector<Segment>& Segments() const { return segments_; }

    float Pending() const { return pending_; }

private:
    std::vector<Segment> segments_;
    float                pending_ = 0.0f;
};

// Format one segment as "{rounded metres} Meter {DirectionWord}". The
// outBuf must have room; returns bytes written excluding NUL.
int FormatSegment(const Segment& seg, char* outBuf, size_t outBufSize) {
    int metres = static_cast<int>(seg.dist + 0.5f);
    if (metres < 1) metres = 1;
    const char* dir = acc::strings::Get(acc::engine::SectorString(seg.sector));
    int n = std::snprintf(outBuf, outBufSize,
                          acc::strings::Get(
                              acc::strings::Id::FmtRouteSegment),
                          metres, dir);
    return n < 0 ? 0 : n;
}

// Sum the rounded segment distances. Used for the route-header total.
int TotalMetres(const std::vector<Segment>& segs, float pending) {
    float sum = pending;
    for (const auto& s : segs) sum += s.dist;
    int m = static_cast<int>(sum + 0.5f);
    if (m < 1) m = 1;
    return m;
}

}  // namespace

bool Speak(const Vector& playerPos,
           const std::vector<Vector>& waypoints,
           const char* targetName,
           bool isTransition,
           bool interrupt) {
    const char* nameOrFallback = (targetName && targetName[0]) ? targetName
                                                              : "?";

    if (waypoints.empty()) {
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(
                          acc::strings::Id::FmtBeaconNoPath),
                      nameOrFallback);
        prism::Speak(msg, interrupt);
        acclog::Write("Description", "no path -> [%s]", msg);
        return false;
    }

    SegmentBuilder builder;

    // First hop — player position → first waypoint. Often short (player
    // is close to the nearest nav-graph node); SegmentBuilder folds it
    // into the next hop if it's sub-threshold.
    builder.AddHop(waypoints[0].x - playerPos.x,
                   waypoints[0].y - playerPos.y);

    // Subsequent hops — waypoint i → waypoint i+1.
    for (size_t i = 0; i + 1 < waypoints.size(); ++i) {
        builder.AddHop(waypoints[i + 1].x - waypoints[i].x,
                       waypoints[i + 1].y - waypoints[i].y);
    }

    const auto& segs = builder.Segments();

    // Already-at-destination case — the whole path landed inside the
    // first segment's sub-threshold pending bucket. Speak the localised
    // "already there" phrase.
    if (segs.empty()) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::BeaconAlreadyAtDest);
        prism::Speak(msg, interrupt);
        acclog::Write("Description", "all-sub-threshold -> [%s]", msg);
        return true;
    }

    // Build the per-segment joined list: "5 Meter Norden, 4 Meter Nord-
    // Ost, 6 Meter Osten". Separator and word choices live in
    // strings.h so localisation can swap them without touching this
    // code.
    const char* sep = acc::strings::Get(
        acc::strings::Id::RouteJoinSeparator);

    char joined[1024] = "";
    size_t pos = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (pos >= sizeof(joined) - 1) break;
        if (i > 0) {
            int w = std::snprintf(joined + pos, sizeof(joined) - pos,
                                  "%s", sep);
            if (w < 0) break;
            pos += static_cast<size_t>(w);
            if (pos >= sizeof(joined) - 1) break;
        }
        char seg[64];
        FormatSegment(segs[i], seg, sizeof(seg));
        int w = std::snprintf(joined + pos, sizeof(joined) - pos, "%s", seg);
        if (w < 0) break;
        pos += static_cast<size_t>(w);
    }

    const char* transNote = acc::strings::Get(
        isTransition ? acc::strings::Id::RouteOneTransition
                     : acc::strings::Id::RouteNoTransition);

    int total = TotalMetres(segs, builder.Pending());

    char msg[1280];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtRouteHeader),
                  nameOrFallback, total, joined, transNote);
    prism::Speak(msg, interrupt);
    acclog::Write("Description", "segs=%zu total=%dm transition=%d -> [%s]",
                  segs.size(), total, isTransition ? 1 : 0, msg);
    return true;
}

}  // namespace acc::guidance::description
