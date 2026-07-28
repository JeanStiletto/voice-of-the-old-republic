# core_dllmain.cpp (340 lines)

DLL entry point + lazy-init plumbing. `DllMain` only logs and reads `KOTOR_VERSION_SHA` under the loader lock — no LoadLibrary/COM/file-I/O there. `OnRulesInit` (hooked at `CSWRules::CSWRules` construction, @0x00552c9a) is the real bring-up: installs the DirectInput no-mouse crash guard via a hand-built inline trampoline (`InstallMouseGuard`), installs the save-screenshot guard, detects the installed language from `dialog.tlk` (`DetectLanguageFromTlk`, with a CP1251-content probe `TlkLooksCyrillic` that outranks the declared LanguageID — needed because community Russian translations ship under LanguageID=0), pins the speech codepage, initialises Prism (`EnsurePrismInitialized`), snapshots startup settings, rebuilds the engine keymap table, and starts the focus-probe thread + bringup-nag. Talks to `strings.h`, `prism.h`, `save_crash_guard.h`, `diag_settings.h`, `engine_keymap.h`, `bringup_announce.h`, `diag_focus.h`, `update_checker.h` (deliberately NOT started here — see comment on why WinHTTP I/O during bringup competes with Bink movie playback).

## Declarations (in source order)

- L27 — `char g_versionSha[128] = "(unset)"`
- L29 — `const char* LangName(acc::strings::Lang l)`
- L54 — `bool TlkLooksCyrillic(FILE* fp, uint32_t entriesOffset)`
  note: >=20% of sampled bytes >=0xC0 classifies as CP1251 Cyrillic; measured 78% on Allard's real TLK vs ~0% for English prose
- L87 — `acc::strings::Lang DetectLanguageFromTlk()`
  note: content probe (Cyrillic) outranks the declared LanguageID; TLK header layout documented inline (20 bytes: "TLK " V3.0 int32 LanguageID uint32 StringCount uint32 OffsetToStringEntries)
- L156 — `void EnsurePrismInitialized()`
  note: first hook to fire wins; speaks the "loaded, version X" greeting
- L195-197 — `kInitMouseAddr`, `kInitMouseContinue`, `kInitMouseFailEpilogue`
- L199 — `void InstallMouseGuard()`
  note: hand-assembled 32-byte trampoline patched in AFTER the engine's first successful mouse init — an earlier KPatchManager entry-hook attempt broke DirectInput's foreground-cooperative handshake and caused post-focus-loss keyboard death; crash ref userlogs/swkotor.exe.58504.dmp
- L269 — `extern "C" void __cdecl OnRulesInit(void* rulesThis)`
  note: fires exactly once; order is load-bearing (language detect + codepage pin BEFORE EnsurePrismInitialized so the first utterance is already correctly widened)
- L327 — `BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)`
  note: only acclog::Init + env var read under loader lock; Prism init deliberately deferred
