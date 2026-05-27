#include "announce_degrees.h"

#include <cmath>
#include <cstdio>

#include "camera_announce.h"
#include "engine_area.h"
#include "engine_compass.h"
#include "engine_panels.h"
#include "engine_player.h"
#include "hotkeys.h"
#include "log.h"
#include "strings.h"
#include "prism.h"
#include "transitions.h"
#include "wall_topology.h"

namespace acc::announce_degrees {

namespace {

constexpr float kDegToRad = 0.017453292519943295f;

int CompassDegreesFromEngineYaw(float engineYaw) {
    float compass = acc::engine::EngineYawToCompass(engineYaw);
    int degrees = static_cast<int>(std::floor(compass + 0.5f)) % 360;
    if (degrees < 0) degrees += 360;
    return degrees;
}

// Camera facing in engine yaw (0° = +X, CCW+). Prefers camera_announce's
// cached value; falls back to deriving from camera→player vector when
// announce hasn't anchored.
bool ReadCameraEngineYawDegrees(float& out) {
    if (acc::camera_announce::TryGetCameraEngineYawDegrees(out)) return true;
    Vector cam, player;
    if (!acc::engine::GetCameraPosition(cam)) return false;
    if (!acc::engine::GetPlayerPosition(player)) return false;
    float dx = player.x - cam.x;
    float dy = player.y - cam.y;
    if (std::fabs(dx) < 1e-3f && std::fabs(dy) < 1e-3f) return false;
    constexpr float kRadToDeg = 57.29577951308232f;
    float yaw = std::atan2(dy, dx) * kRadToDeg;
    if (yaw < 0.0f) yaw += 360.0f;
    out = yaw;
    return true;
}

const char* SectorWord(int compassDegrees) {
    int sector = acc::engine::CompassToSector(static_cast<float>(compassDegrees));
    return acc::strings::Get(acc::engine::SectorString(sector));
}

// Perceptual-region label at the player's position. False when no
// labelled cluster (graph not built, out-of-snap, or open-area-only — too
// vague to add value). Cluster source matches transitions.cpp so this
// readout agrees with the last in-world cluster announcement.
bool ResolveClusterLabelForPlayer(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';

    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) return false;
    void* area = acc::engine::GetCurrentArea();
    if (!area) return false;

    char buf[160] = {0};
    int sig = 0;
    int cid = acc::wall_topology::kClusterIdNone;
    if (!acc::wall_topology::LookupAt(area, pos, buf, sizeof(buf),
                                      sig, cid)) {
        return false;
    }
    if (cid == acc::wall_topology::kClusterIdNone ||
        cid == acc::wall_topology::kClusterIdOpenArea ||
        buf[0] == '\0') {
        return false;
    }
    std::snprintf(outBuf, bufSize, "%s", buf);
    return true;
}

void OnAnnounceWorldDegrees() {
    float engineYaw = 0.0f;
    if (!ReadCameraEngineYawDegrees(engineYaw)) {
        // Mid-transient (spawn / area-load / chain degenerate).
        acclog::Write("AnnounceDegrees", "camera yaw unavailable, skipping");
        return;
    }
    int degrees    = CompassDegreesFromEngineYaw(engineYaw);
    const char* sector = SectorWord(degrees);

    char clusterBuf[160];
    bool haveCluster = ResolveClusterLabelForPlayer(clusterBuf,
                                                    sizeof(clusterBuf));

    char msg[256];
    if (haveCluster) {
        std::snprintf(msg, sizeof(msg),
            acc::strings::Get(acc::strings::Id::FmtWorldStateOriented),
            sector, clusterBuf);
    } else {
        std::snprintf(msg, sizeof(msg),
            acc::strings::Get(acc::strings::Id::FmtWorldStateUnknownCluster),
            sector);
    }
    // Normal priority — one-shot review key, no held-key cancel pressure
    // to bypass. Urgent SAPI would change voice for no benefit.
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("AnnounceDegrees",
        "world -> [%s] (camYaw=%.1f, deg=%d, sector=\"%s\", cluster=\"%s\")",
        msg, engineYaw, degrees, sector, haveCluster ? clusterBuf : "");
}

// Three-tier room name lookup matching transitions::Tick:
// landmark → friendly room name → synthetic "Room N".
bool ResolveRoomNameForPlayer(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';

    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) return false;
    void* area = acc::engine::GetCurrentArea();
    if (!area) return false;

