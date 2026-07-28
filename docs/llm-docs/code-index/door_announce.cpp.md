# door_announce.cpp (78 lines)

Speaks the player's current camera facing the instant a door THEY opened begins to open (KOTOR autoturns the player toward interacted objects, disorienting a blind player). Records the opener id from the `OnDoorOpen` detour trampoline, filters to leader-only on the next Tick, then defers to camera_announce for the actual readout + de-dup. Talks to camera_announce, engine_player.

## Declarations (in source order)

- L17 — `constexpr unsigned int kDedupMs = 1000` — suppresses same-direction readout already spoken by the autoturn's own direction-change announce
- L20 — `constexpr size_t kGameObjectIdOffset = 0x4` — CGameObject.id
- L24-25 — `bool s_pending`, `uint32_t s_pendingOpener` — single pending slot, last-writer-wins
- L27 — `bool ReadServerObjectId(void* serverObj, uint32_t& outId)`
- L40 — `void NoteDoorOpened(uint32_t openerServerId)` (public)
- L45 — `void Tick()` (public)
  note: compares opener id against `GetPlayerServerCreature`'s handle; non-leader opens are skipped and logged
- L75 — `extern "C" void __cdecl OnDoorOpen(void*, uint32_t openerServerId)`
  note: CSWSDoor::OpenDoor detour @0x00589ceb, fires at start-of-open before SetOpenState; ESI=CSWSDoor* this (unused), EDI=param_1=opener server id
