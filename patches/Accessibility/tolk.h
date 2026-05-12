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

// Urgent speech — bypasses NVDA's typed-character-cancels-speech behaviour.
//
// Tolk_Output runs at NVDA's NORMAL priority. NVDA's typed-character speech
// also runs at NORMAL, so when the user holds a movement key NVDA cancels
// our utterance on every keystroke. There's no Tolk flag to override that.
//
// `SpeakUrgent` instead routes through `nvdaController_speakSsml`
// (NVDA 2024.1+, see the controller-client IDL) with priority
// `SPEECH_PRIORITY_NOW` — per NVDA's documented semantics, NOW utterances
// cannot be interrupted by lower-priority (NORMAL) speech such as the
// typed-character announces. After the NOW utterance completes, any
// queued NORMAL speech resumes.
//
// Falls back to `Speak(text, /*interrupt=*/false)` when:
//   - nvdaControllerClient32.dll is missing (silent / no NVDA installed)
//   - nvdaController_speakSsml is not exported (NVDA < 2024.1)
//   - speakSsml returns a non-zero error (RPC_S_UNKNOWN_IF == 1717 on
//     older NVDA; any RPC failure also fails over)
//   - The active screen reader isn't NVDA (JAWS / Narrator / SAPI all
//     take the Tolk_Output path and accept the cancellation reality
//     until each driver grows an equivalent priority API)
//
// Use for short, single-shot announcements during keyboard panning
// (map cursor, future map data narration). NOT for long continuous
// content — NOW priority forces a queue interruption every time it
// fires, so spamming this would make the user lose every other
// utterance.
void SpeakUrgent(const char* text);

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
