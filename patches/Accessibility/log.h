// Logging primitives. All output mirrors to OutputDebugStringA so DebugView
// captures the same lines if file writes fail.
//
// Conventions: every line carries a "Tag" prefix (first arg). Trace/Edge/
// Once collapse high-frequency repeats while preserving the suppressed
// count — full fidelity without flooding. No verbosity levels; dedup is
// structural.

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

// Symbolic names for known pointers (e.g. "$mgr"). Unknown pointers fall
// through to hex in a rotating static buffer so multiple FmtPtr calls in
// one log line stay valid simultaneously.
void RegisterPtr(const void* ptr, const char* name);
const char* FmtPtr(const void* ptr);

void FlushAll();

// Absolute path of <install>\patches. Empty before Init. Used by prism.cpp
// for SetDllDirectory.
const char* PatchDir();

}  // namespace acclog
