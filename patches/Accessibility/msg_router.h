// In-game message-buffer router.
//
// Owns the CGuiInGame::AppendToMsgBuffer @0x0062b5c0 hook and dispatches
// each engine-emitted line through registered rules. First-match-wins;
// the first rule returning true claims the line and suppresses the raw-
// speak fallback. Unclaimed lines fire on-unmatched cleanup hooks then
// speak raw.
//
// Suppression/merging/priority policy lives entirely in rules registered
// here — one place to read, one place to change.
//
// AppendToMsgBuffer is main-thread-only. No locking.

#pragma once

namespace acc::msg {

using RuleFn = bool (*)(const char* text);

// Invoked when no rule claims a line, before the raw speech. Use for
// stateful rules that need to flush a buffered block on a category
// boundary (combat's "flush partial attack block on miss").
using OnUnmatchedFn = void (*)(const char* text);

class Router {
public:
    static Router& Instance();

    void AddRule(const char* name, RuleFn fn);
    void AddOnUnmatched(OnUnmatchedFn fn);
    void SetLogTag(const char* tag);   // default "MsgBuf"

    void Dispatch(const char* text);

    void Speak(const char* text);
    void LogRaw(const char* text);
    void LogEmit(const char* subTag, const char* text);

private:
    Router() = default;

    static constexpr int kMaxRules = 32;
    struct Rule { const char* name; RuleFn fn; };
    Rule rules_[kMaxRules] = {};
    int  rule_count_ = 0;

    static constexpr int kMaxUnmatched = 4;
    OnUnmatchedFn unmatched_[kMaxUnmatched] = {};
    int unmatched_count_ = 0;

    const char* log_tag_ = "MsgBuf";
};

}  // namespace acc::msg
