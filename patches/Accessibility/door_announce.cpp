#include "door_announce.h"

#include <windows.h>
#include <cstdint>

#include "camera_announce.h"
#include "engine_player.h"  // GetPlayerServerCreature
#include "log.h"

namespace acc::door_announce {

namespace {

// The autoturn that faced the door fires a direction-change announce a beat
// before the open; suppress a same-direction door readout inside this window
// so the two don't double up. (Per the design request: ~1s.)
constexpr unsigned int kDedupMs = 1000;

// CGameObject layout: vtable @0x0, id @0x4, object_type @0x8.
constexpr size_t kGameObjectIdOffset = 0x4;

// One pending slot; the OpenDoor detour records into it, Tick drains it the
// same frame. Last-writer-wins if two doors open in one frame (negligible).
bool     s_pending       = false;
uint32_t s_pendingOpener = 0;

bool ReadServerObjectId(void* serverObj, uint32_t& outId) {
    if (!serverObj) return false;
    __try {
        outId = *reinterpret_cast<uint32_t*>(
            static_cast<unsigned char*>(serverObj) + kGameObjectIdOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void NoteDoorOpened(uint32_t openerServerId) {
    s_pendingOpener = openerServerId;
    s_pending       = true;
}

void Tick() {
    if (!s_pending) return;
    s_pending = false;
    uint32_t opener = s_pendingOpener;

    // Only the player-controlled character's own door-opens re-orient the
    // player. GetPlayerServerCreature is the currently-controlled leader's
    // server creature; its game-object id shares the server namespace with the
    // opener id OpenDoor passed us.
    void*    leaderCreature = acc::engine::GetPlayerServerCreature();
    uint32_t leaderId       = 0;
    if (!ReadServerObjectId(leaderCreature, leaderId) || leaderId == 0) return;

    if (opener != leaderId) {
        acclog::Write("DoorAnnounce",
            "door opened by 0x%08x != leader 0x%08x — skip", opener, leaderId);
        return;
    }

    acc::camera_announce::AnnounceCurrentFacing(kDedupMs);
}

}  // namespace acc::door_announce

// CSWSDoor::OpenDoor detour @0x00589ceb — fires at the START of the open, right
// where the engine stamps the opener id into the door and is about to call
// SetOpenState to begin the animation. ESI = CSWSDoor* (this, unused here),
// EDI = param_1 = opener's SERVER object id. Server-side, so the id is directly
// comparable to the leader's. We only record; the facing readout + player
// filter + dedup run on the next tick.
extern "C" void __cdecl OnDoorOpen(void* /*serverDoor*/, uint32_t openerServerId) {
    acc::door_announce::NoteDoorOpened(openerServerId);
}
