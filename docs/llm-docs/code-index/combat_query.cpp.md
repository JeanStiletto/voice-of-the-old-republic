# combat_query.cpp

Implements leader-change announce, Phase 2B (target combat brief enrichment for Q/E
cycle), and Phase 2C (Ö Examine + bare-H self-status). All paths read-only;
no engine re-entry beyond documented accessors.

## Declarations (in source order)

- `namespace acc::combat::query`
- `typedef int (__thiscall* PFN_GetIntThiscall)(void* this_)`
- `constexpr size_t kClientCreatureLvlUpStatsOffset`
- `constexpr size_t kClientStatsCurrentHpOffset`
- `constexpr size_t kClientStatsMaxHpOffset`
- `int ReadCurrentHpFromClient(void* clientLeader)`
  note: reads pregame_current_hp @+0x4c on CSWCLevelUpStats; client-side path avoids server-accessor is_pc gate that returns garbage for companions
- `int ReadMaxHpFromClient(void* clientLeader)`
  note: reads max_hit_points @+0x4e; same client-side path as ReadCurrentHpFromClient
- `bool ReadEquippedItemName(void* serverCreature, size_t slotHandleOffset, const char* slotLabel, char* outBuf, size_t outBufSize)`
  note: walks serverCreature -> inventory -> slot handle -> GetObjectDisplayNameByHandle; sentinels 0/0xFFFF/0x7F00 treated as empty
- `float ComputePlayerDistanceMeters(void* targetObject)`
  note: 2D horizontal distance (z ignored); returns -1.0f on position-read failure
- `struct BriefBuf`
  note: appendable formatter with saturation; used to chain optional suffix clauses
- `void BriefAppend(BriefBuf& b, const char* fmt, ...)`
- `int ReadEffectCount(void* serverObject)`
  note: reads CExoArrayList size from CSWSObject.effects @+0x124; cheap existence check
- `bool BuildEffectsSummary(void* serverObject, char* outBuf, size_t outBufSize)`
  note: produces comma-joined deduped mapped-type-only effect names; capped at 5; unmapped types skipped to avoid "Effect #N" noise
- `int ReadDamageLevelDirect(void* serverObject)`
  note: calls CSWSObject::GetDamageLevel @0x4cb020; returns 0..5 wound bucket (healthy/light/wounded/badly/dying/dead)
- `acc::strings::Id DamageLevelStringIdFor(int level)`
- `void TickLeaderChangeAutoAnnounce()`
  note: polls leader name; speaks name only on change; suppresses first observation
- `bool BuildTargetCombatBrief(void* targetServerObject, const char* targetName, char* outBuf, size_t outBufSize)`
  note: Phase 2B — Creature-kind only; appends condition+distance+effects+main-hand+off-hand to outBuf
- L636 — `typedef uint32_t (__thiscall* PFN_GetLastTarget)(void* this_)`
- L637 — `constexpr uintptr_t kAddrCClientExoAppGetLastTargetLocal`
- L638 — `void* GetClientExoApp()`
- L649 — `void* GetClientExoAppInternal()`
- L661 — `uint32_t ReadLastTargetHandle()`
- L675 — `void HotkeyShiftH()`
  note: Phase 2C — resolves LastTarget; builds brief via BuildTargetCombatBrief for Creature, plain name for others
- L746 — `void TickExaminePanel()`
  note: logs panel open/close edges; no speech emitted (speech owned by HotkeyShiftH + kExamineSpec)
- L810 — `void PollWin32Hotkey()`
  note: Win32 poll for Ö; self-gates on GetPlayerPosition
- L824 — `void SpeakSelfStatus()`
  note: bare-H — reads client HP, BuildEffectsSummary, equipped weapons; falls back to cur-only phrase when max=0
- L890 — `void PollWin32SelfStatusHotkey()`
  note: Win32 poll for H; gates on player loaded + IsForegroundUiBlocking
