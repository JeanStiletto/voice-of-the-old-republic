# engine_player.h (183 lines)

Player state readers — position, facing, area. SEH-guarded. Documents the full chain: AppManager → +0x4 CClientExoApp → GetPlayerCreature @0x5ed540 → CSWCCreature* → server_object @+0xf8 → CSWSObject* → position @+0x90/orientation @+0x9c. Also covers party table, CSWPlayerControl, and NPC roster.

## Declarations (in source order)

- L26 — `namespace acc::engine`
- L28 — `bool GetPlayerPosition(Vector& out)`
- L31 — `bool GetPlayerFacing(Vector& out)`
  note: 2D unit vector; engine zeroes z on object facing
- L35 — `bool GetPlayerYawDegrees(float& out)`
  note: 0°=+X=east, CCW positive; false when no player loaded or heading degenerate
- L38 — `void* GetPlayerArea()`
- L47 — `bool GetCameraPosition(Vector& out)`
  note: orbital camera world position; chain through +0x18 CSWCModule + +0x40 Camera + +0x7C Vector
- L51 — `void* GetPlayerServerCreature()`
- L64 — `bool GetPlayerCharacterName(char* outBuf, size_t bufSize)`
  note: PC chargen name from CClientExoAppInternal::player_character_name, not CSWSCreatureStats (which is empty for vanilla PC)
- L67 — `void* GetClientLeader()`
  note: Tab-cycles; reflects the current leader via CClientExoApp::GetPlayerCreature
- L84 — `bool GetActiveLeaderName(char* outBuf, size_t bufSize)`
  note: three-path resolution: GetObjectDisplayNameByHandle → stats.first_name → PC chargen slot; MUST be gated on GetPlayerPosition to avoid /GS crash during chargen→world transient
- L98 — `bool SetPlayerInputEnabled(bool enabled, bool armAutoRestore = true)`
  note: wraps CSWPlayerControl::SetEnabled @0x006792e0; armAutoRestore=false for sustained disables (view mode) where caller owns re-enable
- L101 — `void TickPlayerInputRestore()`
- L110 — `int GetPartyMembers(uint32_t* outHandles, int maxCount)`
- L114 — `void* GetServerPartyTable()`
- L118 — `bool PartyTableIsNPCAvailable(int npcSlot)`
- L122 — `bool PartyTableIsNPCSelectable(int npcSlot)`
- L126 — `bool GetPartyNpcNameForSlot(int npcSlot, char* outBuf, size_t bufSize)`
  note: walks GetNPCObject → display-name accessor; falls back to hardcoded roster table when companion not in current module
- L131 — `constexpr uintptr_t kAddrAppManagerPtr           = 0x007A39FC`
- L132 — `constexpr size_t    kAppManagerClientAppOffset   = 0x4`
- L135 — `constexpr uintptr_t kAddrGetPlayerCreature = 0x005ED540`
- L136 — `constexpr uintptr_t kAddrCSWSObjectGetArea = 0x004CB120`
- L139 — `constexpr size_t kClientObjectServerObjectOffset = 0xf8`
- L141 — `constexpr size_t kServerObjectPositionOffset    = 0x90`
- L142 — `constexpr size_t kServerObjectOrientationOffset = 0x9c`
- L145 — `constexpr size_t kClientExoAppInternalOffset = 0x4`
- L148 — `constexpr size_t kClientAppPlayerControlOffset = 0x2a0`
- L150 — `constexpr uintptr_t kAddrCSWPlayerControlSetEnabled = 0x006792E0`
- L152 — `constexpr uintptr_t kAddrCClientExoAppGetPlayerCharacterName = 0x005EDAB0`
- L165 — `constexpr size_t    kAppManagerServerOffsetPlayer  = 0x8`
- L166 — `constexpr size_t    kServerExoAppInternalOffset    = 0x4`
- L167 — `constexpr size_t    kServerInternalPartyTableOffset = 0x1b770`
  note: party_table in INTERNAL at +0x1b770, NOT in the public facade; legacy offset kServerExoAppPartyTableOffset is an alias
- L171 — `constexpr size_t    kPartyTableNumMembersOffset    = 0x0`
- L172 — `constexpr size_t    kPartyTableMemberIdsOffset     = 0x4`
- L173 — `constexpr int       kPartyTableMaxMembers          = 11`
- L177 — `constexpr uintptr_t kAddrCSWPartyTableGetIsNPCAvailable     = 0x005636B0`
- L178 — `constexpr uintptr_t kAddrCSWPartyTableGetNPCSelectability   = 0x005637C0`
- L179 — `constexpr uintptr_t kAddrCSWPartyTableGetNPCObject          = 0x00564700`
- L182 — `constexpr int kPartyRosterSlotCount = 9`