    constexpr float kLandmarkRangeM = 15.0f;
    char   landmarkBuf[128] = {0};
    Vector landmarkPos;
    if (acc::transitions::FindLandmarkNear(
            pos, kLandmarkRangeM,
            landmarkBuf, sizeof(landmarkBuf), landmarkPos) &&
        landmarkBuf[0] != '\0') {
        std::snprintf(outBuf, bufSize, "%s", landmarkBuf);
        return true;
    }

    int roomIdx = -1;
    acc::engine::GetRoomAtIndexed(area, pos, roomIdx);
    if (roomIdx < 0) return false;

    // Skip resref-style noise like "m02_03e".
    char roomBuf[128] = {0};
    if (acc::engine::GetRoomDisplayName(area, roomIdx,
                                        roomBuf, sizeof(roomBuf)) &&
        roomBuf[0] != '\0' &&
        !acc::transitions::IsResrefStyleRoomName(roomBuf)) {
        std::snprintf(outBuf, bufSize, "%s", roomBuf);
        return true;
    }
    std::snprintf(outBuf, bufSize,
                  acc::strings::Get(acc::strings::Id::FmtTransitionRoomIndex),
                  roomIdx);
    return true;
}

void OnAnnounceMapDegrees() {
    // Either failure falls back to world-frame so the key never feels eaten.
    float camYawDeg = 0.0f;
    if (!ReadCameraEngineYawDegrees(camYawDeg)) {
        acclog::Write("AnnounceDegrees",
            "map: camera facing unavailable; falling back to world");
        OnAnnounceWorldDegrees();
        return;
    }
    float yawRad = camYawDeg * kDegToRad;
    Vector facing;
    facing.x = std::cos(yawRad);
    facing.y = std::sin(yawRad);
    facing.z = 0.0f;
    void* areaMap = acc::engine::GetAreaMap();
    if (!areaMap) {
        acclog::Write("AnnounceDegrees",
            "map: areaMap unavailable; falling back to world");
        OnAnnounceWorldDegrees();
        return;
    }

    float mapYawCCW = 0.0f;
    if (!acc::engine::GetMapRotateCCWFromWorldOrientation(
            areaMap, facing, mapYawCCW)) {
        acclog::Write("AnnounceDegrees",
            "map: GetMapRotateCCW failed; falling back to world");
        OnAnnounceWorldDegrees();
        return;
    }

    // Engine returns CCW-from-+X in map space, same convention as engine
    // yaw — EngineYawToCompass gives CW-from-North matching the map arrow.
    int degrees = CompassDegreesFromEngineYaw(mapYawCCW);
    const char* sector = SectorWord(degrees);

    char roomBuf[160];
    bool haveRoom = ResolveRoomNameForPlayer(roomBuf, sizeof(roomBuf));

    char msg[384];
    if (haveRoom) {
        std::snprintf(msg, sizeof(msg),
            acc::strings::Get(acc::strings::Id::FmtMapStateOriented),
            roomBuf, degrees, sector);
    } else {
        std::snprintf(msg, sizeof(msg),
            acc::strings::Get(acc::strings::Id::FmtMapStateUnknownRoom),
            degrees, sector);
    }
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("AnnounceDegrees",
        "map -> [%s] (camYaw=%.1f, mapYawCCW=%.1f, deg=%d, room=\"%s\")",
        msg, camYawDeg, mapYawCCW, degrees, haveRoom ? roomBuf : "");
}

}  // namespace

void PollWin32() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::AnnounceDegrees)) return;

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return;

    if (acc::engine::HasActiveMapPanel()) {
        OnAnnounceMapDegrees();
    } else {
        OnAnnounceWorldDegrees();
    }
}

}  // namespace acc::announce_degrees
