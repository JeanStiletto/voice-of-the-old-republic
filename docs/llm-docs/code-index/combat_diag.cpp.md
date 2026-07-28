# combat_diag.cpp (355 lines)

Implements the state-bit snapshot/delta probe and the four hook-driven event loggers declared in combat_diag.h. `ReadState` pulls combat-mode bit, inner queue size (combat_round.actions), outer queue size (CSWSObject.action_nodes @+0xfc — the one the sighted strip UI reflects), auto-paused, pause-state(0), and the main_interface target handle, each SEH-guarded independently. The `OnCombatRoundAddAction` detour additionally calls into `combat_queue::OnEngineActionAdded` to drive the authoritative "X, Platz N" / "Warteschlange voll" announce, since this hook fires once per genuine add at function entry (never races the queue drain or under-counts on key auto-repeat, unlike the old rising-edge poll).

## Declarations (in source order)

- L15 — `namespace acc::combat_diag`
- L22 — `constexpr size_t kCSWCCreatureCombatModeOffset = 0x440`
- L25-26 — `kAddrCClientExoAppGetAutoPaused`, `kAddrCClientExoAppGetPauseState`
- L30-31 — `kGuiInGameMainInterfaceOffset = 0x90`, `kMainInterfaceTargetHandleOff = 0x64`
- L36 — `uint8_t ReadCombatModeBit(void* clientLeader)`
- L47 — `int ReadQueueSize(void* serverCreature)`
  note: fast path reads internal.count directly, bypassing the node walker
- L84 — `int ReadOuterQueueSize(void* serverCreature)`
  note: CExoLinkedList<T> is INLINE at CSWSObject+0xfc, not a pointer — first deref is already the internal ptr
- L104 — `void* GetExoApp()`
- L116 — `int CallGetAutoPaused(void* exoApp)`
- L127 — `int CallGetPauseState(void* exoApp)`
- L138 — `uint32_t ReadMainInterfaceTarget(void* exoApp)`
- L160 — `struct State` (cm/qs/oq/ap/ps/tgt)
- L171 — `void ReadState(State& s)`
- L192 — `State g_last`, `bool g_armed`
- L197 — `void Tick()`
  note: gated on GetPlayerPosition; INIT line on first arm, DELTA lines thereafter
- L242 — `void LogPreFire(const char* label)`
- L250 — `void LogPostFire(const char* label)`
- L264 — `void* GetPlayerCombatRound()`
- L276 — `const char* RoleTag(void* combatRound)`
  note: "PLAYER" vs "other" — distinguishes the user's combat round from companion/enemy rounds sharing the same hook
- L288 — `extern "C" void __cdecl OnCombatRoundAddAction(...)`
  note: derefs esp+N stack slots per the documented LEA-vs-MOV bug; reads action_type @+0x10; forwards to combat::queue::OnEngineActionAdded
- L329 — `extern "C" void __cdecl OnCombatRoundRemoveAllActions(void*)`
- L336 — `extern "C" void __cdecl OnCombatRoundSetCurrentAction(void*, void*)`
- L350 — `extern "C" void __cdecl OnCombatRoundRemoveLastAction(void*)`
