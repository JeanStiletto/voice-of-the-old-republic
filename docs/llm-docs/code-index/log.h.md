# log.h (53 lines)

Logging primitives. All output mirrors to OutputDebugStringA. Trace/Edge/Once collapse high-frequency repeats while preserving suppressed count. No verbosity levels; dedup is structural. Thread-safe via CRITICAL_SECTION.

## Declarations (in source order)

- L14 — `namespace acclog`
- L18 — `void Init(HINSTANCE hinstDLL)`
  note: resolves log path from DLL path; writes session header; call from DllMain DLL_PROCESS_ATTACH
- L21 — `void Shutdown()`
  note: flushes dedup counters + closes log; idempotent
- L24 — `void Write(const char* tag, const char* fmt, ...)`
  note: unconditional printf-style; tag uses dots (e.g. "Menus.Chain")
- L29 — `void Trace(const char* tag, const char* fmt, ...)`
  note: deduplicates consecutive identical content per tag; emits "(repeated Nx more)" footer on change
- L33 — `void Once(const char* tag, const char* fmt, ...)`
  note: writes only on first observation of tag
- L36 — `void Edge(const char* tag, int state, const char* fmt, ...)`
  note: writes only when state differs from previous call for tag
- L39 — `void WriteHex(const char* tag, const char* label, const void* bytes, size_t len)`
- L43 — `void RegisterPtr(const void* ptr, const char* name)`
- L44 — `const char* FmtPtr(const void* ptr)`
  note: rotating 8-slot buffer; unknown ptrs fall through to hex
- L46 — `void FlushAll()`
- L50 — `const char* PatchDir()`
  note: absolute path of install/patches; empty before Init; used by prism.cpp for SetDllDirectory
