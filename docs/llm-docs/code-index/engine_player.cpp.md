# engine_player.cpp (843 lines)

Implementation of engine_player.h. Owns the player-input-disable/auto-restore
session state machine (queue-drain / grace / stall / ceiling), the party
roster resolution chain, and the multi-path active-leader name resolver.

## Declarations (in source order)

- L14 — `namespace acc::engine`
- L16 — `namespace { ... }` (anonymous, PFN typedefs + GetPlayerServerObject)
- L18 — `typedef void* (__thiscall* PFN_GetPlayerCreature)(void*)`
- L19 — `typedef void* (__thiscall* PFN_CSWSObjectGetArea)(void*)`
- L20 — `typedef void* (__thiscall* PFN_GetPlayerCharacterName)(void*)`
- L30 — `void* GetPlayerServerObject()`
  note: centralises the chain walk so per-field readers pay one SEH frame instead of three.
- L56 — `bool GetPlayerPosition(Vector& out)`
- L69 — `bool GetPlayerFacing(Vector& out)`
- L83 — `bool GetPlayerYawDegrees(float& out)`
- L94 — `void* GetPlayerArea()`
- L106 — `bool GetCameraPosition(Vector& out)`
- L138 — `bool GetCameraYawRadians(float& outRad)`
  note: fwd.x = 2(x*y - z*w), fwd.y = 1 - 2(x*x + z*z); verified against engine Yaw() @0x4a9f40.
- L176 — `void* GetPlayerServerCreature()`
- L180 — `void* GetClientLeader()`
- L196 — `int SetLeaderQueueModeBit(int on)`
- L215 — `bool IsAnyPartyMemberInCombat()`
  note: walks CSWParty::GetCharacter @0x006346C0 over a capped 8-slot scan, ORing field200_0x440 bit 0 across every resolved character.
- L263 — `bool GetActiveLeaderName(char* outBuf, size_t bufSize)`
  note: Path 1 GetObjectDisplayNameByHandle, Path 1b server-object GetObjectName (covers the PC's client 0xffffffff sentinel), Path 2 direct stats.first_name, Path 3 GetPlayerCharacterName; every dead path logged via acclog::Trace (not Write — dedups per-tick spam).
- L408 — `bool GetPlayerCharacterName(char* outBuf, size_t bufSize)`
- L430 — `namespace { ... }` (auto-restore session statics + GetPlayerControl)
- L450 — `constexpr DWORD kQueueGraceMs = 300`, `kStallMs` = 4000, `kUnreadableCeilingMs` = 8000, `kProgressEpsSq` = 0.25f
- L454 — `bool g_disableActive`, `g_sawPending`, `g_haveProgress` + timing statics
  note: g_sawPending latches once a non-empty queue is observed so an early 0-read doesn't trip a premature restore; a repeat disable while a session is active does NOT re-arm (the janicebug livelock guard).
- L463 — `typedef void (__thiscall* PFN_CSWPlayerControlSetEnabled)(void*, int)`
- L468 — `void* GetPlayerControl()`
- L498 — `int GetPlayerActionQueueDepth()`
- L513 — `bool SetPlayerInputEnabled(bool enabled, bool armAutoRestore)`
- L558 — `int GetPartyMembers(uint32_t* outHandles, int maxCount)`
- L601 — `void* GetServerPartyTable()`
  note: AppManager+0x8 → facade → +0x4 internal → +0x1b770; the earlier bug read facade+0x1b770 directly (missing the internal indirection), always landing on a zero byte.
- L623 — `bool GetSoloMode()`
- L635 — `namespace { ... }` (PFN typedefs for party thiscalls)
- L643 — `bool PartyTableIsNPCAvailable(int npcSlot)`
- L656 — `bool PartyTableIsNPCSelectable(int npcSlot)`
- L677 — `static const char* const kCompanionNamesBySlot[9]`
  note: hardcoded fixed-roster fallback in engine slot order (0 Bastila .. 8 Zaalbar) for companions not resolvable via GetNPCObject.
- L689 — `bool GetPartyNpcNameForSlot(int npcSlot, char* outBuf, size_t bufSize)`
- L767 — `void TickPlayerInputRestore()`
  note: state machine — depth>0 tracks progress (no time cap while moving, stall restore after kStallMs of no movement); depth==0 restores on queue-drained or post-grace no-op; depth<0 falls back to the unreadable ceiling.
- L827 — `void TickActionQueueDiag()`
  note: low-volume delta-only diagnostic, gated on GetPlayerPosition (the documented safe window for per-tick PC-slot probes).
