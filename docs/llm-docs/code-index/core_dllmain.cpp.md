# core_dllmain.cpp (93 lines)

DLL entry and lazy-init plumbing. DllMain runs under the loader lock — no Prism init here. Prism is initialised lazily on the first hook fire. RE helper for dumping runtime-decrypted bytes included but disposable.

## Declarations (in source order)

- L24 — `void EnsurePrismInitialized()`
  note: one-shot; speaks "loaded, version X" greeting on first fire
- L39 — `static void DumpFunctionBytes(const char* tag, uintptr_t va, size_t len)`
  note: RE helper; reads live process bytes and logs as hex; removable once investigations close
- L61 — `extern "C" void __cdecl OnRulesInit(void* rulesThis)`
  note: hook on CSWRules::CSWRules @0x00552c9a; first fire triggers EnsurePrismInitialized + RE dumps
- L80 — `BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID)`
