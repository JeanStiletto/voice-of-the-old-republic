// Speech bridge — wraps the Prism C ABI.
//
// prism.dll resolves lazily via LoadLibrary so a missing/broken install
// degrades to silent mode. Init MUST NOT run inside DllMain — Prism loads
// COM + screen-reader driver DLLs, both unsafe under the loader lock.

#pragma once

#include <cstddef>

namespace prism {

// Idempotent. Returns IsAvailable().
bool Init();

// Speak/Silence are no-ops when this is false.
bool IsAvailable();

// Normal-channel speech via the user's screen reader (NVDA/JAWS/etc).
void Speak(const wchar_t* text, bool interrupt);

// ANSI overload — converts via MultiByteToWideChar(CP_ACP). Right entry
// point for CExoString-sourced text.
void Speak(const char* text, bool interrupt);

// Urgent speech routes through SAPI to bypass NVDA's typed-character
// cancel (which silences NORMAL-priority queued speech). voiceId picks
// among Prism's enumerated SAPI voices (0 = first). Out-of-range falls
// back silently.
//
// Falls back to the normal channel when prism.dll didn't load, SAPI
// isn't in Prism's registry, or SAPI init failed.
//
// interrupt is implicitly true.
void SpeakUrgent(const char* text);
void SpeakUrgent(const char* text, size_t voiceId);

// Cancel in-progress speech on both channels.
void Silence();

// e.g. L"NVDA"; L"none" on init failure. Stable for process lifetime.
const wchar_t* ActiveScreenReader();

// Drops IsAvailable() flag; Prism context stays — tearing it down from
// DLL_PROCESS_DETACH is unsafe under the loader lock. Process exit
// reclaims everything.
void Shutdown();

}  // namespace prism
