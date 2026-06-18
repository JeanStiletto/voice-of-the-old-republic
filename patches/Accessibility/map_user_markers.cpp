#include "map_user_markers.h"

#include <cstdio>

#include "engine_area.h"
#include "engine_panels.h"
#include "engine_player.h"
#include "hotkeys.h"
#include "log.h"
#include "map_ui_cursor.h"
#include "strings.h"
#include "prism.h"
#include "transitions.h"

namespace acc::map_user_markers {

namespace {

// Per-area sequence number. Resets to 0 when the area pointer changes;
// concatenated into the auto-name so multiple markers in the same area
// stay distinguishable when the room name is the same.
void*    g_lastArea    = nullptr;
uint32_t g_seqInArea   = 0;

// Registry of CSWCMapPin* this mod created in the current area. The engine
// frees all map pins on area transition (ClearAllMapPins), so the registry
// is reset in lockstep when the area pointer changes — no stale pointers
// survive into a new area-load. Markers per area are few (the user drops a
// handful), so a small fixed array beats a heap container here.
constexpr int kMaxUserMarkers = 64;
void* g_userMarkers[kMaxUserMarkers] = {nullptr};
int   g_userMarkerCount = 0;

// Reset the per-area sequence + marker registry when the player changes
// area. Caller supplies the current area so we don't re-resolve.
void MaybeResetForArea(void* currentArea) {
    if (currentArea != g_lastArea) {
        g_lastArea        = currentArea;
        g_seqInArea       = 0;
        g_userMarkerCount = 0;
        for (int i = 0; i < kMaxUserMarkers; ++i) g_userMarkers[i] = nullptr;
    }
}

// Record a pin we just created so IsUserMarkerPin can recognise it. No-op
// past capacity (the marker still works; it just won't skip fog in the
// cycle/cursor — acceptable degradation at 64+ markers in one area).
void RegisterUserMarker(void* pin) {
    if (!pin) return;
    if (g_userMarkerCount < kMaxUserMarkers) {
        g_userMarkers[g_userMarkerCount++] = pin;
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

    // Tier 1 — proximity-based landmark lookup (15m, matching the
    // cursor / walking-adapter window). Landmark binding by .lyt-room
    // breaks down on K1's sliver-shaped rooms.
    constexpr float kLandmarkRangeM = 15.0f;
    char   landmarkBuf[128] = {0};
    Vector landmarkPos;
    if (acc::transitions::FindLandmarkNear(
            pos, kLandmarkRangeM,
            landmarkBuf, sizeof(landmarkBuf), landmarkPos) &&
        landmarkBuf[0] != '\0') {
        std::snprintf(outBuf, bufSize, tplWithRoom, landmarkBuf, seq);
        return;
    }

    // Tier 2 — engine-supplied authored room name (skip vanilla
    // resref-style noise).
    if (area) {
        int roomIdx = -1;
        acc::engine::GetRoomAtIndexed(area, pos, roomIdx);
        if (roomIdx >= 0) {
            char roomBuf[128] = {0};
            if (acc::engine::GetRoomDisplayName(
                    area, roomIdx, roomBuf, sizeof(roomBuf)) &&
                roomBuf[0] != '\0' &&
                !acc::transitions::IsResrefStyleRoomName(roomBuf)) {
                std::snprintf(outBuf, bufSize, tplWithRoom, roomBuf, seq);
                return;
            }
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
        prism::Speak(
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

    // Track identity so the map-hint cycle / cursor recognise this as a
    // player marker (fog-exempt) rather than guessing from its reference
    // number, which collides with the engine's client-id-keyed pins.
    RegisterUserMarker(newPin);

    char msg[160];
    std::snprintf(msg, sizeof(msg),
        acc::strings::Get(acc::strings::Id::FmtSavedMarkerPlaced), name);
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("UserMarker",
        "drop ok: pin=%p clientArea=%p pos=(%.1f,%.1f,%.1f) "
        "ref=0x%08x seq=%u name=\"%s\"",
        newPin, clientArea, cursorWorld.x, cursorWorld.y, cursorWorld.z,
        refNum, seq, name);
}

}  // namespace

bool IsUserMarkerPin(void* pin) {
    if (!pin) return false;
    // Self-sync to the current area first: if the player has since changed
    // area, the registry holds freed pointers from the old load and must be
    // cleared before any identity compare (a recycled allocation could
    // otherwise alias a stale entry). Cheap — MaybeResetForArea only does
    // work on an actual area change.
    MaybeResetForArea(acc::engine::GetCurrentArea());
    for (int i = 0; i < g_userMarkerCount; ++i) {
        if (g_userMarkers[i] == pin) return true;
    }
    return false;
}

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
