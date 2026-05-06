#include "spatial_change_detector.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "audio_cue_player.h"
#include "audio_cues.h"           // NavCue
#include "core_settings.h"
#include "engine_area.h"
#include "engine_offsets.h"       // Vector
#include "engine_player.h"        // GetPlayerPosition / GetPlayerYawDegrees
#include "view_mode.h"            // GetEffectiveOrientationYawDegrees —
                                  // routes T2 cone through camera yaw
                                  // when view mode is active
#include "filter_objects.h"
#include "log.h"

namespace acc::spatial::change_detector {

namespace {

// --- Wall cache ---------------------------------------------------------
//
// Sized at 4096 — Endar Spire areas have logged 405–908 perimeter edges
// (lay-off 1 + initial Trigger 1 verification). Open-environment areas
// (Manaan, Korriban) plausibly run higher; 4096 leaves comfortable
// headroom. Overflow is logged once per area-change so we can size up
// if a real area exceeds it.

constexpr int kMaxWallEdges = 4096;
acc::engine::WallEdge g_walls[kMaxWallEdges];
int                   g_wall_count = 0;

// Per-wall last-cued distance. Negative sentinel = "wall is currently
// out of range" (or "we haven't observed it in range yet this area").
// On area change CalibrateInRange seeds this for every in-range wall
// without firing cues, so the next normal tick fires only on actual
// player motion past the threshold.
float g_wall_last_distance[kMaxWallEdges];

// Per-wall last-cued timestamp (ms via GetTickCount). Shared between T1
// and T2: T1 stamps when it fires a wall; T2 reads to gate its own fire
// (and stamps too). 0 = never cued. On area change all entries reset to
// 0 so the first T2 candidate post-load is allowed to fire after the
// T2 first-tick suppression is cleared.
DWORD g_wall_last_cued_at[kMaxWallEdges];

// --- Object state -------------------------------------------------------
//
// In-range Pillar 1 vocabulary objects (Door / Npc / Container / Item /
// Landmark / Transition). Linear-probed table keyed by engine handle.
// Sized at 256 — typical per-area object counts are in the dozens; the
// in-range subset at 5m is much smaller. Empty slot = handle == 0.

struct ObjectState {
    uint32_t handle;
    float    last_distance;
    DWORD    last_cued_at;   // 0 = never cued (shared T1/T2)
};

constexpr int kMaxTrackedObjects = 256;
ObjectState g_object_state[kMaxTrackedObjects];

// --- Trigger 2 — foremost-in-front debounce state ----------------------
//
// Three-variable pattern ported from turn_announce.cpp. The "feature
// identity" can be a wall (by index into g_walls) or an object (by
// engine handle) or None (cone clear). Equality compares (kind +
// discriminator) so a wall and an object with happenstance-matching
// integers are distinguishable.

constexpr DWORD kT2QuietMs = 250;

enum class FeatureKind : int {
    None   = 0,
    Wall   = 1,
    Object = 2,
};

struct Foremost {
    FeatureKind kind;
    int         wall_index;
    uint32_t    object_handle;
};

bool ForemostEqual(const Foremost& a, const Foremost& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case FeatureKind::Wall:   return a.wall_index    == b.wall_index;
        case FeatureKind::Object: return a.object_handle == b.object_handle;
        case FeatureKind::None:   return true;
    }
    return false;
}

const char* ForemostKindTag(FeatureKind k) {
    switch (k) {
        case FeatureKind::None:   return "none";
        case FeatureKind::Wall:   return "wall";
        case FeatureKind::Object: return "obj";
    }
    return "?";
}

Foremost g_t2_last_fired      = { FeatureKind::None, -1, 0 };
Foremost g_t2_pending         = { FeatureKind::None, -1, 0 };
DWORD    g_t2_pending_changed_at = 0;
bool     g_t2_initialised     = false;

// --- Geometry -----------------------------------------------------------

// Closest-point-on-segment squared distance. Writes the closest point
// on the [a,b] segment to outClosest. Degenerate segment (a == b) treats
// the segment as the single point a.
float ClosestPointDistanceSquared(const Vector& p,
                                  const Vector& a,
                                  const Vector& b,
                                  Vector& outClosest) {
    Vector ab = { b.x - a.x, b.y - a.y, b.z - a.z };
    Vector ap = { p.x - a.x, p.y - a.y, p.z - a.z };
    float ab_len_sq = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    if (ab_len_sq < 1e-6f) {
        outClosest = a;
        return ap.x * ap.x + ap.y * ap.y + ap.z * ap.z;
    }
    float t = (ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / ab_len_sq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    outClosest = { a.x + t * ab.x, a.y + t * ab.y, a.z + t * ab.z };
    Vector diff = {
        p.x - outClosest.x, p.y - outClosest.y, p.z - outClosest.z };
    return diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
}

// --- Object classification ---------------------------------------------

acc::audio::NavCue CategoryToNavCue(acc::filter::CycleCategory c) {
    using F = acc::filter::CycleCategory;
    using N = acc::audio::NavCue;
    switch (c) {
        case F::Door:        return N::Door;
        case F::Npc:         return N::NpcCreature;
        case F::Container:   return N::ContainerPlaceable;
        case F::Item:        return N::Item;
        case F::Landmark:    return N::Landmark;
        case F::Transition:  return N::TransitionExit;
        default:             return N::Wall;  // unreachable
    }
}

bool ClassifyObject(void* obj, acc::audio::NavCue& outCue) {
    using F = acc::filter::CycleCategory;
    for (int c = 0; c < static_cast<int>(F::Count_); ++c) {
        if (acc::filter::ObjectMatches(obj, static_cast<F>(c))) {
            outCue = CategoryToNavCue(static_cast<F>(c));
            return true;
        }
    }
    return false;
}

// --- Object state table ------------------------------------------------

ObjectState* FindOrAddObjectState(uint32_t handle, bool& outIsNew) {
    outIsNew = false;
    if (handle == 0) return nullptr;
    for (auto& s : g_object_state) {
        if (s.handle == handle) return &s;
    }
    for (auto& s : g_object_state) {
        if (s.handle == 0) {
            s.handle        = handle;
            s.last_distance = 0.0f;
            s.last_cued_at  = 0;
            outIsNew        = true;
            return &s;
        }
    }
    return nullptr;  // table full
}

ObjectState* FindObjectState(uint32_t handle) {
    if (handle == 0) return nullptr;
    for (auto& s : g_object_state) {
        if (s.handle == handle) return &s;
    }
    return nullptr;
}

void RemoveObjectState(uint32_t handle) {
    if (handle == 0) return;
    for (auto& s : g_object_state) {
        if (s.handle == handle) {
            s.handle        = 0;
            s.last_distance = 0.0f;
            s.last_cued_at  = 0;
            return;
        }
    }
}

// --- K-nearest candidate pool ------------------------------------------
//
// Per-tick scratch buffer for "walls that crossed threshold this tick".
// Sized at 256 — peak observed in-range count was 65 (line 19:56:16 of
// patch-20260505-195454.log); 4× headroom for hypothetical dense areas.
// Overflow drops the extra candidates from selection but their
// last_distance still gets updated below so they don't re-queue next
// tick.

struct WallCandidate {
    int    wall_index;
    float  distance;
    Vector closest_point;
};

constexpr int kMaxWallCandidates = 256;
WallCandidate g_candidates[kMaxWallCandidates];

// --- Direction sectors for "one cue per direction" selection -----------
//
// The plan locks Trigger 1 as 360° awareness, but the user-visible cue
// budget is K=3 per tick (settings: trigger1MaxWallCuesPerTick). With
// raw "K closest by distance" selection in dense areas, all K picks
// can land on fragments of the *same* physical wall (the closest one),
// leaving the user with three near-identical pans on one ear.
//
// We instead bin candidates into four cardinal sectors *relative to
// player heading* — front, left, back, right — and pick the closest
// candidate in each sector. Each sector contributes at most one cue,
// so a corridor wall fragmented into N pieces on the right collapses
// to one "right" cue regardless of N. The K-cap then picks the K
// closest among the per-sector reps; in a 4-walls-around-you room
// the furthest sector (typically "back") is the one silenced first.
//
// Sectors are defined relative to player heading:
//   Front: rel bearing in [-45°, +45°)  i.e. [315°, 360°) ∪ [0°, 45°)
//   Left : rel bearing in [+45°, +135°)
//   Back : rel bearing in [+135°, +225°)
//   Right: rel bearing in [+225°, +315°)
//
// Bearing convention follows engine_player::GetPlayerYawDegrees: world
// frame, 0° = +X = east, CCW positive. Relative bearing = world bearing
// from player to candidate, minus player yaw, normalised to [0, 360).
// Left = +90° (CCW) because both yaw and atan2 are CCW-positive.
//
// Trigger 2's ±45° front cone equals the Front sector exactly — both
// triggers share this classifier so the same in-front candidate set
// drives T2 firing decisions.
enum class WallSector : int {
    Front = 0,
    Left  = 1,
    Back  = 2,
    Right = 3,
    Count_ = 4,
};

WallSector ClassifyRelativeBearing(float relBearingDeg) {
    while (relBearingDeg <    0.0f) relBearingDeg += 360.0f;
    while (relBearingDeg >= 360.0f) relBearingDeg -= 360.0f;
    if (relBearingDeg <  45.0f) return WallSector::Front;
    if (relBearingDeg < 135.0f) return WallSector::Left;
    if (relBearingDeg < 225.0f) return WallSector::Back;
    if (relBearingDeg < 315.0f) return WallSector::Right;
    return WallSector::Front;
}

const char* SectorTag(WallSector s) {
    switch (s) {
        case WallSector::Front: return "F";
        case WallSector::Left:  return "L";
        case WallSector::Back:  return "B";
        case WallSector::Right: return "R";
        default:                return "?";
    }
}

// --- Area tracking ------------------------------------------------------

void* g_prev_area = nullptr;

void OnAreaChange(void* area) {
    if (!area) {
        g_wall_count = 0;
        return;
    }
    g_wall_count = acc::engine::BuildAreaWallCache(area, g_walls, kMaxWallEdges);
    int totalDiscovered = acc::engine::BuildAreaWallCache(area, nullptr, 0);
    bool overflow = (totalDiscovered > kMaxWallEdges);

    for (int i = 0; i < kMaxWallEdges; ++i) {
        g_wall_last_distance[i] = -1.0f;
        g_wall_last_cued_at[i]  = 0;
    }
    for (int i = 0; i < kMaxTrackedObjects; ++i) {
        g_object_state[i].handle        = 0;
        g_object_state[i].last_distance = 0.0f;
        g_object_state[i].last_cued_at  = 0;
    }

    // T2 first-tick suppression: clear identity state. The first
    // post-CalibrateInRange tick will seed g_t2_last_fired without
    // firing.
    g_t2_last_fired         = { FeatureKind::None, -1, 0 };
    g_t2_pending            = { FeatureKind::None, -1, 0 };
    g_t2_pending_changed_at = 0;
    g_t2_initialised        = false;

    if (overflow) {
        acclog::Write(
            "ChangeDetector: area change — walls cached=%d/%d (OVERFLOW; "
            "increase kMaxWallEdges) areaPtr=%p",
            g_wall_count, totalDiscovered, area);
    } else {
        acclog::Write(
            "ChangeDetector: area change — walls cached=%d areaPtr=%p",
            g_wall_count, area);
    }
}

// Calibration scan — runs once per area-change after OnAreaChange has
// rebuilt the cache. Fills `last_distance` for every in-range wall and
// seeds an object-state entry for every in-range Pillar 1 vocabulary
// object, WITHOUT firing any cues. Eliminates the wall-of-sound the
// user experienced on every save-load (when 23+ in-range walls all
// fired their first-observation entry cue on the same frame).
//
// After calibration, the next normal Tick() fires only on actual
// player motion past the threshold (or on features that newly enter
// the awareness bubble during play).
void CalibrateInRange(void* area, const Vector& playerPos) {
    if (!area) return;
    const auto& settings = acc::core::Get().pillar1;
    float range   = settings.awarenessRangeMeters;
    float rangeSq = range * range;

    int wallsCalibrated = 0;
    for (int i = 0; i < g_wall_count; ++i) {
        const auto& w = g_walls[i];
        Vector closest;
        float distSq = ClosestPointDistanceSquared(
            playerPos, w.a, w.b, closest);
        if (distSq > rangeSq) continue;
        g_wall_last_distance[i] = std::sqrt(distSq);
        ++wallsCalibrated;
    }

    int objectsCalibrated = 0;
    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    while ((obj = iter.Next()) != nullptr) {
        acc::audio::NavCue cue;
        if (!ClassifyObject(obj, cue)) continue;
        Vector pos;
        if (!acc::engine::GetObjectPosition(obj, pos)) continue;
        float dx = pos.x - playerPos.x;
        float dy = pos.y - playerPos.y;
        float dz = pos.z - playerPos.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > rangeSq) continue;
        uint32_t handle = acc::engine::GetObjectHandle(obj);
        if (handle == 0) continue;
        bool isNew = false;
        ObjectState* s = FindOrAddObjectState(handle, isNew);
        if (!s) continue;
        s->last_distance = std::sqrt(distSq);
        ++objectsCalibrated;
    }

    acclog::Write(
        "ChangeDetector: calibrated walls=%d objects=%d at "
        "playerPos=(%.2f,%.2f,%.2f)",
        wallsCalibrated, objectsCalibrated,
        playerPos.x, playerPos.y, playerPos.z);
}

}  // namespace

