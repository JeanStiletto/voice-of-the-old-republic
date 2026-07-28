# door_announce.h (35 lines)

Door-open facing readout: KOTOR autoturns the player to face an interacted door, so this announces current camera facing the moment the door begins opening (before SetOpenState animates). Reacts only to doors the player-controlled leader opens.

## Declarations (in source order)

- L27 — `void NoteDoorOpened(uint32_t openerServerId)`
  note: called from the OpenDoor detour (main thread); no engine reads/speech here
- L32 — `void Tick()`
  note: must run AFTER camera_announce::Tick so its de-dup sees this frame's last-spoken sector
