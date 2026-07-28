# examine_view.cpp (1530 lines)

Implements the synthetic "Ö examine" list view: a keyboard-navigable list of
pre-composed target-info rows (name, faction, condition, HP, level, distance,
equipment, effects, feats), since KOTOR 1's native CSWGuiExamine is a plain
TLK message box. The bulk of the file is per-language (EN/DE/FR/IT/ES)
lookup tables mapping EFFECT_TYPES and effecticon.2da row ids to localized
display names (sighted-icon parity), selected by acc::strings::GetLanguage().
BuildRows() walks CSWSObject/CSWSCreatureStats fields directly (SEH-guarded)
to compose rows; Open/HandleInputEvent/Tick/PollWin32Hotkey drive the
arm/step/close lifecycle and route through engine_subscreen's
Begin/EndOverlayPause to freeze the world while open. Shares helpers
(ResolveFeatName, ResolveSpellName, IsHostileCreature, EffectName,
EffectIconName) with combat_query for consistent Q/E brief wording.

## Declarations (in source order)

- L23 — `namespace acc::examine_view`
- L30 — `constexpr int kMaxRows = 64`
- L32 — `struct State` — active/focusIdx/rowCount/rows[64][192]/targetHandle/targetObj
- L41 — `State g_state`
- L43-52 — `PFN_GetFeat/GetFeatNameText`, `struct ExoStringRaw`
- L54 — `void* GetCSWRules()`
- L67-435 — `EffectNameEn/De/Es/Fr/It(int type)` — EFFECT_TYPES → localized name tables
- L452-775 — `EffectIconNameEn/De/Fr/It/Es(int id)` — effecticon.2da row → localized name tables
  note: names verified against spells.2da strrefs in all five locale TLKs (2026-07-17); rows 0/60/61 (alignment gauges) unmapped.
- L777 — `uint32_t ReadLastTargetHandle()` (forward decl)
- L779 — `bool IsSentinel(uint32_t handle)`
- L783 — `void* ReadCreatureStats(void* serverCreature)`
- L794 — `int ReadHpCurrent(void* obj)`
- L806-827 — `PFN_GetIntThis/GetIntThisInt`, `CallIntThis`, `CallIntThisInt`
- L829 — `int ReadHpMax(void* serverCreature)`
- L837 — `int ReadLevel(void* serverCreature)`
- L846 — `int ReadDamageLevel(void* obj)`
  note: masks to low byte — buckets 0..3 carry flag garbage in upper bytes.
- L856 — `bool ReadDeadFlag(void* serverCreature)`
- L861-869 — note-only: no ReadInvisibleFlag — engine's GetInvisible mutates perception state (SANCTUARY save roll), not a pure read.
- L871 — `bool ReadBlindFlag(void* serverCreature)`
- L876 — `acc::strings::Id DamageLevelStringId(int level)`
- L889 — `int ReadFactionId(void* serverCreature)`
- L903 — `acc::strings::Id FactionWordIdFor(int factionId)` — faction table shared with IsHostileCreature
- L927 — `int Read2DDistanceMeters(void* obj)`
- L943 — `bool ReadEquippedItemNameAtSlot(void*, size_t slotOffset, char*, size_t)`
  note: duplicated from combat_query to avoid a header cycle.
- L973 — `int AppendEffectRows(void* serverObject, char rows[][192], int&, int rowCap)`
- L1012 — `int AppendFeatRows(void* serverCreature, char rows[][192], int&, int rowCap)`
- L1050-1075 — `PFN_GetLastTarget`, `kAddrCClientExoAppGetLastTargetLocal`, `GetClientExoAppLocal()`, `ReadLastTargetHandle()`
- L1081 — `int BuildRows()` — (re)builds the full row list for the cached target
- L1245 — `void SpeakRow(int idx)`
- L1262 — `bool IsHostileCreature(void* serverObject)` — public; shared with stealth_watch
- L1279 — `bool ResolveSpellName(int spellId, char*, size_t)` — public; CSWSpellArray::GetSpell path
- L1329 — `bool ResolveFeatName(unsigned short featIdx, char*, size_t)` — public; CSWRules::GetFeat path
  note: leaks the heap-allocated CExoString on purpose — cross-DLL dtor risks a CRT mismatch.
- L1362 — `const char* EffectName(int type)` — public language dispatcher
- L1375 — `const char* EffectIconName(int iconId)` — public language dispatcher
- L1388 — `bool IsActive()`
- L1390 — `void ForceDisarm(const char* reason)`
- L1402 — `bool Open()`
- L1451 — `bool HandleInputEvent(int code, int value)` — Up/Down/Enter/Esc, press-edge only
- L1501 — `void Tick()` — self-disarms if player position becomes unresolvable
- L1512 — `void PollWin32Hotkey()` — Ö toggles open/close
