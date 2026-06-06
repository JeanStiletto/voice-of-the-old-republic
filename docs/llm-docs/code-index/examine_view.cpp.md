# examine_view.cpp (865 lines)

Implementation of the examine view. Builds a flat pre-composed row array from
creature fields (name, faction, condition, HP, level, distance, status flags,
equipment slots, effects, feats) and serves them on Up/Down navigation.

## Declarations (in source order)

- L18 — `namespace acc::examine_view`
- L20 — `namespace` (anonymous)
- L25 — `constexpr int kMaxRows`
- L27 — `struct State`
  note: holds active flag, focus index, row array, and cached target handle + obj ptr
- L36 — `State g_state`
- L38 — `typedef void* (__thiscall* PFN_GetFeat)(void* rules, unsigned short featIdx)`
- L39 — `typedef void* (__thiscall* PFN_GetFeatNameText)(void* feat, void* outExoString)`
- L44 — `struct ExoStringRaw`
- L49 — `void* GetCSWRules()`
- L59 — `bool ResolveFeatName(unsigned short featIdx, char* outBuf, size_t outBufSize)`
  note: SEH-guarded walk through CSWRules::GetFeat vtable; leaks CExoString heap alloc to avoid cross-DLL CRT mismatch
- L96 — `const char* EffectNameEn(int type)`
- L170 — `const char* EffectNameDe(int type)`
- L244 — `uint32_t ReadLastTargetHandle()`
  note: forward declaration; defined at L523
- L246 — `bool IsSentinel(uint32_t handle)`
- L250 — `void* ReadCreatureStats(void* serverCreature)`
- L261 — `int ReadHpCurrent(void* obj)`
  note: reads short at kObjectHitPointsOffset (client-side CSWCCreatureStats, not server-side accessor)
- L272 — `typedef int (__thiscall* PFN_GetIntThis)(void* this_)`
- L273 — `typedef int (__thiscall* PFN_GetIntThisInt)(void* this_, int arg)`
- L275 — `int CallIntThis(void* this_, uintptr_t addr)`
- L286 — `int CallIntThisInt(void* this_, int arg, uintptr_t addr)`
- L296 — `int ReadHpMax(void* serverCreature)`
  note: calls GetMaxHitPoints(param_1=1) — includes Toughness and class HP totals
- L304 — `int ReadLevel(void* serverCreature)`
- L313 — `int ReadDamageLevel(void* obj)`
- L319 — `bool ReadDeadFlag(void* serverCreature)`
- L324 — `bool ReadInvisibleFlag(void* serverCreature)`
- L329 — `bool ReadBlindFlag(void* serverCreature)`
- L334 — `acc::strings::Id DamageLevelStringId(int level)`
- L347 — `int ReadFactionId(void* serverCreature)`
- L361 — `acc::strings::Id FactionWordIdFor(int factionId)`
  note: maps raw faction IDs 0-17 to Friendly/Hostile/Neutral string IDs; unlisted IDs default to Neutral
- L385 — `int Read2DDistanceMeters(void* obj)`
- L401 — `bool ReadEquippedItemNameAtSlot(void* serverCreature, size_t slotOffset, char* outBuf, size_t outBufSize)`
  note: duplicated from combat_query to avoid header cycle between the two TUs
- L431 — `int AppendEffectRows(void* serverObject, char rows[][192], int& outIdx, int rowCap)`
  note: walks CSWSObject.effects CExoArrayList; caps at 64 entries; partial data is acceptable
- L470 — `int AppendFeatRows(void* serverCreature, char rows[][192], int& outIdx, int rowCap)`
  note: walks CSWSCreatureStats.feats ushort array via CSWRules::GetFeat for localized names
- L508 — `typedef uint32_t (__thiscall* PFN_GetLastTarget)(void* this_)`
- L509 — `constexpr uintptr_t kAddrCClientExoAppGetLastTargetLocal`
- L511 — `void* GetClientExoAppLocal()`
- L523 — `uint32_t ReadLastTargetHandle()`
- L539 — `int BuildRows()`
  note: (re)builds full row array for cached target handle; returns 0 if target unresolvable (view should disarm)
- L708 — `void SpeakRow(int idx)`
- L719 — `const char* EffectName(int type)`
  note: dispatches to EffectNameDe or EffectNameEn based on GetLanguage
- L726 — `bool IsActive()`
- L728 — `void ForceDisarm(const char* reason)`
- L739 — `bool Open()`
- L787 — `bool HandleInputEvent(int code, int value)`
- L836 — `void Tick()`
  note: only self-disarms; no narration here — all speech driven from HandleInputEvent
- L847 — `void PollWin32Hotkey()`
  note: toggle: pressing Ö while active closes the view
