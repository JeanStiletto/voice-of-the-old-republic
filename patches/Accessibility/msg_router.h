// In-game message-buffer router.
//
// Owns the CGuiInGame::AppendToMsgBuffer @0x0062b5c0 hook entry and
// dispatches each engine-emitted feedback line through a registered
// rule table. Rules are first-match-wins; the first one to return true
// claims the line (suppressing the raw-speak fallback). Rules that
// return false leave the line for later rules — or, if no rule claims
// it, the router runs registered "on-unmatched" cleanup hooks and then
// speaks the line as-is.
//
// Why a router and not an if-ladder: the message buffer carries every
// combat row, every loot pickup, every heal, every world feedback line.
// As we tweak suppression / merging / priority over time, all the
// policy lives in rules registered here — one place to read, one place
// to change.
//
// Rules register lazily on the first hook fire, so subsystems just need
// to be linked in (no DllMain ordering concerns).
//
// Threading: AppendToMsgBuffer is invoked from the engine's main thread
// only. No locking. The static state lives behind the Meyers singleton.

#pragma once

namespace acc::msg {

// Rule callback: inspects `text`, returns true if the line is claimed
// (suppress raw speech) or false to pass through to the next rule.
// Handlers may call Router::Speak / LogEmit to emit substitute output.
using RuleFn = bool (*)(const char* text);

// Cleanup hook invoked when no rule claims a line, before the router
// speaks it raw. Use this for stateful rules that need to finalize a
// buffered block on a category boundary (e.g. combat's "flush partial
// attack block on miss").
using OnUnmatchedFn = void (*)(const char* text);

class Router {
public:
    static Router& Instance();

    // Configuration — call during one-time registration.
    void AddRule(const char* name, RuleFn fn);
    void AddOnUnmatched(OnUnmatchedFn fn);
    void SetLogTag(const char* tag);   // tag used by LogRaw/LogEmit; default "MsgBuf"

    // Entry: invoked from the AppendToMsgBuffer hook with the engine's
    // freshly-appended row.
    void Dispatch(const char* text);

    // Helpers callable from rule handlers.
    void Speak(const char* text);                          // routes to TTS
    void LogRaw(const char* text);                         // tag: "<tag>: raw: ..."
    void LogEmit(const char* subTag, const char* text);    // tag: "<tag>: <subTag>: ..."

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
