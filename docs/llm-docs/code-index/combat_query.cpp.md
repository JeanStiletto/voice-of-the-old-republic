# combat_query.cpp (916 lines)

Implements Phase 2A (PC stat block + leader-change announce), Phase 2B (target combat
brief enrichment for Q/E cycle), and Phase 2C (Shift+H Examine + bare-H self-status).
All paths read-only; no engine re-entry beyond documented accessors.

## Declarations (in source order)

- L25 — `namespace acc::combat::query`
- L34 — `struct StatSnap`
  note: snapshot of one creature's stats for Phase 2A; fpMax stays 0 (no clean accessor)
- L45 — `typedef int (__thiscall* PFN_GetIntThiscall)(void* this_)`
- L46 — `typedef int (__thiscall* PFN_GetIntStatsThiscall)(void* this_)`
- L47 — `typedef int (__thiscall* PFN_GetIntThisInt)(void* this_, int arg)`
- L50 — `void* ReadCreatureStats(void* serverCreature)`
  note: reads CSWSCreatureStats* via +0xa74 (kCreatureStatsPtrOffset)
- L62 — `int CallIntAccessor(void* this_, uintptr_t addr)`
- L81 — `int CallIntAccessorArg(void* this_, uintptr_t addr, int arg)`
  note: required for engine getters with __thiscall(this, int) signature; using PFN_GetIntThiscall silently reads garbage from caller frame
- L115 — `constexpr size_t kClientCreatureLvlUpStatsOffset`
- L116 — `constexpr size_t kClientStatsCurrentHpOffset`
- L117 — `constexpr size_t kClientStatsMaxHpOffset`
- L119 — `int ReadCurrentHpFromClient(void* clientLeader)`
  note: reads pregame_current_hp @+0x4c on CSWCLevelUpStats; client-side path avoids server-accessor is_pc gate that returns garbage for companions
- L139 — `int ReadMaxHpFromClient(void* clientLeader)`
  note: reads max_hit_points @+0x4e; same client-side path as ReadCurrentHpFromClient
- L161 — `int ReadFeatCount(void* stats)`
  note: direct CExoArrayList.size read on CSWSCreatureStats.feats; no engine call
- L182 — `bool ReadEquippedItemName(void* serverCreature, size_t slotHandleOffset, const char* slotLabel, char* outBuf, size_t outBufSize)`
  note: walks serverCreature -> inventory -> slot handle -> GetObjectDisplayNameByHandle; sentinels 0/0xFFFF/0x7F00 treated as empty
- L224 — `float ComputePlayerDistanceMeters(void* targetObject)`
  note: 2D horizontal distance (z ignored); returns -1.0f on position-read failure
- L238 — `struct BriefBuf`
  note: appendable formatter with saturation; used to chain optional suffix clauses
- L244 — `void BriefAppend(BriefBuf& b, const char* fmt, ...)`
- L259 — `void ReadAttrTotals(void* stats, int outAttrs[6])`
  note: reads 6 attribute bytes @+0x34..+0x39 (STR DEX CON INT WIS CHA)
- L278 — `int ReadEffectCount(void* serverObject)`
  note: reads CExoArrayList size from CSWSObject.effects @+0x124; cheap existence check
- L302 — `bool BuildEffectsSummary(void* serverObject, char* outBuf, size_t outBufSize)`
  note: produces comma-joined deduped mapped-type-only effect names; capped at 5; unmapped types skipped to avoid "Effect #N" noise
- L351 — `int ReadDamageLevelDirect(void* serverObject)`
  note: calls CSWSObject::GetDamageLevel @0x4cb020; returns 0..5 wound bucket (healthy/light/wounded/badly/dying/dead)
- L364 — `acc::strings::Id DamageLevelStringIdFor(int level)`
- L377 — `bool ReadStatSnap(void* serverCreature, StatSnap& out)`
  note: aggregates all stat fields into StatSnap; HP via client-side path, rest via server accessors
- L413 — `bool SpeakSelectedPcStatBlock()`
  note: Phase 2A — interrupt-speaks leader name + HP/FP/AC/attrs/saves/alignment/effects count
- L482 — `void TickLeaderChangeAutoAnnounce()`
  note: Phase 2A tick — polls leader name; speaks name only on change (stat block deferred pending accessor validation); suppresses first observation
- L538 — `bool BuildTargetCombatBrief(void* targetServerObject, const char* targetName, char* outBuf, size_t outBufSize)`
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
  note: Win32 poll for Shift+H; self-gates on GetPlayerPosition
- L824 — `void SpeakSelfStatus()`
  note: bare-H — reads client HP, BuildEffectsSummary, equipped weapons; falls back to cur-only phrase when max=0
- L890 — `void PollWin32SelfStatusHotkey()`
  note: Win32 poll for H; gates on player loaded + IsForegroundUiBlocking
