// Logging primitives shared by the accessibility patch DLL.
//
// All output is mirrored to OutputDebugStringA so a DebugView session captures
// the same lines even if the file write fails. Log file path is resolved at
// DLL_PROCESS_ATTACH from the DLL's own location (see Init).
//
// Conventions:
//   * Every line carries a "Tag" prefix so the log slices by subsystem with a
//     single regex. Tag is mandatory (first arg of Write/Trace/Once/Edge).
//   * Trace/Edge/Once collapse high-frequency repeats while preserving the
//     suppressed count, so we keep full information density without flooding
//     the log when state hasn't changed. No level/verbosity flags — the dev
//     log is always full fidelity, dedup is structural.

#pragma once

#include <windows.h>
#include <cstddef>

namespace acclog {

// Resolve <install>\logs\patch-<utc>.log + <install>\patches dir from the DLL's
// own absolute path. Must be called from DllMain on DLL_PROCESS_ATTACH.
void Init(HINSTANCE hinstDLL);

// Flush pending Trace/Edge counters and close the log file. Safe to call
// multiple times. Recommended on DLL_PROCESS_DETACH so any lingering dedup
// counts reach the file before shutdown.
void Shutdown();

// printf-style append. Output line is:
//   <timestamp> [accessibility] <tag>: <formatted>
// Tag must be a non-empty string literal-style identifier (e.g. "Menus.Chain",
// "Interact", "Radial.Diag"). Use dots for sub-systems, no spaces.
void Write(const char* tag, const char* fmt, ...);

// Dedup consecutive identical content per tag. When the same tag is called
// twice with identical formatted content, the second call is suppressed and
// a counter increments. When the next call for the same tag produces
// different content (or any other Write/Trace/Edge call would advance the
// log past a stale dedup window), a flush line is emitted recording how
// many duplicates were suppressed. So a tight loop that fires 30 identical
// lines emits one line + one "(repeated 29x more)" footer — full fidelity
// preserved, file size dropped 30x.
//
// Stale entries (no fire for >1s) are flushed automatically the next time
// any log helper is called, so the count surfaces even if the calling
// system goes dormant after the burst.
void Trace(const char* tag, const char* fmt, ...);

// Write only the first time the helper sees `tag`. Subsequent calls with
// the same tag are silently dropped. Use for one-shot discovery / setup
// events that have no useful repeat semantics ("first vtable observed for
// class X").
void Once(const char* tag, const char* fmt, ...);

// Write only when `state` differs from the previous call for `tag`. The
// transition line includes the held count of the previous state so a
// long-held state reports how long it lasted ("(prev held 47x)").
// Use for binary or low-cardinality state polled per frame (stuck flag,
// view-mode active, panel kind).
void Edge(const char* tag, int state, const char* fmt, ...);

// Hex dump helper. Splits long byte runs across multiple lines so none get
// truncated by the per-line buffer. The label appears once on the first line.
void WriteHex(const char* tag, const char* label, const void* bytes, size_t len);

// Symbolic names for known global pointers. After RegisterPtr, FmtPtr(p)
// returns the registered name (e.g. "$mgr") instead of raw hex. Unknown
// pointers fall through to a hex string in a small rotating static buffer
// so multiple FmtPtr calls in one log line stay valid simultaneously.
void RegisterPtr(const void* ptr, const char* name);
const char* FmtPtr(const void* ptr);

// Force-flush all pending Trace/Edge dedup counters. Called automatically
// from Shutdown(); exposed so callers can flush before a deliberate burst
// of unrelated logs (e.g. crash diagnostics) where stale counts mixed with
// the dump would be confusing.
void FlushAll();

// Absolute path of the directory that hosts our patch DLL
// (<install>\patches). Empty before Init runs. Used by prism.cpp to point
// SetDllDirectory at the bundled prism.dll and its delay-loaded bridges.
const char* PatchDir();

}  // namespace acclog
