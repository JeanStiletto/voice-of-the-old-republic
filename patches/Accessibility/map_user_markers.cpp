#include "map_user_markers.h"

#include <cstdio>

#include "engine_area.h"
#include "engine_panels.h"
#include "engine_player.h"
#include "hotkeys.h"
#include "log.h"
#include "map_ui_cursor.h"
#include "strings.h"
#include "tolk.h"
#include "transitions.h"

namespace acc::map_user_markers {

namespace {

// Per-area sequence number. Resets to 0 when the area pointer changes;
// concatenated into the auto-name so multiple markers in the same area
// stay distinguishable when the room name is the same.
void*    g_lastArea    = nullptr;
uint32_t g_seqInArea   = 0;

// Reset the per-area sequence when the player changes area. Caller
// supplies the current area so we don't re-resolve.
void MaybeResetForArea(void* currentArea) {
    if (currentArea != g_lastArea) {
        g_lastArea  = currentArea;
        g_seqInArea = 0;
    }
}

// Compose a friendly auto-name from the cursor's spot. Falls back to
// a bare "Marker N" if no room context is available.
//
//   Format: "{room/landmark} - Marker N" (DE: "Marke N")
//   Fallback: "Marker N" alone
//
// 64 bytes is plenty for a localized name (Bioware landmark names are
// short — "Brücke", "Frachtraum"; even with a numeric suffix this
// fits easily).
void BuildAutoName(void* area, const Vector& pos, int seq,
                   char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 4) return;
    const char* tpl = acc::strings::Get(
        acc::strings::Id::FmtSavedMarkerAutoNumber);
    const char* tplWithRoom = acc::strings::Get(
        acc::strings::Id::FmtSavedMarkerAutoWithRoom);

    int roomIdx = -1;
    if (area) acc::engine::GetRoomAtIndexed(area, pos, roomIdx);
    if (roomIdx >= 0) {
        const char* landmark =
            acc::transitions::GetLandmarkForRoom(roomIdx);
        if (landmark && landmark[0] != '\0') {
            std::snprintf(outBuf, bufSize, tplWithRoom, landmark, seq);
            return;
        }
        char roomBuf[128] = {0};
        if (area && acc::engine::GetRoomDisplayName(
                area, roomIdx, roomBuf, sizeof(roomBuf)) &&
            roomBuf[0] != '\0' &&
            !acc::transitions::IsResrefStyleRoomName(roomBuf)) {
            std::snprintf(outBuf, bufSize, tplWithRoom, roomBuf, seq);
            return;
        }
    }
    std::snprintf(outBuf, bufSize, tpl, seq);
}

void OnDrop() {
    if (!acc::engine::HasActiveMapPanel()) {
        // Out-of-map Shift+Q has no meaning right now. Stay silent so
        // the chord doesn't tax the user when fired from world context.
        return;
    }
    Vector cursorWorld;
    if (!acc::map_ui_cursor::TryGetCursorWorldPosition(cursorWorld)) {
        acclog::Write("UserMarker",
            "drop refused: cursor inactive (map fg without seeded cursor)");
        return;
    }

    void* serverArea = acc::engine::GetCurrentArea();
    if (!serverArea) {
        acclog::Write("UserMarker", "drop refused: no current area");
        return;
    }
    void* clientArea = acc::engine::GetClientArea(serverArea);
    if (!clientArea) {
        acclog::Write("UserMarker",
            "drop refused: client area unresolved (server=%p)", serverArea);
        return;
    }

    MaybeResetForArea(serverArea);
    uint32_t seq = ++g_seqInArea;
    uint32_t refNum = kUserMarkerReferenceBase + seq;

    char name[64];
    BuildAutoName(serverArea, cursorWorld, static_cast<int>(seq),
                  name, sizeof(name));

    void* newPin = nullptr;
    bool ok = acc::engine::CreateMapPin(clientArea, cursorWorld, name,
                                        refNum, &newPin);
    if (!ok) {
        // Never silent: speak a localised "couldn't save" message.
        tolk::Speak(
            acc::strings::Get(acc::strings::Id::SavedMarkerFailed),
            /*interrupt=*/true);
        acclog::Write("UserMarker",
            "CreateMapPin failed: clientArea=%p pos=(%.1f,%.1f,%.1f) "
            "ref=0x%08x seq=%u name=\"%s\"",
            clientArea, cursorWorld.x, cursorWorld.y, cursorWorld.z,
            refNum, seq, name);
        // Roll back the sequence so the next attempt doesn't skip a number.
        --g_seqInArea;
        return;
    }

    char msg[160];
    std::snprintf(msg, sizeof(msg),
        acc::strings::Get(acc::strings::Id::FmtSavedMarkerPlaced), name);
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write("UserMarker",
        "drop ok: pin=%p clientArea=%p pos=(%.1f,%.1f,%.1f) "
        "ref=0x%08x seq=%u name=\"%s\"",
        newPin, clientArea, cursorWorld.x, cursorWorld.y, cursorWorld.z,
        refNum, seq, name);
}

}  // namespace

void PollWin32() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::SaveMarkerAtCursor)) {
        return;
    }
    // In-world gate — speak only when a player is loaded; otherwise the
    // map panel can't be foreground anyway. Belt-and-braces.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return;
    OnDrop();
}

}  // namespace acc::map_user_markers
