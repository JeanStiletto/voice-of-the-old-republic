// Speech bridge — wraps the Prism C ABI.
//
// Resolves prism.dll lazily via LoadLibrary so a missing/broken install
// degrades to silent mode instead of failing the whole patch DLL load.
// Init must NOT run inside DllMain — Prism loads COM and screen-reader
// driver DLLs, both unsafe under the loader lock. Call from the first
// hook fire.

#pragma once

#include <cstddef>

namespace prism {

// Loads prism.dll from the patch dir, resolves entry points, initialises
// a Prism context, acquires the best-available screen-reader backend for
// normal speech and the SAPI backend for urgent speech. Idempotent.
// Returns IsAvailable().
bool Init();

// True iff Init succeeded AND a normal-channel speech backend was acquired.
// Speak/Silence are no-ops when this is false.
bool IsAvailable();

// Output a string on the normal channel — the user's screen reader (NVDA,
// JAWS, ZDSR, Window-Eyes, System Access, OneCore, ...; whichever Prism's
// acquire_best picks at startup). interrupt=true cancels any speech
// currently being spoken on this channel.
void Speak(const wchar_t* text, bool interrupt);

// ANSI overload — converts via MultiByteToWideChar(CP_ACP) and dispatches as
// UTF-8 to the normal Prism backend. KOTOR's CExoString payload is ANSI in
// the engine's active codepage; this is the right entry point for strings
// we lift from game memory.
void Speak(const char* text, bool interrupt);

// Urgent speech — bypasses NVDA's typed-character-cancels-speech behaviour
// by routing through Prism's SAPI backend instead of the user's screen
// reader. NVDA cancels its own queued NORMAL-priority speech on each
// typed character (including held WASD), and there is no controller-side
// flag that overrides it. SAPI is a separate audio path NVDA does not
// manage, so urgent map-cursor / region-cursor / walking cues are not
// silenced by typed-character cancel.
//
// `voiceId` picks among the SAPI voices Prism enumerates on this machine
// (0 = first registered voice; SpeakUrgent() with no arg uses 0). Pass a
// distinct id for utterances that should sound different from the default
// urgent channel — e.g. compass-turn cues use voice 1 so they can't be
// confused with map-cursor cues mid-spin. Out-of-range voice ids fall
// back to the currently-selected voice silently.
//
// Falls back to the normal Prism backend (best-effort) when:
//   - prism.dll never loaded (no speech path at all)
//   - SAPI backend not in Prism's registry on this system
//   - SAPI initialisation failed at startup
//
// interrupt is implicitly true.
void SpeakUrgent(const char* text);
void SpeakUrgent(const char* text, size_t voiceId);

// Cancel any in-progress speech on both channels (normal + urgent).
void Silence();

// Common name of the active normal-channel backend, e.g. L"NVDA". L"none"
// if Init failed or no backend resolved. Stable pointer for the lifetime
// of the process.
const wchar_t* ActiveScreenReader();

// Drop the IsAvailable() flag. The underlying Prism context is left in
// place deliberately — tearing down Prism's RPC/COM bridges from inside
// DLL_PROCESS_DETACH is unsafe under the loader lock. Process exit
// reclaims everything.
void Shutdown();

}  // namespace prism
