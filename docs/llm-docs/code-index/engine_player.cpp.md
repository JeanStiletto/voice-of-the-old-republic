# engine_player.cpp (526 lines)

Implementation of engine_player.h. No leading comment block.

## Declarations (in source order)

- L13 — `namespace acc::engine`
- L15 — `namespace { ... }` (anonymous, PFN typedefs + GetPlayerServerObject)
- L17 — `typedef void* (__thiscall* PFN_GetPlayerCreature)(void*)`
- L18 — `typedef void* (__thiscall* PFN_CSWSObjectGetArea)(void*)`
- L19 — `typedef void* (__thiscall* PFN_GetPlayerCharacterName)(void*)`
- L29 — `void* GetPlayerServerObject()`
  note: private to this TU; centralises chain walk so per-field readers pay one SEH frame not three
- L55 — `bool GetPlayerPosition(Vector& out)`
- L68 — `bool GetPlayerFacing(Vector& out)`
- L82 — `bool GetPlayerYawDegrees(float& out)`
- L93 — `void* GetPlayerArea()`
- L105 — `bool GetCameraPosition(Vector& out)`
- L137 — `void* GetPlayerServerCreature()`
- L141 — `void* GetClientLeader()`
- L157 — `bool GetActiveLeaderName(char* outBuf, size_t bufSize)`
  note: three-path resolution; Path 1 uses GetObjectDisplayNameByHandle, Path 2 reads stats.first_name, Path 3 falls back to PC chargen slot
- L273 — `bool GetPlayerCharacterName(char* outBuf, size_t bufSize)`
- L295 — `namespace { ... }` (auto-restore session state + GetPlayerControl)
- L305 — `typedef void (__thiscall* PFN_CSWPlayerControlSetEnabled)(void*, int)`
- L311 — `void* GetPlayerControl()`
- L335 — `bool SetPlayerInputEnabled(bool enabled, bool armAutoRestore)`
- L368 — `int GetPartyMembers(uint32_t* outHandles, int maxCount)`
- L398 — `void* GetServerPartyTable()`
  note: resolves through CServerExoApp facade → internal at +0x4 to reach party_table at +0x1b770
- L420 — `namespace { ... }` (PFN typedefs for party thiscalls)
- L428 — `bool PartyTableIsNPCAvailable(int npcSlot)`
- L441 — `bool PartyTableIsNPCSelectable(int npcSlot)`
- L462 — `static const char* const kCompanionNamesBySlot[kPartyRosterSlotCount]`
  note: hardcoded fallback for companions not in current module; order matches engine roster index (Bastila=0..Zaalbar=8)
- L474 — `bool GetPartyNpcNameForSlot(int npcSlot, char* outBuf, size_t bufSize)`
- L516 — `void TickPlayerInputRestore()`
