# msg_router.h (55 lines)

Declares the `acc::msg::Router` class: owns the `AppendToMsgBuffer` hook and
dispatches each engine-emitted message-buffer line through registered rules
(first-match-wins), falling back to on-unmatched cleanup hooks then raw
speech. Main-thread-only, no locking. All suppression/merging/priority policy
is meant to live in registered rules, not in the router itself.

## Declarations (in source order)

- L18 — `using RuleFn = bool (*)(const char* text)`
- L23 — `using OnUnmatchedFn = void (*)(const char* text)` — fires on unclaimed lines before raw speech, for stateful flush-on-boundary rules
- L25-L52 — `class Router`: `static Router& Instance()`, `AddRule`, `AddOnUnmatched`, `SetLogTag` (default "MsgBuf"), `Dispatch`, `Speak`, `LogRaw`, `LogEmit`; private fixed arrays `rules_[32]`/`unmatched_[4]`
