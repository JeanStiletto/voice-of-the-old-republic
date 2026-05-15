// Speech bridge.
//
// Historical name: this header used to wrap Tolk.dll. After the Prism
// migration the namespace name stayed `tolk::` to avoid churning ~30
// caller files, but the implementation is Prism-only — see tolk.cpp for
// the two-backend split (normal screen-reader + SAPI urgent channel).
//
// Resolves prism.dll lazily via LoadLibrary so a missing/broken install
// degrades to silent mode instead of failing the whole patch DLL load.
// Init must NOT run inside DllMain — Prism loads COM and screen-reader
// driver DLLs, both unsafe under the loader lock. Call from the first
// hook fire.

#pragma once

namespace tolk {

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
// typed character (including held WASD), and there is no Tolk/NVDA-controller
// flag that overrides it. SAPI is a separate audio path NVDA does not
// manage, so urgent map-cursor / region-cursor / walking cues are not
// silenced by typed-character cancel.
//
// Falls back to the normal Prism backend (best-effort) when:
//   - prism.dll never loaded (no speech path at all)
//   - SAPI backend not in Prism's registry on this system
//   - SAPI initialisation failed at startup
//
// Use for short, single-shot announcements during keyboard panning (map
// cursor, future map data narration). interrupt is implicitly true.
void SpeakUrgent(const char* text);

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

}  // namespace tolk
