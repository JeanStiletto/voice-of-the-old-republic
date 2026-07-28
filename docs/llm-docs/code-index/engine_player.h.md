# engine_player.h (246 lines)

Player state readers — position, facing, area, camera, leader identity,
input-enable/disable, and the server party table. SEH-guarded throughout.
Documents the canonical chain: AppManager → +0x4 CClientExoApp →
GetPlayerCreature @0x5ed540 → CSWCCreature* → server_object @+0xf8 →
CSWSObject* → position @+0x90 / orientation @+0x9c (server layout is
authoritative; client +0x24/+0x30 is a parallel cache).

## Declarations (in source order)

- L27 — `namespace acc::engine`
- L29 — `bool GetPlayerPosition(Vector& out)`
- L32 — `bool GetPlayerFacing(Vector& out)`
  note: 2D unit vector; engine zeroes z on object facing.
- L36 — `bool GetPlayerYawDegrees(float& out)`
  note: 0°=+X=east, CCW positive, [0,360); false on degenerate heading.
- L39 — `void* GetPlayerArea()`
- L48 — `bool GetCameraPosition(Vector& out)`
  note: chain through +0x18 CSWCModule + +0x40 Camera + +0x7C Vector (Gob embedded at Camera+0x04, position at Gob+0x78).
- L59 — `bool GetCameraYawRadians(float& outRad)`
  note: derives yaw from the Camera+0x88 orientation quaternion (w,x,y,z layout); matches the position-derived heading and stays valid when the camera sits directly above the player (where position-derived degenerates).
- L63 — `void* GetPlayerServerCreature()`
- L76 — `bool GetPlayerCharacterName(char* outBuf, size_t bufSize)`
  note: reads CClientExoAppInternal::player_character_name (+0x294) — the PC's CSWSCreatureStats.first_name is empty in vanilla saves, so the generic creature-name path can't be used for the PC.
- L80 — `void* GetClientLeader()`
  note: Tab-cycled active leader; mirrors GetPlayerServerObject's chain but stops at the client pointer.
- L96 — `int SetLeaderQueueModeBit(int on)`
  note: writes CSWCCreature.field200_0x440 bit 0 (replace-vs-append action-queue discriminator); returns the PREVIOUS bit value for restore, -1 on failure.
- L105 — `bool IsAnyPartyMemberInCombat()`
  note: ORs the per-creature combat bit across the whole party — the global CClientExoApp::GetCombatMode only reflects the CONTROLLED leader and re-syncs on every Tab, so it falsely reads "combat ended" when switching to a not-yet-engaged member mid-fight.
- L121 — `bool GetActiveLeaderName(char* outBuf, size_t bufSize)`
  note: three-path resolution — GetObjectDisplayNameByHandle → server-object GetObjectName → stats.first_name → PC chargen slot; MUST be gated on GetPlayerPosition (Path 1 trips a /GS __fastfail on the PC handle during the chargen→world transient otherwise).
- L138 — `bool SetPlayerInputEnabled(bool enabled, bool armAutoRestore = true)`
  note: wraps CSWPlayerControl::SetEnabled @0x006792e0; armAutoRestore=true arms the queue-watched auto-restore (autowalk shape), false leaves the timer inert (view-mode shape, caller owns re-enable).
- L142 — `void TickPlayerInputRestore()`
  note: queue-drain/ceiling-driven; cheap when idle.
- L146 — `void TickActionQueueDiag()`
- L150 — `int GetPlayerActionQueueDepth()`
  note: -1 unreadable this tick, 0 drained, >0 pending — reads CSWSObject.action_nodes.
- L162 — `int GetPartyMembers(uint32_t* outHandles, int maxCount)`
  note: pt_member_ids[] holds NPC roster SLOT INDICES (0..8), not handles — each resolved to a live handle via CSWPartyTable::GetNPCObject.
- L166 — `void* GetServerPartyTable()`
- L171 — `bool GetSoloMode()`
  note: CSWPartyTable.pt_solomode @+0x190.
- L175 — `bool PartyTableIsNPCAvailable(int npcSlot)`
- L179 — `bool PartyTableIsNPCSelectable(int npcSlot)`
- L190 — `bool GetPartyNpcNameForSlot(int npcSlot, char* outBuf, size_t bufSize)`
  note: client universal-name accessor first, then server stats.first_name/tag (covers the Endar Spire slot-0 occupant Trask — a live server object invisible to the client namespace), then a hardcoded fixed-roster table.
- L195 — `constexpr uintptr_t kAddrAppManagerPtr = 0x007A39FC`
- L196 — `constexpr size_t kAppManagerClientAppOffset = 0x4`
- L198 — `const uintptr_t kAddrGetPlayerCreature = R(0x005ED540)`
- L199 — `const uintptr_t kAddrCSWSObjectGetArea = R(0x004CB120)`
- L202 — `constexpr size_t kClientObjectServerObjectOffset = 0xf8`
- L204 — `constexpr size_t kServerObjectPositionOffset    = 0x90`
- L205 — `constexpr size_t kServerObjectOrientationOffset = 0x9c`
- L209 — `constexpr size_t kClientExoAppInternalOffset = 0x4`
- L212 — `constexpr size_t kClientAppPlayerControlOffset = 0x2a0`
- L214 — `const uintptr_t kAddrCSWPlayerControlSetEnabled = R(0x006792E0)`
- L217 — `const uintptr_t kAddrCClientExoAppGetPlayerCharacterName = R(0x005EDAB0)`
- L228 — `constexpr size_t kAppManagerServerOffsetPlayer  = 0x8`
- L229 — `constexpr size_t kServerExoAppInternalOffset    = 0x4`
- L230 — `constexpr size_t kServerInternalPartyTableOffset = 0x1b770`
  note: party_table lives in the INTERNAL at +0x1b770 (not the public facade) — earlier walks reading facade+0x1b770 returned garbage (all-1s avail/selectable arrays).
- L234 — `constexpr size_t kPartyTableNumMembersOffset = 0x0`, `kPartyTableMemberIdsOffset` = 0x4, `kPartyTableSoloModeOffset` = 0x190
- L237 — `constexpr int kPartyTableMaxMembers = 11`
- L241 — `const uintptr_t kAddrCSWPartyTableGetIsNPCAvailable = R(0x005636B0)`, `GetNPCSelectability = R(0x005637C0)`, `GetNPCObject = R(0x00564700)`
- L246 — `constexpr int kPartyRosterSlotCount = 9`
