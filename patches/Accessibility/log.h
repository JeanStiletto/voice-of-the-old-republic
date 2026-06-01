// Logging primitives. The full log always goes to the session file; it also
// mirrors to OutputDebugStringA, but ONLY when a debugger is attached (an
// idle DBWin listener otherwise stalls the game's main tick — see log.cpp).
//
// Conventions: every line carries a "Tag" prefix (first arg). Trace/Edge/
// Once collapse high-frequency repeats while preserving the suppressed
// count — full fidelity without flooding. BlockLog does the same for
// multi-line snapshots (a panel walk, a manager-stack dump): identical
// consecutive blocks fold to a "(repeated Nx)" summary. No verbosity
// levels; dedup is structural.

#pragma once

#include <windows.h>
#include <cstddef>

namespace acclog {

// Resolves <install>\logs\patch-<utc>.log + <install>\patches dir from the
// DLL's path. Call from DllMain on DLL_PROCESS_ATTACH.
void Init(HINSTANCE hinstDLL);

// Flush pending dedup counters + close the log. Idempotent.
void Shutdown();

// printf-style. Tag uses dots for subsystems, no spaces ("Menus.Chain").
void Write(const char* tag, const char* fmt, ...);

// Dedup consecutive identical content per tag — emits one line then a
// "(repeated Nx more)" footer when the next distinct line arrives. Stale
// entries (>1s idle) flush automatically.
void Trace(const char* tag, const char* fmt, ...);

// Write only on first observation of `tag`.
void Once(const char* tag, const char* fmt, ...);

// Write only when `state` differs from the previous call for `tag`. The
// transition line includes the held count of the previous state.
void Edge(const char* tag, int state, const char* fmt, ...);

// Splits long byte runs across multiple lines. Label appears once.
void WriteHex(const char* tag, const char* label, const void* bytes, size_t len);

// Block-level dedup for multi-line snapshots that are re-emitted verbatim
// (a panel walk re-dumped on every rebuild, a manager-stack dump). Trace's
// line-level dedup can't fold these because the repeats are interleaved
// across the block's stride (line [0],[1],[2]...,[0],[1],[2]...). BlockLog
// buffers a whole block, then End() (or the destructor) emits it only if it
// differs from the previous block under the same tag; identical consecutive
// blocks collapse to a "(repeated Nx — unchanged)" summary. Any difference —
// a different panel, changed text, an enabled/disabled control — re-prints
// the block in full, so no information is lost. Per-keystroke navigation
// lives in other tags (Menus.Chain/SetActive) and is unaffected.
//
// Identity vs. display: by default a block's identity (what decides "is this
// the same as last time?") is its full display text. But some blocks carry
// volatile noise that shouldn't count toward identity — e.g. a panel walk
// re-prints the same controls at fresh heap addresses every time the engine
// recreates the panel, so hashing the printed pointers would fold nothing.
// For those, call Key() alongside Line() with the pointers stripped: identity
// is then the Key() stream only, and re-creations of the same panel fold.
// Key() costs nothing extra (it folds into a running hash, no buffer).
//
// Usage (exact-match fold — e.g. a manager-stack dump):
//   acclog::BlockLog b("ManagerStack");
//   b.Line("mgr=%p size=%d", mgr, n);
//   for (...) b.Line("  panels[%d]=%p", i, p);
//
// Usage (semantic fold, ignore volatile pointers — e.g. a panel walk):
//   acclog::BlockLog b("Menus.PanelWalk");
//   b.Line("panel=%p kind=%s", panel, kind);  b.Key("kind=%s", kind);
//   for (...) { b.Line("  [%d] %p text=%s", i, child, text);
//               b.Key ("  [%d] text=%s", i, text); }
//   // b.End() runs automatically when b goes out of scope.
//
// Accumulates on the stack with no lock; only End() touches shared state.
// A block larger than the internal buffer falls back to direct, un-deduped
// writes (so oversized blocks still log in full — they just don't fold).
class BlockLog {
public:
    explicit BlockLog(const char* tag);
    ~BlockLog();
    BlockLog(const BlockLog&)            = delete;
    BlockLog& operator=(const BlockLog&) = delete;

    void Line(const char* fmt, ...);   // display text (always emitted)
    void Key(const char* fmt, ...);    // identity contribution (not emitted)
    void End();

private:
    static constexpr size_t kCap = 16384;
    const char* tag_;
    size_t      len_;
    bool        ended_;
    bool        passthrough_;   // overflowed → already emitting directly
    bool        hasKey_;        // Key() was used → identity is the key stream
    unsigned long long keyHash_;// running FNV-1a over Key() contributions
    char        buf_[kCap];
};

// Symbolic names for known pointers (e.g. "$mgr"). Unknown pointers fall
// through to hex in a rotating static buffer so multiple FmtPtr calls in
// one log line stay valid simultaneously.
void RegisterPtr(const void* ptr, const char* name);
const char* FmtPtr(const void* ptr);

void FlushAll();

// Record a milestone in the bringup timeline. First call (typically
// "dll_attach") captures the baseline; subsequent calls emit a single
// "Bringup: <name> t+<ms>ms" line measured from that baseline. Lets us
// read any user log and see at a glance where the time goes between
// DLL load and an interactive menu — including the gap during which
// intro-movie playback or window-focus glitches can stall the engine
// input pump.
void BringupMark(const char* name);

// Absolute path of <install>\patches. Empty before Init. Used by prism.cpp
// for SetDllDirectory.
const char* PatchDir();

}  // namespace acclog
