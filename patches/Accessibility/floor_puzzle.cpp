// Rakatan temple floor-plate puzzle assist — see floor_puzzle.h for the
// puzzle model (decompiled k_punk_floor01..09 / k_punk_reset scripts).

#include "floor_puzzle.h"

#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "engine_area.h"     // AreaObjectIterator, GetObjectKind/Tag, GetTriggerGeometry, GetObjectLocalBoolean, ResolveServerObjectHandle, GetCurrentArea, GetCurrentAreaResName
#include "engine_keymap.h"   // GameActionVk — the game's live solo-mode bind
#include "engine_player.h"   // GetPlayerPosition, GetPlayerYawDegrees, GetPartyMembers, GetSoloMode
#include "hotkeys.h"         // VkLabel
#include "log.h"
#include "prism.h"
#include "strfmt.h"
#include "strings.h"

namespace acc::floor_puzzle {

namespace {

using acc::strings::Get;
using acc::strings::Id;

// Poll cadence. Plate polygons are ~1.6-2.0m and walk speed ~5m/s, so
// 150ms bounds the detection lag to well under one plate length; the
// toggle scripts run inside the engine's own trigger dispatch, so the
// board read one poll later already sees the new state.
constexpr DWORD kScanIntervalMs = 150;

// The only module this feature exists in.
constexpr char kPuzzleModule[] = "unk_m44ab";

// NWScript local-boolean index the plate scripts store the lit state in
// (GetLocalBoolean(panel, 10) throughout k_punk_floor05's decompile).
constexpr int kLitBooleanIndex = 10;

// swkotor.ini [Keymapping] id for Solo Mode: keymap.2da row 7
// ("PartyActive", default V) + the 200 offset verified via row 64
// STEALTH <-> Action264.
constexpr int kSoloModeActionId = 207;
constexpr int kSoloModeDefaultVk = 'V';

// Proximity warning band while off-plate (metres to the nearest plate
// edge). Enter under 0.6m — inside one step of the edge but before the
// polygon; re-arm only after backing off past 1.1m.
constexpr float kWarnEnterM = 0.6f;
constexpr float kWarnExitM  = 1.1f;

// Nearest-plate announcements: re-announce on identity change, but not
// faster than this (walking a lane past the grid changes the nearest
// plate every ~2m; unthrottled that is chatter, throttled it reads as a
// position stream).
constexpr DWORD kNearestMinGapMs = 700;

// One-shot intro fires when the leader first comes this close to the
// grid centre (the room is ~15m long; 12m covers "walked in").
constexpr float kIntroRadiusM = 12.0f;

// Board-delta announcements within this window of the leader entering a
// plate are attributed to that step; outside it they were caused by a
// follower walking the grid.
constexpr DWORD kLeaderToggleWindowMs = 600;

constexpr int kPlateCount = 9;
constexpr int kResetIndex = 9;   // g_plates slot for the reset trigger
constexpr int kTrackCount = 10;
constexpr int kMaxPolyVerts = 8;

struct Plate {
    uint32_t trigHandle = 0;
    uint32_t plcHandle  = 0;   // FloorPanelNN placeable (lit-state carrier)
    Vector   poly[kMaxPolyVerts];
    int      vertCount  = 0;
    Vector   center     = {0.0f, 0.0f, 0.0f};
};

// Grid-position word per plate index (tag number - 1). Rows run
// north->south, columns west->east: kFloorPanel01 is the north-west
// corner (GIT geometry, m44ab), 05 the centre, 09 the south-east corner.
constexpr Id kPlateWord[kPlateCount] = {
    Id::DirNorthwest, Id::DirNorth,  Id::DirNortheast,
    Id::DirWest,      Id::PlateCenterWord, Id::DirEast,
    Id::DirSouthwest, Id::DirSouth,  Id::DirSoutheast,
};

Plate g_plates[kTrackCount];
bool  g_cacheReady = false;

void* g_area         = nullptr;
bool  g_isPuzzleArea = false;

bool  g_lit[kPlateCount] = {};
bool  g_haveLit    = false;
int   g_curPlate   = -1;     // -1 none, 0..8 plate, 9 reset
int   g_lastNearest = -1;
DWORD g_nearestSpokenAt = 0;
bool  g_warnArmed[kTrackCount] = {};
DWORD g_lastEntryAt = 0;     // leader stepped onto a plate (0..8) at this tick
bool  g_introSpoken    = false;
bool  g_solvedQuiet    = false;
bool  g_resetHintSpoken = false;
DWORD g_lastScan = 0;

void ResetAreaState() {
    for (auto& p : g_plates) p = Plate{};
    g_cacheReady = false;
    g_haveLit    = false;
    g_curPlate   = -1;
    g_lastNearest = -1;
    g_nearestSpokenAt = 0;
    for (auto& w : g_warnArmed) w = true;
    g_lastEntryAt = 0;
    g_introSpoken     = false;
    g_solvedQuiet     = false;
    g_resetHintSpoken = false;
}

// ---- Naming -----------------------------------------------------------------

// Short name: the bare direction word ("Nord-West", "Mitte") used inside
// delta lists; kResetIndex gets the full reset-plate noun.
const char* ShortName(int idx) {
    if (idx == kResetIndex) return Get(Id::PlateResetName);
    if (idx < 0 || idx >= kPlateCount) return "";
    return Get(kPlateWord[idx]);
}

// Full name: "Platte <word>" for grid plates, the reset noun as-is.
std::string FullName(int idx) {
    if (idx == kResetIndex) return std::string(Get(Id::PlateResetName));
    return acc::strfmt::Format(Get(Id::FmtPlateName), ShortName(idx));
}

// ---- Geometry ----------------------------------------------------------------

bool PointInPoly(const Plate& p, float x, float y) {
    if (p.vertCount < 3) return false;
    bool in = false;
    for (int i = 0, j = p.vertCount - 1; i < p.vertCount; j = i++) {
        const Vector& a = p.poly[i];
        const Vector& b = p.poly[j];
        if ((a.y > y) != (b.y > y) &&
            x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) {
            in = !in;
        }
    }
    return in;
}

float DistToSegment(float px, float py, const Vector& a, const Vector& b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (len2 > 1e-6f) {
        t = ((px - a.x) * dx + (py - a.y) * dy) / len2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    float cx = a.x + t * dx - px, cy = a.y + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}

// 0 when inside; else the closest XY distance to any polygon edge.
float DistToPlate(const Plate& p, float x, float y) {
    if (p.vertCount < 3) return 1e9f;
    if (PointInPoly(p, x, y)) return 0.0f;
    float best = 1e9f;
    for (int i = 0, j = p.vertCount - 1; i < p.vertCount; j = i++) {
        float d = DistToSegment(x, y, p.poly[i], p.poly[j]);
        if (d < best) best = d;
    }
    return best;
}

// World-cardinal axis offsets toward `target`, e.g. "2 Meter Nord,
// 1 Meter West" — per user direction the puzzle is walked with cardinal
// stutter-steps (align north once, then forward/back + strafe), so
// facing-relative clock bearings are the wrong frame here: they mix both
// axes into one diagonal number. Vocabulary is limited to the four
// cardinal words; an axis already aligned within half a metre is
// dropped. Empty when both axes are aligned.
std::string AxisPhrase(const Vector& playerPos, const Vector& target) {
    float dN = target.y - playerPos.y;  // world +Y = north
    float dE = target.x - playerPos.x;  // world +X = east
    const char* fmt = Get(Id::FmtRouteSegment);
    std::string out;
    if (std::fabs(dN) >= 0.5f) {
        int m = static_cast<int>(std::fabs(dN) + 0.5f);
        if (m < 1) m = 1;
        out = acc::strfmt::Format(fmt, m,
                                  Get(dN >= 0 ? Id::DirNorth : Id::DirSouth));
    }
    if (std::fabs(dE) >= 0.5f) {
        int m = static_cast<int>(std::fabs(dE) + 0.5f);
        if (m < 1) m = 1;
        if (!out.empty()) out += ", ";
        out += acc::strfmt::Format(fmt, m,
                                   Get(dE >= 0 ? Id::DirEast : Id::DirWest));
    }
    return out;
}

// The louder of the two axis components, for the close-range warning
// ("unter einem Meter Nord").
const char* DominantCardinal(const Vector& playerPos, const Vector& target) {
    float dN = target.y - playerPos.y;
    float dE = target.x - playerPos.x;
    if (std::fabs(dN) >= std::fabs(dE)) {
        return Get(dN >= 0 ? Id::DirNorth : Id::DirSouth);
    }
    return Get(dE >= 0 ? Id::DirEast : Id::DirWest);
}

// ---- Cache -------------------------------------------------------------------

// Trigger tag -> slot. Tags authored as kFloorPanel01..09 / kPanelReset;
// compare case-insensitively.
int TrigSlotForTag(const char* tag) {
    if (_stricmp(tag, "kPanelReset") == 0) return kResetIndex;
    if (_strnicmp(tag, "kFloorPanel", 11) == 0) {
        int n = atoi(tag + 11);
        if (n >= 1 && n <= kPlateCount) return n - 1;
    }
    return -1;
}

int PlcSlotForTag(const char* tag) {
    if (_strnicmp(tag, "FloorPanel", 10) == 0) {
        int n = atoi(tag + 10);
        if (n >= 1 && n <= kPlateCount) return n - 1;
    }
    return -1;
}

bool BuildCache(void* area) {
    int trigFound = 0, plcFound = 0;
    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    while ((obj = iter.Next()) != nullptr) {
        int kind = acc::engine::GetObjectKind(obj);
        char tag[64];
        if (kind == static_cast<int>(acc::engine::GameObjectKind::Trigger)) {
            if (!acc::engine::GetObjectTag(obj, tag, sizeof(tag))) continue;
            int slot = TrigSlotForTag(tag);
            if (slot < 0) continue;
            Plate& p = g_plates[slot];
            p.vertCount = acc::engine::GetTriggerGeometry(obj, p.poly,
                                                          kMaxPolyVerts);
            if (p.vertCount < 3) continue;
            // The GIT authors trigger polygons as position + local
            // offsets; the engine normally translates to world on load.
            // Defend against a local-space read: room coordinates here
            // are all > 60, local offsets all within a few metres of 0.
            if (std::fabs(p.poly[0].x) < 30.0f &&
                std::fabs(p.poly[0].y) < 30.0f) {
                Vector base{};
                if (acc::engine::GetObjectPosition(obj, base)) {
                    for (int i = 0; i < p.vertCount; ++i) {
                        p.poly[i].x += base.x;
                        p.poly[i].y += base.y;
                        p.poly[i].z += base.z;
                    }
                    acclog::Write("FloorPuzzle",
                                  "trigger %s geometry was local-space; "
                                  "translated by (%.1f,%.1f)",
                                  tag, base.x, base.y);
                }
            }
            p.trigHandle = acc::engine::GetObjectHandle(obj);
            p.center = {0.0f, 0.0f, 0.0f};
            for (int i = 0; i < p.vertCount; ++i) {
                p.center.x += p.poly[i].x;
                p.center.y += p.poly[i].y;
                p.center.z += p.poly[i].z;
            }
            p.center.x /= p.vertCount;
            p.center.y /= p.vertCount;
            p.center.z /= p.vertCount;
            ++trigFound;
        } else if (kind ==
                   static_cast<int>(acc::engine::GameObjectKind::Placeable)) {
            if (!acc::engine::GetObjectTag(obj, tag, sizeof(tag))) continue;
            int slot = PlcSlotForTag(tag);
            if (slot < 0) continue;
            g_plates[slot].plcHandle = acc::engine::GetObjectHandle(obj);
            ++plcFound;
        }
    }
    if (trigFound < kTrackCount || plcFound < kPlateCount) {
        acclog::Write("FloorPuzzle",
                      "cache incomplete: triggers=%d/10 placeables=%d/9 — "
                      "retrying next scan",
                      trigFound, plcFound);
        return false;
    }
    // Geometry sanity line: the NW plate should sit near (71.1, 92.3)
    // world if the engine stores trigger polygons world-space (GIT
    // authoring says local + position; the engine translates on load).
    acclog::Write("FloorPuzzle",
                  "cache ready: plate01 verts=%d center=(%.1f,%.1f) "
                  "reset center=(%.1f,%.1f)",
                  g_plates[0].vertCount, g_plates[0].center.x,
                  g_plates[0].center.y, g_plates[kResetIndex].center.x,
                  g_plates[kResetIndex].center.y);
    return true;
}

// ---- Announce composition ------------------------------------------------

void SpeakIntro(const Vector& playerPos) {
    float dx = playerPos.x - g_plates[4].center.x;
    float dy = playerPos.y - g_plates[4].center.y;
    if (dx * dx + dy * dy > kIntroRadiusM * kIntroRadiusM) return;
    g_introSpoken = true;
    std::string line(Get(Id::FloorPuzzleIntro));
    uint32_t followers[12];
    int followerCount = acc::engine::GetPartyMembers(
        followers, static_cast<int>(sizeof(followers) / sizeof(followers[0])));
    if (followerCount > 0 && !acc::engine::GetSoloMode()) {
        int vk = acc::engine_keymap::GameActionVk(kSoloModeActionId);
        if (vk == 0) vk = kSoloModeDefaultVk;
        line += " ";
        line += acc::strfmt::Format(Get(Id::FmtFloorSoloHint),
                                    acc::hotkeys::VkLabel(vk));
    }
    prism::Speak(line.c_str(), /*interrupt=*/false);
    acclog::Write("FloorPuzzle", "intro spoken (followers=%d solo=%d)",
                  followerCount, acc::engine::GetSoloMode() ? 1 : 0);
}

// Itemized flip list ("Nord-West leuchtet, West dunkel") + lit count.
std::string DeltaText(const bool* flipped, const bool* lit, int litCount) {
    std::string out;
    if (litCount == 0) {
        out = Get(Id::PlatesAllDark);
        return out;
    }
    for (int i = 0; i < kPlateCount; ++i) {
        if (!flipped[i]) continue;
        if (!out.empty()) out += ", ";
        out += acc::strfmt::Format(
            Get(lit[i] ? Id::FmtPlateLit : Id::FmtPlateDark), ShortName(i));
    }
    if (!out.empty()) out += ". ";
    out += acc::strfmt::Format(Get(Id::FmtPlateLitCount), litCount);
    return out;
}

}  // namespace

bool IsPuzzlePlateTrigger(uint32_t handle) {
    if (!g_isPuzzleArea || !g_cacheReady || handle == 0) return false;
    for (const Plate& p : g_plates) {
        if (p.trigHandle == handle) return true;
    }
    return false;
}

void Tick() {
    DWORD now = GetTickCount();
    if ((now - g_lastScan) < kScanIntervalMs) return;
    g_lastScan = now;

    void* area = acc::engine::GetCurrentArea();
    if (area != g_area) {
        g_area = area;
        ResetAreaState();
        g_isPuzzleArea = false;
        if (area) {
            char resname[64];
            if (acc::engine::GetCurrentAreaResName(resname, sizeof(resname)) &&
                _stricmp(resname, kPuzzleModule) == 0) {
                g_isPuzzleArea = true;
                acclog::Write("FloorPuzzle", "puzzle module entered (%s)",
                              resname);
            }
        }
    }
    if (!area || !g_isPuzzleArea) return;

    if (!g_cacheReady) {
        g_cacheReady = BuildCache(area);
        if (!g_cacheReady) return;
    }

    // ---- Board state ---------------------------------------------------
    bool lit[kPlateCount];
    int  litCount = 0;
    for (int i = 0; i < kPlateCount; ++i) {
        void* plc = acc::engine::ResolveServerObjectHandle(
            g_plates[i].plcHandle);
        lit[i] = plc &&
                 acc::engine::GetObjectLocalBoolean(plc, kLitBooleanIndex);
        if (lit[i]) ++litCount;
    }
    bool flipped[kPlateCount] = {};
    bool anyFlip = false;
    if (g_haveLit) {
        for (int i = 0; i < kPlateCount; ++i) {
            flipped[i] = (lit[i] != g_lit[i]);
            anyFlip |= flipped[i];
        }
    } else {
        // First read for this area visit. An already all-lit board means
        // the puzzle was solved on an earlier visit — stay quiet without
        // announcing success again.
        if (litCount == kPlateCount) {
            g_solvedQuiet = true;
            acclog::Write("FloorPuzzle",
                          "board already all-lit on entry — quiet mode");
        }
        g_haveLit = true;
    }
    std::memcpy(g_lit, lit, sizeof(g_lit));

    // ---- Solved --------------------------------------------------------
    if (!g_solvedQuiet && litCount == kPlateCount && anyFlip) {
        g_solvedQuiet = true;
        prism::Speak(Get(Id::FloorPuzzleSolved), /*interrupt=*/false);
        acclog::Write("FloorPuzzle", "solved — module going quiet");
        return;
    }
    if (g_solvedQuiet) return;

    // ---- Leader position vs plates --------------------------------------
    Vector pos{};
    bool havePos = acc::engine::GetPlayerPosition(pos);
    int newPlate = -1;
    if (havePos) {
        for (int i = 0; i < kTrackCount; ++i) {
            if (PointInPoly(g_plates[i], pos.x, pos.y)) { newPlate = i; break; }
        }
    }
    bool entered = havePos && newPlate != g_curPlate && newPlate >= 0;
    if (havePos) {
        // Reset counts too: its script darkens the board, and that delta
        // must not read as a follower toggle when it lands a poll later.
        if (entered) g_lastEntryAt = now;
        // Stepping off into open floor: drop the nearest anchor so the
        // off-plate stream re-announces the closest plate once.
        if (newPlate < 0 && g_curPlate >= 0) g_lastNearest = -1;
        g_curPlate = newPlate;
    }

    // ---- Entry / delta speech -------------------------------------------
    // A flip within the attribution window of the leader's own step is
    // "their" toggle; a flip outside it means a follower walked the grid.
    bool leaderCaused =
        (now - g_lastEntryAt) <= kLeaderToggleWindowMs && g_lastEntryAt != 0;
    std::string line;
    if (entered) {
        line = acc::strfmt::Format(Get(Id::FmtPlateEntered),
                                   FullName(newPlate).c_str());
        if (newPlate == kResetIndex && !g_resetHintSpoken) {
            g_resetHintSpoken = true;
            line += ". ";
            line += Get(Id::PlateResetHint);
        }
    }
    if (anyFlip) {
        if (!line.empty()) {
            line += ". ";
        } else if (!leaderCaused) {
            line += Get(Id::FloorPartyToggled);
            line += ": ";
        }
        line += DeltaText(flipped, lit, litCount);
    }
    if (!line.empty()) {
        prism::Speak(line.c_str(), /*interrupt=*/false);
        acclog::Write("FloorPuzzle",
                      "announce: [%s] (plate=%d lit=%d leaderCaused=%d)",
                      line.c_str(), newPlate, litCount, leaderCaused ? 1 : 0);
        // Entering a plate consumes any pending edge warnings.
        for (auto& w : g_warnArmed) w = true;
        return;
    }

    if (!havePos) return;

    // ---- Intro ----------------------------------------------------------
    if (!g_introSpoken) {
        SpeakIntro(pos);
        if (g_introSpoken) return;
    }

    // ---- Off-plate: nearest plate + close-range warning -------------------
    if (g_curPlate >= 0) return;
    int   nearest = -1;
    float nearestDist = 1e9f;
    for (int i = 0; i < kTrackCount; ++i) {
        float d = DistToPlate(g_plates[i], pos.x, pos.y);
        if (d < nearestDist) { nearestDist = d; nearest = i; }
    }
    if (nearest < 0) return;

    // Close-range warning has priority over the identity stream: it is
    // the "stop before you step" cue.
    if (nearestDist < kWarnEnterM && g_warnArmed[nearest]) {
        g_warnArmed[nearest] = false;
        std::string warn = acc::strfmt::Format(
            Get(Id::FmtPlateWarnClose), FullName(nearest).c_str(),
            DominantCardinal(pos, g_plates[nearest].center));
        prism::Speak(warn.c_str(), /*interrupt=*/false);
        acclog::Write("FloorPuzzle", "edge warn: [%s] dist=%.2f",
                      warn.c_str(), nearestDist);
        g_lastNearest = nearest;
        g_nearestSpokenAt = now;
        return;
    }
    for (int i = 0; i < kTrackCount; ++i) {
        if (!g_warnArmed[i] &&
            DistToPlate(g_plates[i], pos.x, pos.y) > kWarnExitM) {
            g_warnArmed[i] = true;
        }
    }

    if (nearest != g_lastNearest &&
        (now - g_nearestSpokenAt) >= kNearestMinGapMs) {
        g_lastNearest = nearest;
        g_nearestSpokenAt = now;
        std::string say = FullName(nearest);
        std::string axes = AxisPhrase(pos, g_plates[nearest].center);
        if (!axes.empty()) {
            say += ", ";
            say += axes;
        }
        prism::Speak(say.c_str(), /*interrupt=*/false);
        acclog::Write("FloorPuzzle", "nearest: [%s] dist=%.2f", say.c_str(),
                      nearestDist);
    }
}

}  // namespace acc::floor_puzzle
