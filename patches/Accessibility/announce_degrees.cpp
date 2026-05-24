#include "announce_degrees.h"

#include <cmath>
#include <cstdio>

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

int CompassDegreesFromEngineYaw(float engineYaw) {
    float compass = acc::engine::EngineYawToCompass(engineYaw);
    int degrees = static_cast<int>(std::floor(compass + 0.5f)) % 360;
    if (degrees < 0) degrees += 360;
    return degrees;
}

const char* SectorWord(int compassDegrees) {
    int sector = acc::engine::CompassToSector(static_cast<float>(compassDegrees));
    return acc::strings::Get(acc::engine::SectorString(sector));
}

// Try to fill `outBuf` with the perceptual-region label at the player's
// current world position (wall_topology cluster). Returns true on a real
// labelled cluster; false when the graph isn't built, the position is
// out-of-snap, or only the open-area fallback fires (which is too vague
// to add value to an orientation announce). Matches the cluster-id
// trigger source the in-world transition path uses, so what the user
// hears here lines up with the last in-world cluster announcement.
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
    if (!acc::engine::GetPlayerYawDegrees(engineYaw)) {
        // Yaw degenerate (mid-spawn / area-load). Stay silent — the
        // user pressed during a transient, no sensible answer.
        acclog::Write("AnnounceDegrees", "yaw unavailable, skipping");
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
    // Normal priority by design: this is a one-shot review key. The user
    // pressed AltGr; their screen reader's own typed-char-cancel can't
    // eat it (no held-WASD context) so the urgent SAPI bypass isn't
    // needed and would change which voice the user hears. Degrees are
    // logged for diagnostics but deliberately dropped from speech.
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("AnnounceDegrees",
        "world -> [%s] (engineYaw=%.1f, deg=%d, sector=\"%s\", cluster=\"%s\")",
        msg, engineYaw, degrees, sector, haveCluster ? clusterBuf : "");
}

// Resolve a display name for the layout-room the player currently
// stands in, using the same three-tier chain transitions::Tick uses
// for in-world room announcement (Bioware landmark → friendly room
// name → "Raum N" synthetic). Writes a null-terminated string to
// outBuf and returns true; returns false when no resolvable room
// (player off-walkmesh / area null).
bool ResolveRoomNameForPlayer(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';

    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) return false;
    void* area = acc::engine::GetCurrentArea();
    if (!area) return false;

    int roomIdx = -1;
    acc::engine::GetRoomAtIndexed(area, pos, roomIdx);
    if (roomIdx < 0) return false;

    // Tier 1 — Bioware landmark waypoint (localized, sparse).
    const char* landmark = acc::transitions::GetLandmarkForRoom(roomIdx);
    if (landmark && landmark[0] != '\0') {
        std::snprintf(outBuf, bufSize, "%s", landmark);
        return true;
    }
    // Tier 2 — mod-supplied friendly room_names entry. Skip vanilla
    // resref-style noise ("m02_03e").
    char roomBuf[128] = {0};
    if (acc::engine::GetRoomDisplayName(area, roomIdx,
                                        roomBuf, sizeof(roomBuf)) &&
        roomBuf[0] != '\0' &&
        !acc::transitions::IsResrefStyleRoomName(roomBuf)) {
        std::snprintf(outBuf, bufSize, "%s", roomBuf);
        return true;
    }
    // Tier 3 — synthetic "Raum N" so the user always hears a place
    // marker even when authoring data is bare.
    std::snprintf(outBuf, bufSize,
                  acc::strings::Get(acc::strings::Id::FmtTransitionRoomIndex),
                  roomIdx);
    return true;
}

void OnAnnounceMapDegrees() {
    // Need both the player facing and the area-map singleton to convert
    // the heading into map-frame space. Either failing falls back to
    // the world-frame announcement so the key never feels eaten.
    Vector facing;
    if (!acc::engine::GetPlayerFacing(facing) ||
        (facing.x == 0.0f && facing.y == 0.0f)) {
        acclog::Write("AnnounceDegrees",
            "map: facing unavailable; falling back to world");
        OnAnnounceWorldDegrees();
        return;
    }
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

    // Engine returns CCW-from-+X in map space — same convention as
    // engine yaw. EngineYawToCompass converts to CW-from-North so the
    // sector word + degrees match what a sighted player reads off the
    // map's compass arrow.
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
        "map -> [%s] (mapYawCCW=%.1f, deg=%d, room=\"%s\")",
        msg, mapYawCCW, degrees, haveRoom ? roomBuf : "");
}

}  // namespace

void PollWin32() {
    // Binding lives in `hotkeys.cpp` as Action::AnnounceDegrees — AltGr
    // (VK_RMENU) alone, Shift forbidden so it stays distinct from the
    // Shift+AltGr Mouse Look probe. `Pressed()` covers rising-edge,
    // modifier match, and the KOTOR-foreground gate.
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::AnnounceDegrees)) return;

    // In-world gate — speak only when a player creature is loaded. In
    // menus / chargen / mid-load there's no meaningful "facing" to read.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return;

    // Phase 6 lay-off 2: route to the map-frame payload when the
    // InGameMap panel is foreground. World-frame branch unchanged.
    if (acc::engine::HasActiveMapPanel()) {
        OnAnnounceMapDegrees();
    } else {
        OnAnnounceWorldDegrees();
    }
}

}  // namespace acc::announce_degrees
