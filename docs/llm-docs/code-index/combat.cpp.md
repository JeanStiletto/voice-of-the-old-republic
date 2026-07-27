# combat.cpp (1491 lines)

Two halves. Phase 1: `TickCombatMode` polls `CClientExoApp::GetCombatMode` with a stability-debounce (mirrors turn_announce), speaking a strong begin/end cue keyed on `partyInCombat` and a subtler "leader at peace while party fights" cue keyed on the leader-only bit; `TickCombatLog` walks `CSWGuiInGameMessages.messages_listbox` for diagnostic logging only (the listbox fills lazily at review-screen-open, so speech stays off there). Phase 2 (from L315 on): a `msg::Router` rule set that parses the engine's German/localised combat-log lines (attack summary → Angriffsstatistik → Abwehrstatistik → Schadensstatistik) into one compact merged line, plus separate coalescing bursts for damage-absorption, blaster-deflection, and per-target ability/grenade/force-power effects (save throws, direct damage, status). Talks to `combat_strings.h` (`acc::combat::loc::Get()`) for all locale anchors/output words, `msg_router.h` for the rule registration, `party_cache.h` for `IsPartyMember`, and `trap_watch.h` for mine-detect line enrichment. Gotcha: outgoing-party misses are suppressed unless the attack used a feat; incoming hits speak victim-led; a status effect on a party member is lifted to the urgent SAPI channel instead of the merged/normal-priority line.

## Declarations (in source order)

- L24 — `namespace acc::combat`
- L38 — `typedef int (__thiscall* PFN_GetCombatMode)(void* this_)`
- L40 — `bool ReadCombatMode(int& outMode)`
- L56 — `constexpr DWORD kCombatModeQuietMs = 200`
- L60 — `bool IsCombatActive()`
- L70 — `bool IsPartyInCombat()`
- L76 — `void TickCombatMode()`
  note: two independent debounced state machines (party-level strong cue, leader-level subtle cue) sharing kCombatModeQuietMs; auto-closes the unified action menu on a genuine encounter end
- L198 — `constexpr size_t kCGuiInGameInGameMessagesOffset = 0x1c`
- L202 — `void* FindInGameMessagesPanel()`
- L215 — `int ReadListBoxRowCount(void* lb)`
- L228 — `void* ReadListBoxRow(void* lb, int i)`
- L243 — `void TickCombatLog()`
  note: diagnostic sanity-check only — speech intentionally off, see file header
- L343 — `struct AttackBlock`
  note: accumulates one attack sequence's summary/angriff/abwehr/schaden fields plus status tail
- L367 — `AttackBlock g_pending`
- L378 — `constexpr DWORD kAbsorbQuietMs = 1500`, `kAbsorbMaxHoldMs = 5000`
  note: widened from 600/2500 (2026-07-17) — autofire volleys were splitting into a spoken line per volley
- L380 — `char g_absorb_namepart[160]`, `g_absorb_suffix[64]`, `int g_absorb_total`, `g_absorb_count`, `DWORD g_absorb_last_tick`, `g_absorb_first_tick`
- L395 — `constexpr DWORD kDeflectQuietMs = 1500`, `kDeflectMaxHoldMs = 5000`, `constexpr int kMaxDeflectSlots = 4`
- L399 — `struct DeflectSlot` / `DeflectSlot g_deflect[kMaxDeflectSlots]`
- L413 — `constexpr DWORD kAbilityWindowMs = 5000`, `kFxQuietMs = 500`, `kFxMaxHoldMs = 2500`, `constexpr int kMaxFx = 12`
- L418 — `struct LastAbility` / `LastAbility g_lastAbility`
- L426 — `struct EffectTarget` / `EffectTarget g_fx[kMaxFx]`
- L443 — `EffectTarget* FxFind(const char* target)`
- L452 — `EffectTarget* FxAlloc(const char* target)`
  note: attributes the most recent "benutzt" ability if still within kAbilityWindowMs
- L474 — `bool MsgStartsWith(const char* s, const char* p)`
- L479 — `void CopyRange(char* dst, size_t cap, const char* start, const char* end)`
- L488 — `bool ParseSummary(const char* text, AttackBlock& b)`
- L553 — `bool ParseAngriff(const char* text, AttackBlock& b)`
- L654 — `bool ParseAbwehr(const char* text, AttackBlock& b)`
- L692 — `bool ParseSchaden(const char* text, AttackBlock& b)`
  note: builds two parallel outputs — full-breakdown and short-form ("6 Energie + 5 Bonus")
- L811 — `void BuildCompact(const AttackBlock& b, char* out, size_t cap)`
- L883 — `void BuildResultTail(const AttackBlock& b, char* tail, size_t cap)`
- L899 — `void BuildOutgoingLine(const AttackBlock& b, char* out, size_t cap)`
- L920 — `void BuildIncomingLine(const AttackBlock& b, char* out, size_t cap)`
- L941 — `bool MaybeSpeakStatusUrgent(const char* victim, const char* status)`
  note: moves (not duplicates) a party-member status word onto the urgent SAPI channel
- L963 — `void FlushPending()`
  note: party attack (feat or hit) → actor-led; incoming hit on party → victim-led; else suppressed (log-only)
- L994 — `bool RuleSummary`, L1002 `RuleAngriff`, L1007 `RuleAbwehr`, L1012 `RuleSchaden` — router rules for the 4-line attack sequence
- L1026 — `bool RuleAuswirkung(const char* text)`
  note: attaches status to an existing effect slot, or drops the duplicate already folded into the attack line
- L1048 — `bool RuleAbilityUse(const char* text)`
- L1081 — `bool RuleSaveThrow(const char* text)`
- L1108 — `bool RuleDirectDamage(const char* text)`
- L1144 — `void BuildEffectLine(const EffectTarget& e, char* out, size_t cap)`
- L1194 — `void FlushEffect(EffectTarget& e)`
  note: suppresses only positively-identified NPC-vs-NPC effects; unknown caster defaults to announce
- L1220 — `void FlushAbsorb()`
- L1237 — `bool RuleAbsorb(const char* text)`
- L1290 — `void FlushDeflect(DeflectSlot& s)`
- L1312 — `bool RuleDeflect(const char* text)`
- L1358 — `bool RuleBedrohung(const char* text)`
  note: claimed + suppressed — the crit outcome is already carried by the summary tag
- L1367 — `bool RuleKill(const char* text)`
  note: routed through prism::SpeakUrgent
- L1385 — `bool RuleMineDetect(const char* text)`
  note: correlates trap_watch's fresh-mine record to enrich the line with clock position + distance
- L1433 — `void OnUnmatched(const char*)`
  note: flushes any pending block on an unrecognised boundary line (typically a miss with no Schadensstatistik)
- L1442 — `void RegisterCombatMsgRules()`
- L1463 — `void TickCombatAbsorb()`, L1471 `TickCombatDeflect()`, L1481 `TickCombatEffects()`
