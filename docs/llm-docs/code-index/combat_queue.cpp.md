# combat_queue.cpp (548 lines)

Implements the Phase 3 combat action-queue submenu. Walks CSWSCombatRound.actions
(CExoLinkedList) for every party member, builds a flat Row list, and handles
Up/Down/Enter/Esc navigation. Tail-only remove via RemoveLastAction.

## Declarations (in source order)

- L21 — `namespace acc::combat::queue`
- L27 — `typedef int (__thiscall* PFN_RemoveLastAction)(void* combatRound)`
- L29 — `constexpr uintptr_t kAddrCombatRoundRemoveLastAction`
- L33 — `void* ReadCombatRound(void* serverCreature)`
  note: reads CSWSCreature.combat_round @+0x9c8; SEH-guarded
- L57 — `bool ReadNodeActionType(void* node, unsigned char& outType)`
  note: reads action_type byte via node->data; returns false on null data or fault
- L72 — `int CountQueueEntries(void* combatRound)`
  note: walks linked list; skips 0xFF placeholder head nodes; returns real queued count
- L102 — `void* GetQueueAction(void* combatRound, int index)`
  note: returns CSWSCombatRoundAction* at 0-based index (skipping 0xFF placeholders); nullptr if out of range
- L155 — `acc::strings::Id VerbForActionType(unsigned char actionType)`
  note: maps action_type byte to a localized verb string ID; derived from GetActionIcon @0x686fb0 decompile
- L168 — `bool ReadActionFields(void* action, unsigned char& outType, uint32_t& outTarget)`
- L194 — `struct Row`
  note: one entry in the flat queue list; carries creature, combatRound, perCreatureIdx/Count for tail-remove matching, and charName
- L207 — `constexpr int kMaxRows`
- L207 — `struct State`
  note: full submenu state: active flag, focusIdx, count, Row array
- L214 — `State g_state`
- L222 — `void ResolveMemberName(uint32_t handle, bool isPC, char* outBuf, size_t bufSize)`
  note: PC slot (index 0) falls back to GetPlayerCharacterName; others use GetObjectDisplayNameByHandle
- L241 — `int BuildRows()`
  note: rebuilds g_state.rows from live party engine state; fallback to leader-only when party table unreadable
- L305 — `void SpeakRow(int idx)`
  note: speaks "charName verb target, N von M" for the focused row
- L349 — `bool RemoveRow(int idx)`
  note: tail-only — only removes when perCreatureIdx == perCreatureCount-1; logs non-tail attempts as unimplemented
- L376 — `bool ClearAllRows()`
  note: iterates rows back-to-front calling RemoveLastAction on each; returns true if any succeeded
- L398 — `bool IsActive()`
- L400 — `void ForceDisarm(const char* reason)`
- L409 — `bool Open()`
  note: builds rows; speaks count + first row; returns false if empty
- L432 — `bool HandleInputEvent(int code, int value)`
  note: Up/Down navigates; Enter = tail-remove (Shift+Enter = clear-all); Esc = close; rebuilds rows on every press
- L524 — `void Tick()`
  note: auto-disarms when queue drains between ticks; obeys module-load latch
- L538 — `void PollWin32Hotkey()`
  note: opens on Action::CombatQueueOpen (Shift+H); self-gates on GetPlayerPosition
