# msg_router.cpp (148 lines)

Message router implementation + AppendToMsgBuffer hook entry. Lazy rule registration on first hook fire (calls acc::combat::RegisterCombatMsgRules). Hook receives CExoString* via ESP+4 stack-slot address (LEA path per KPatchManager LEA bug).

## Declarations (in source order)

- L11 — `namespace acc::msg`
- L13 — `Router& Router::Instance()`
- L18 — `void Router::AddRule(const char* name, RuleFn fn)`
- L29 — `void Router::AddOnUnmatched(OnUnmatchedFn fn)`
- L37 — `void Router::SetLogTag(const char* tag)`
- L41 — `void Router::Dispatch(const char* text)`
- L52 — `void Router::Speak(const char* text)`
- L59 — `void Router::LogRaw(const char* text)`
- L63 — `void Router::LogEmit(const char* subTag, const char* text)`
- L88 — `extern "C" void __cdecl OnAppendToMsgBuffer(void* guiInGame, void* esp_param1_addr)`
  note: hook on CGuiInGame::AppendToMsgBuffer @0x0062b5c0; dereferences esp_param1_addr once to get CExoString*
- L135 — `namespace acc::combat { void RegisterCombatMsgRules(); }` (forward decl)
- L139 — `static void EnsureRulesRegistered()` (anonymous namespace)
