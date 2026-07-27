# msg_router.cpp (185 lines)

Implements `acc::msg::Router` (first-match-wins rule dispatch over engine
message-buffer lines) and owns the `CGuiInGame::AppendToMsgBuffer` hook entry
point (`OnAppendToMsgBuffer`). The hook copies the appended `CExoString` out,
suppresses the full save-load replay flood (engine re-appends the entire
persisted feedback log on load), then dispatches through the Router singleton.
Combat rules (combat.cpp), locked-object bark recall, and Endar Spire tutorial
door guidance register themselves lazily on first fire via
`EnsureRulesRegistered` (this now covers three subsystems, not just combat).

## Declarations (in source order)

- L15-L18 — `Router& Router::Instance()` — function-local static singleton
- L20-L29 — `void Router::AddRule(const char* name, RuleFn fn)` — fixed-size table, logs+drops on overflow
- L31-L37 — `void Router::AddOnUnmatched(OnUnmatchedFn fn)`
- L39-L41 — `void Router::SetLogTag(const char* tag)`
- L43-L52 — `void Router::Dispatch(const char* text)` — LogRaw, first matching rule wins and returns; else runs all on-unmatched then Speak
- L54-L59 — `void Router::Speak(const char* text)` — single seam to `prism::Speak(text, interrupt=false)`
- L61-L68 — `void Router::LogRaw`, `void Router::LogEmit`
- L90-L157 — `extern "C" void __cdecl OnAppendToMsgBuffer(void* guiInGame, void* esp_param1_addr)` — hook entry point
  note: `esp_param1_addr` is the stack-slot address (KPatchManager's esp+X source emits LEA not MOV — see project_kpatchmanager_lea_bug), dereferenced once to get the `CExoString*`
  note: replay suppression gates on `acc::engine::IsLoadingSaveGame()` OR `!GetPlayerPosition()` (covers main-menu load with no PC); module/door transitions keep the same CGuiInGame and do not replay, so no gate needed there
- L163-L165 — forward decls: `acc::combat::RegisterCombatMsgRules`, `acc::locked_recall::RegisterMsgRule`, `acc::endar::RegisterMsgRule`
  note: locked_recall and endar registrations are new since the last index refresh (only combat was registered before)
- L169-L182 — `void EnsureRulesRegistered()` — one-shot lazy registration, sets log tag to "Combat.MsgBuf", registers all three subsystems
