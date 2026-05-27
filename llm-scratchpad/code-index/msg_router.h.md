# msg_router.h (54 lines)

In-game message-buffer router. Owns the CGuiInGame::AppendToMsgBuffer @0x0062b5c0 hook. First-match-wins rule dispatch. Unclaimed lines fire OnUnmatched hooks then speak raw. All policy (suppression, merging, priority) lives in registered rules, not in the router.

## Declarations (in source order)

- L16 — `namespace acc::msg`
- L18 — `using RuleFn = bool (*)(const char* text)`
- L23 — `using OnUnmatchedFn = void (*)(const char* text)`
- L25 — `class Router`
  - L27 — `static Router& Instance()`
  - L29 — `void AddRule(const char* name, RuleFn fn)`
  - L30 — `void AddOnUnmatched(OnUnmatchedFn fn)`
  - L31 — `void SetLogTag(const char* tag)`
  - L33 — `void Dispatch(const char* text)`
  - L35 — `void Speak(const char* text)`
  - L36 — `void LogRaw(const char* text)`
  - L37 — `void LogEmit(const char* subTag, const char* text)`
  - L42 — `static constexpr int kMaxRules = 32`
  - L43 — `struct Rule { const char* name; RuleFn fn; }`
