// Tolk screen-reader bridge.
//
// Resolves Tolk.dll lazily via LoadLibrary so a missing/broken Tolk install
// degrades to silent mode instead of failing the whole patch DLL load.
// Init must NOT run inside DllMain — Tolk loads COM and screen-reader-driver
// DLLs, both unsafe under the loader lock. Call from the first hook fire.

#pragma once

namespace tolk {

// Loads Tolk.dll from the patch dir, resolves entry points, calls Tolk_Load
// and detects the active screen reader. Idempotent. Returns IsAvailable().
bool Init();

// True iff Init succeeded AND a screen reader with speech support is present.
// Speak/Silence are no-ops when this is false.
bool IsAvailable();

// Output a string. interrupt=true cancels any speech currently being spoken.
void Speak(const wchar_t* text, bool interrupt);

// ANSI overload — converts via MultiByteToWideChar(CP_ACP). KOTOR's CExoString
// payload is ANSI in the engine's active codepage; this is the right entry
// point for strings we lift from game memory.
void Speak(const char* text, bool interrupt);

// Cancel any in-progress speech.
void Silence();

// Common name of the active screen reader, e.g. L"NVDA". L"none" if none /
// not loaded.
const wchar_t* ActiveScreenReader();

// Tolk_Unload. Safe to call multiple times. Call from DLL_PROCESS_DETACH if
// we ever decide to do clean shutdown — currently the process is dying so
// unload is optional.
void Shutdown();

}  // namespace tolk