void Tick() {
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        // Player gone (menu / chargen / pre-spawn). Drop area tracking
        // so re-load triggers a fresh OnAreaChange.
        g_prev_area = nullptr;
        return;
    }

    void* area = acc::engine::GetCurrentArea();
    if (!area) return;

    if (area != g_prev_area) {
        OnAreaChange(area);
        CalibrateInRange(area, playerPos);
        g_prev_area = area;
        // Calibration completed silently — last_distance now reflects
        // the as-loaded position for every in-range feature, so the
        // first cue we fire on the next tick will be a real motion
        // event, not the area-load entry flood.
        return;
    }

    const auto& settings = acc::core::Get().pillar1;
    if (!settings.trigger1DistanceDelta && !settings.trigger2FrontCone) return;

    float range     = settings.awarenessRangeMeters;
    float threshold = settings.distanceDeltaThresholdMeters;
    float rangeSq   = range * range;
    int   maxCues   = settings.trigger1MaxWallCuesPerTick;
    if (maxCues < 0) maxCues = 0;

    DWORD now = GetTickCount();

    // Hoisted yaw — used for both T1 wall-sector binning and T2 Front-cone
    // candidate detection. One read per tick.
    //
    // Camera yaw when view mode is active (T2 tracks where the user is
    // looking as they pan A/D); player yaw otherwise. T1 distance-delta
    // doesn't fire in view mode anyway (character not moving), so its
    // sector binning riding the same value is fine.
    float effectiveYaw = 0.0f;
    if (!acc::view_mode::GetEffectiveOrientationYawDegrees(effectiveYaw)) {
        effectiveYaw = 0.0f;
    }

    int walls_in_range    = 0;
    int wall_candidates   = 0;
    int wall_overflow     = 0;
    int sectors_active    = 0;
    int walls_cued        = 0;
    int objs_in_range     = 0;
    int objs_cued         = 0;
    char sector_log[8]    = {0};   // "F-L-B-R" mask string for diagnostic

    // T2 foremost-in-front candidate (best across walls + objects).
    // Tracked inline during the T1 passes — every in-range feature is
    // a T2 candidate, regardless of whether its distance crossed the
    // T1 threshold this tick.
    Foremost           t2_best        = { FeatureKind::None, -1, 0 };
    float              t2_best_dist   = 1e30f;
    Vector             t2_best_pos    = { 0.0f, 0.0f, 0.0f };
    acc::audio::NavCue t2_best_cue    = acc::audio::NavCue::Wall;
    const bool         t2_enabled     = settings.trigger2FrontCone;

    // --- Walls: pass 1 — collect candidates, update references ---------
    //
    // Update last_distance immediately for every threshold-crossing wall,
    // even if the candidate buffer is full or the wall later loses the
    // K-nearest cut. Otherwise unfired walls would keep crossing the
    // same threshold every tick and starve the K-selection forever.
    for (int i = 0; i < g_wall_count; ++i) {
        const auto& w = g_walls[i];
        Vector closest;
        float distSq = ClosestPointDistanceSquared(
            playerPos, w.a, w.b, closest);
        if (distSq > rangeSq) {
            g_wall_last_distance[i] = -1.0f;
            continue;
        }
        ++walls_in_range;
        float dist = std::sqrt(distSq);

        // T2 candidate detection — every in-range wall whose closest
        // point lies in the Front sector competes for foremost.
        if (t2_enabled) {
            float dx = closest.x - playerPos.x;
            float dy = closest.y - playerPos.y;
            float wb = std::atan2(dy, dx) * 57.29577951308232f;
            if (ClassifyRelativeBearing(wb - effectiveYaw) ==
                    WallSector::Front &&
                dist < t2_best_dist) {
                t2_best_dist          = dist;
                t2_best.kind          = FeatureKind::Wall;
                t2_best.wall_index    = i;
                t2_best.object_handle = 0;
                t2_best_pos           = closest;
                t2_best_cue           = acc::audio::NavCue::Wall;
            }
        }

        if (!settings.trigger1DistanceDelta) continue;

        bool crossed = false;
        if (g_wall_last_distance[i] < 0.0f) {
            crossed = true;  // first observation in range during play
        } else if (std::fabs(dist - g_wall_last_distance[i]) > threshold) {
            crossed = true;
        }
        if (!crossed) continue;

        // Record the new reference distance now — applies to all
        // candidates regardless of fate (kept, deduped, K-cut, overflow).
        g_wall_last_distance[i] = dist;

        if (wall_candidates < kMaxWallCandidates) {
            g_candidates[wall_candidates].wall_index    = i;
            g_candidates[wall_candidates].distance      = dist;
            g_candidates[wall_candidates].closest_point = closest;
            ++wall_candidates;
        } else {
            ++wall_overflow;
        }
    }

    // --- Walls: pass 2 — sector-binning + fire K closest sector reps ---
    //
    // Bin each candidate into one of four character-relative sectors
    // (Front/Left/Back/Right). Each sector contributes at most one
    // cue — the closest candidate in that sector. After binning, sort
    // the per-sector reps by distance and fire the K closest. Same-wall
    // fragments collapse automatically (they all share a sector); a
    // T-junction with walls in three sectors fires three cues panned
    // to three different directions.
    if (wall_candidates > 0) {
        int sectorWinner[static_cast<int>(WallSector::Count_)] = {-1,-1,-1,-1};
        for (int i = 0; i < wall_candidates; ++i) {
            const auto& c = g_candidates[i];
            float dx = c.closest_point.x - playerPos.x;
            float dy = c.closest_point.y - playerPos.y;
            // World-frame bearing (atan2(dy,dx) — same convention as
            // GetPlayerYawDegrees: 0° = +X, CCW positive).
            float worldBearingDeg = std::atan2(dy, dx) * 57.29577951308232f;
            float relBearing = worldBearingDeg - effectiveYaw;
            int s = static_cast<int>(ClassifyRelativeBearing(relBearing));
            if (sectorWinner[s] < 0 ||
                g_candidates[i].distance <
                    g_candidates[sectorWinner[s]].distance) {
                sectorWinner[s] = i;
            }
        }

        // Pack winners into a small array and sort ascending by distance.
        int winners[static_cast<int>(WallSector::Count_)];
        int winnerCount = 0;
        for (int s = 0; s < static_cast<int>(WallSector::Count_); ++s) {
            if (sectorWinner[s] >= 0) winners[winnerCount++] = sectorWinner[s];
        }
        sectors_active = winnerCount;
        for (int i = 0; i + 1 < winnerCount; ++i) {
            int minIdx = i;
            for (int j = i + 1; j < winnerCount; ++j) {
                if (g_candidates[winners[j]].distance <
                    g_candidates[winners[minIdx]].distance) {
                    minIdx = j;
                }
            }
            if (minIdx != i) {
                int tmp = winners[i];
                winners[i] = winners[minIdx];
                winners[minIdx] = tmp;
            }
        }

        // Build a "F-L-B-R" diagnostic mask string in fired-order so the
        // log shows which sectors fired this tick (e.g. "RLF" = right
        // closest, left next, front third).
        int firedCount = winnerCount < maxCues ? winnerCount : maxCues;
        int logIdx = 0;
        for (int k = 0; k < firedCount; ++k) {
            const auto& c = g_candidates[winners[k]];
            // Recompute sector for the fired winner — cheap, no need
            // to thread it through the winners array.
            float dx = c.closest_point.x - playerPos.x;
            float dy = c.closest_point.y - playerPos.y;
            float wb = std::atan2(dy, dx) * 57.29577951308232f;
            WallSector s = ClassifyRelativeBearing(wb - effectiveYaw);
            if (logIdx < static_cast<int>(sizeof(sector_log)) - 1) {
                sector_log[logIdx++] = SectorTag(s)[0];
            }
            if (acc::audio::PlayCueAtPosition(
                    acc::audio::NavCue::Wall, c.closest_point,
                    playerPos, range)) {
                ++walls_cued;
                // Stamp shared last_cued_at so T2 sees this wall as
                // recently-cued and skips it.
                g_wall_last_cued_at[c.wall_index] = now;
            }
        }
        sector_log[logIdx] = '\0';
    }

    // --- Objects --------------------------------------------------------
    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    while ((obj = iter.Next()) != nullptr) {
        acc::audio::NavCue cue;
        if (!ClassifyObject(obj, cue)) continue;
        Vector pos;
        if (!acc::engine::GetObjectPosition(obj, pos)) continue;

        float dx = pos.x - playerPos.x;
        float dy = pos.y - playerPos.y;
        float dz = pos.z - playerPos.z;
        float distSq = dx * dx + dy * dy + dz * dz;

        uint32_t handle = acc::engine::GetObjectHandle(obj);

        if (distSq > rangeSq) {
            if (handle != 0) RemoveObjectState(handle);
            continue;
        }
        ++objs_in_range;
        if (handle == 0) continue;
        float dist = std::sqrt(distSq);

        // T2 candidate detection — Front-sector + closer than current
        // best wins. atan2 with horizontal delta only (Z ignored to
        // match the cycle/passive-narrate horizontal convention).
        if (t2_enabled) {
            float wb = std::atan2(dy, dx) * 57.29577951308232f;
            if (ClassifyRelativeBearing(wb - effectiveYaw) ==
                    WallSector::Front &&
                dist < t2_best_dist) {
                t2_best_dist          = dist;
                t2_best.kind          = FeatureKind::Object;
                t2_best.wall_index    = -1;
                t2_best.object_handle = handle;
                t2_best_pos           = pos;
                t2_best_cue           = cue;
            }
        }

        if (!settings.trigger1DistanceDelta) continue;

        bool isNew = false;
        ObjectState* s = FindOrAddObjectState(handle, isNew);
        if (!s) continue;

        bool fire = isNew ||
                    (std::fabs(dist - s->last_distance) > threshold);
        if (fire) {
            if (acc::audio::PlayCueAtPosition(cue, pos, playerPos, range)) {
                ++objs_cued;
                s->last_cued_at = now;
            }
            s->last_distance = dist;
        }
    }

    // --- Trigger 2: foremost-in-front debounce + fire ------------------
    //
    // First-tick suppression: on the first tick after area-change (or
    // first tick since DLL load), seed g_t2_last_fired = current
    // foremost without firing. Mirrors turn_announce's "first
    // observation since DLL load" handling.
    bool t2_fired = false;
    if (t2_enabled) {
        if (!g_t2_initialised) {
            g_t2_last_fired         = t2_best;
            g_t2_pending            = t2_best;
            g_t2_pending_changed_at = now;
            g_t2_initialised        = true;
            acclog::Write(
                "ChangeDetector: T2 first-tick suppress; foremost=%s",
                ForemostKindTag(t2_best.kind));
        } else {
            // Track most-recent-observed foremost + when it last
            // changed. While the player is mid-rotation across multiple
            // features, this fires every tick and keeps changed_at
            // moving — no announcement until the rotation settles.
            if (!ForemostEqual(t2_best, g_t2_pending)) {
                g_t2_pending            = t2_best;
                g_t2_pending_changed_at = now;
            }

            if (!ForemostEqual(g_t2_pending, g_t2_last_fired) &&
                now - g_t2_pending_changed_at >= kT2QuietMs) {
                // Settled on a new foremost. Three exit paths:
                //   1. None  → cone-clear silence (record, don't fire).
                //   2. Wall  → check g_wall_last_cued_at[index].
                //   3. Object → check ObjectState.last_cued_at.
                bool fire = false;
                if (g_t2_pending.kind == FeatureKind::Wall &&
                    g_t2_pending.wall_index >= 0 &&
                    g_t2_pending.wall_index < g_wall_count) {
                    DWORD lastAt = g_wall_last_cued_at[
                        g_t2_pending.wall_index];
                    if (lastAt == 0 || now - lastAt > kT2QuietMs) fire = true;
                } else if (g_t2_pending.kind == FeatureKind::Object) {
                    ObjectState* s = FindObjectState(
                        g_t2_pending.object_handle);
                    DWORD lastAt = s ? s->last_cued_at : 0;
                    if (lastAt == 0 || now - lastAt > kT2QuietMs) fire = true;
                }
                // None → fall through with fire=false (record only).

                if (fire) {
                    bool ok = acc::audio::PlayCueAtPosition(
                        t2_best_cue, t2_best_pos, playerPos, range);
                    if (ok) {
                        t2_fired = true;
                        if (g_t2_pending.kind == FeatureKind::Wall) {
                            g_wall_last_cued_at[g_t2_pending.wall_index] = now;
                        } else if (g_t2_pending.kind == FeatureKind::Object) {
                            ObjectState* s = FindObjectState(
                                g_t2_pending.object_handle);
                            if (s) s->last_cued_at = now;
                        }
                    }
                    acclog::Write(
                        "ChangeDetector: T2 fire kind=%s dist=%.2f "
                        "played=%d (%s -> %s)",
                        ForemostKindTag(g_t2_pending.kind),
                        t2_best_dist, ok ? 1 : 0,
                        ForemostKindTag(g_t2_last_fired.kind),
                        ForemostKindTag(g_t2_pending.kind));
                }
                g_t2_last_fired = g_t2_pending;
            }
        }
    }

    // Tick summary — emitted only when something actually fired this
    // tick. Wall, object, and T2 summaries are split so the per-sector
    // diagnostic only shows when walls actually fired (drops the
    // confusing "sectors=-" placeholder for object-only / T2-only ticks).
    if (walls_cued > 0) {
        acclog::Write(
            "ChangeDetector: tick walls_in_range=%d wall_candidates=%d "
            "sectors_active=%d wall_overflow=%d walls_cued=%d sectors=%s "
            "objs_in_range=%d objs_cued=%d t2_fired=%d",
            walls_in_range, wall_candidates, sectors_active, wall_overflow,
            walls_cued, sector_log,
            objs_in_range, objs_cued, t2_fired ? 1 : 0);
    } else if (objs_cued > 0 || t2_fired) {
        acclog::Write(
            "ChangeDetector: tick walls_in_range=%d wall_candidates=%d "
            "objs_in_range=%d objs_cued=%d t2_fired=%d",
            walls_in_range, wall_candidates,
            objs_in_range, objs_cued, t2_fired ? 1 : 0);
    }
}

}  // namespace acc::spatial::change_detector
