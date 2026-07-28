# diag_settings.cpp (147 lines)

One-shot startup snapshot: dumps every `[section]`/key=value pair of `<install>\swkotor.ini`, probes presence+size of audio/input proxy DLLs (dsound/dsoal/dinput8/mss32), and counts files in `Override\`. Runs once from OnRulesInit so support-log bundles always carry install-root baseline. Talks only to log.h and Win32 profile/file APIs.

## Declarations (in source order)

- L19 — `constexpr DWORD kIniBufBytes = 64 * 1024`
- L21 — `bool ResolveInstallRelative(const char* rel, char* out, size_t outCap)`
  note: derives install root from GetModuleFileNameA, strips exe filename
- L32 — `void DumpIni(const char* iniPath)`
  note: uses GetPrivateProfileSectionNamesA/SectionA against a 64KB heap buffer; strips `;`/`#` comment lines
- L71 — `void ProbeFile(const char* rel)` — logs absent/present+size for one install-relative path
- L97 — `int CountFiles(const char* relDir)` — FindFirstFile/FindNextFile count, skipping subdirectories
- L116 — `void LogStartupSnapshot()` (public)
  note: `static bool fired` guards idempotency; dumps swkotor.ini, probes 5 audio/input DLLs, counts Override\ files
