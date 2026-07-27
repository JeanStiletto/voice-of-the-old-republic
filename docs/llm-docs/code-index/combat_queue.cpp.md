# combat_queue.cpp (834 lines)

Implements the Shift+H queue submenu and the live fire-announce. `BuildRows` sources the controlled creature's queue via `GetPlayerServerCreature` FIRST (party-table walk resolves only NPC roster slots — the PC never appears there), then appends every other party member's queue via `GetPartyMembers`. Each queued action is decoded via `VerbForActionType` (enum confirmed by decompiling `CSWGuiMainInterface::GetActionIcon`) with the specific ability name substituted for type 9/10/11 (force power / item / feat) via `BuildActionLabel`. `RemoveRow` is tail-only (`CSWSCombatRound::RemoveLastAction` is the engine's only public per-round primitive) — non-tail remove is an unimplemented "open" item. `OnEngineActionAdded` uses a `kUserAddWindowMs=250` freshness-window latch (`ArmUserQueueAdd`) to distinguish a user press from the engine's own auto-queued leader attack sharing the same `AddAction` path/round.

## Declarations (in source order)

- L31 — `namespace acc::combat::queue`
- L37 — `typedef int (__thiscall* PFN_RemoveLastAction)(void* combatRound)`
- L39-40 — `kAddrCombatRoundRemoveLastAction`
- L43 — `void* ReadCombatRound(void* serverCreature)`
- L65 — `constexpr int kMaxQueueWalk = 64`
- L67 — `bool ReadNodeActionType(void* node, unsigned char& outType)`
- L82 — `int CountQueueEntries(void* combatRound)`
  note: skips the engine's leading 0xFF placeholder node (validated 2026-05-10)
- L118 — `void* GetQueueAction(void* combatRound, int index)`
- L175 — `acc::strings::Id VerbForActionType(unsigned char actionType)`
  note: enum decoded from CSWGuiMainInterface::GetActionIcon @0x686fb0's case 0xc switch
- L188 — `bool ReadActionFields(void* action, unsigned char& outType, uint32_t& outTarget)`
- L210 — `unsigned short ReadActionFeatId(void* action)`
- L224 — `int ReadActionSpellId(void* action)`
- L238 — `uint32_t ReadActionItemHandle(void* action)`
- L260 — `struct Row` (creature/combatRound/perCreatureIdx/perCreatureCount/charName)
- L270 — `constexpr int kMaxRows = 32`
- L273 — `struct State` (active/focusIdx/count/rows[]) / L280 `State g_state`
- L288 — `void ResolveMemberName(uint32_t handle, char* outBuf, size_t bufSize)`
- L300 — `int BuildRows()`
  note: PC's queue sourced separately from GetPlayerServerCreature; fixed 2026-06-07 bug where party-walk alone left it empty
- L380 — `void BuildActionLabel(void* action, char* out, size_t n)`
  note: shared by SpeakRow and OnEngineActionAdded so both name an entry identically
- L422 — `int RawRoundCount(void* combatRound)`
  note: same field CountPlayerEntries reads, but on an arbitrary round
- L444 — `void SpeakRow(int idx)`
- L494 — `bool RemoveRow(int idx)`
  note: tail-only; non-tail remove logs and returns false
- L521 — `bool ClearAllRows()`
  note: iterates rows back-to-front so each per-creature tail-remove matches RemoveLastAction
- L543 — `int CountPlayerEntries()`
  note: deliberately does NOT filter the 0xFF placeholder — must match the engine's raw cap-check count
- L587 — `DWORD g_userAddArmTick`, `constexpr DWORD kUserAddWindowMs = 250`
- L595 — `void ArmUserQueueAdd()`
- L597 — `void OnEngineActionAdded(void* combatRound, void* action)`
  note: attribution gate — only adds within 250ms of a user press are announced; engine auto-attacks stay silent
- L650 — `int g_prePressDepth` / L653 `void ReportPrePressDepth()` / L657 `int GetPrePressDepth()`
  note: consume-on-read so a press that skipped ReportPrePressDepth reads -1, not a stale value
- L668 — `bool IsActive()`
- L670 — `void ForceDisarm(const char* reason)`
- L680 — `bool Open()`
- L704 — `bool HandleInputEvent(int code, int value)`
  note: Up/Down clamp (no wrap); Enter removes-and-reannounces; Shift+Enter clears all; Esc re-announces the unified menu if still open underneath instead of speaking "closed"
- L810 — `void Tick()`
- L824 — `void PollWin32Hotkey()`
