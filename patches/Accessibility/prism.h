// Speech bridge — wraps the Prism C ABI.
//
// prism.dll resolves lazily via LoadLibrary so a missing/broken install
// degrades to silent mode. Init MUST NOT run inside DllMain — Prism loads
// COM + screen-reader driver DLLs, both unsafe under the loader lock.

#pragma once

#include <cstddef>
#include <vector>

namespace prism {

// Idempotent. Returns IsAvailable().
bool Init();

// Speak/Silence are no-ops when this is false.
bool IsAvailable();

// Normal-channel speech via the user's screen reader (NVDA/JAWS/etc).
void Speak(const wchar_t* text, bool interrupt);

// Narrow overload — converts via MultiByteToWideChar(GetSpeechCodepage()).
// Right entry point for CExoString-sourced text.
void Speak(const char* text, bool interrupt);

// Codepage used to widen every narrow string on the way to the screen reader
// — both our own tables and engine CExoString text. Defaults to CP_ACP, which
// reproduces the historical behaviour for en/de/fr/it/es (all Windows-1252).
// Must be pinned explicitly for languages whose codepage differs from the
// player's OS codepage: a Russian install is Windows-1251 (1251) even on a
// German or English Windows, where CP_ACP is 1252 and would garble every
// Cyrillic byte. Polish (1250) will need the same treatment.
//
// Set once from the detected game language at startup; a 0 argument is
// ignored. Safe to call before Init().
void SetSpeechCodepage(unsigned codepage);
unsigned GetSpeechCodepage();

// Urgent speech routes through SAPI to bypass NVDA's typed-character
// cancel (which silences NORMAL-priority queued speech). Which SAPI voice
// speaks is a persistent user setting — see the voice block below.
//
// Falls back to the normal channel when prism.dll didn't load, SAPI
// isn't in Prism's registry, or SAPI init failed.
//
// interrupt is implicitly true.
void SpeakUrgent(const char* text);

// Same urgent (SAPI) channel as SpeakUrgent, but honouring `interrupt` so a
// caller that emits several lines in a row gets them queued instead of each
// one cutting off the last. Written for the subtitle router in
// dialog_speech.cpp, whose lines follow the engine's own queue/interrupt
// rhythm; SpeakUrgent(text) is exactly this with interrupt=true.
void SpeakOnUrgentChannel(const char* text, bool interrupt);

// Urgent-channel (SAPI) volume as a user-facing percent [0,100].
// 100 = SAPI full volume (the default). The setter clamps, persists the
// value across launches (acc_settings.ini), and applies it immediately to
// the live SAPI backend; the getter lazily loads the persisted value.
// Both are safe to call before Init() — the apply no-ops until the SAPI
// backend is acquired, at which point Init re-applies the persisted level.
// Only the SAPI urgent channel is affected; the normal screen-reader
// channel keeps the user's own NVDA/JAWS volume.
void SetUrgentVolumePercent(int percent);
int  GetUrgentVolumePercent();

// Urgent-channel (SAPI) speech rate as a user-facing percent [0,100].
// Prism maps [0.0..1.0] onto SAPI's -10..+10 with 0.5 = SAPI default, so 50
// is the out-of-the-box SAPI pace and 100 the fastest SAPI offers. The
// default is 80 — the rate the urgent channel has always run at.
// Same contract as the volume pair: the setter clamps, persists across
// launches (acc_settings.ini) and applies immediately to the live SAPI
// backend; the getter lazily loads the persisted value. Both are safe to
// call before Init(), which re-applies the persisted rate once the SAPI
// backend is acquired. The normal screen-reader channel is untouched — it
// keeps the user's own NVDA/JAWS rate.
void SetUrgentRatePercent(int percent);
int  GetUrgentRatePercent();

// ---- Urgent-channel voice selection --------------------------------------
//
// The urgent channel speaks with one of the SAPI voices installed on the
// machine. The choice is persisted BY NAME (acc_settings.ini), not by index:
// installing or removing a voice renumbers the list, and an index would
// silently land the user on a different voice — or a different language.
// Init re-resolves the saved name to an index and applies it.
//
// Count is 0 when SAPI is unavailable (no prism.dll, no SAPI backend, init
// failed) or the running prism.dll predates the voice exports; the picker row
// in Mod settings reads "not available" in that case and the channel keeps
// whatever voice SAPI defaults to.
size_t UrgentVoiceCount();

// Name of voice `voiceId`, converted to the game codepage so it can go
// straight to Speak / SpeakUrgent or into a composed UI line. False on an
// out-of-range id, a degenerate buffer, or SAPI being unavailable.
bool GetUrgentVoiceName(size_t voiceId, char* outBuf, size_t bufSize);

// Index of the voice the urgent channel currently speaks with. Seeded at Init
// from the persisted name, else from whatever voice SAPI is already on.
// Returns 0 when SAPI is unavailable, so a picker can index safely.
size_t GetUrgentVoiceId();

// Switch the urgent channel to `voiceId` and persist it by name. Out-of-range
// ids are ignored. Takes effect on the next urgent utterance.
void SetUrgentVoiceId(size_t voiceId);

// ---- Compass channel -----------------------------------------------------
//
// A third voice, for the facing direction — the compass readout while the
// camera turns and the facing key: a second SAPI instance beside the urgent
// one, with the same NVDA bypass but its own voice, volume and rate, so a
// heading never sounds like an announcement and the two never cut each other
// off. Always interrupts its own previous line. Falls back to SpeakUrgent
// when the instance could not be created.
void SpeakCompass(const char* text);

// `text` on the compass voice, then `followUp` on the screen reader once the
// compass voice has said its words (measured by a silent render, so the
// voice's trailing silence is not waited out) — the two take turns. Clears
// the screen reader first. A later interrupting Speak, SpeakCompass or
// Silence drops a follow-up still waiting. Without the compass instance both
// go to the screen reader as one line. LastSpoken() holds the whole line.
void SpeakCompassThen(const char* text, const char* followUp);

// Releases SpeakCompassThen's follow-up when its turn comes. Once per frame.
void Tick();

// Volume and rate, percent [0,100], same contract and defaults as the urgent
// pair (100% volume, rate 80); persisted under their own keys.
void SetCompassVolumePercent(int percent);
int  GetCompassVolumePercent();
void SetCompassRatePercent(int percent);
int  GetCompassRatePercent();

// The voice. Picks from the urgent channel's list — same ids, same names, so
// GetUrgentVoiceName reads them. Persisted by name; until the player picks
// one it defaults to id 2, or the first voice that is not the urgent one when
// id 2 is missing or is the urgent voice. Count is 0 when the compass channel
// is unavailable (it then speaks on the urgent voice).
size_t CompassVoiceCount();
size_t GetCompassVoiceId();
void   SetCompassVoiceId(size_t voiceId);

// ---- Render to memory (positional-speech groundwork) ---------------------
//
// Renders `text` (game codepage) to PCM with the urgent channel's SAPI voice
// instead of speaking it — prism_backend_speak_to_memory. `ratePercent`
// 0..100 as SetUrgentRatePercent; -1 = the channel's own rate. Rendered at
// full volume: the player sets the loudness (positional speech has its own
// volume settings, apart from the urgent-volume slider).
// The screen-reader backends cannot do this (they own their audio), and a
// SAPI voice that merely forwards to the screen reader (SAPI2SR) renders
// nothing; both come back false.
//
// Synchronous: SAPI renders faster than real time, but the call still blocks
// for the render, and queues behind urgent speech the same voice is still
// speaking. Samples are interleaved float in [-1, 1], leading and trailing
// silence trimmed by Prism.
struct RenderedSpeech {
    std::vector<float> samples;
    size_t channels   = 0;
    size_t sampleRate = 0;
};
bool RenderUrgentToMemory(const char* text, int ratePercent, RenderedSpeech& out);

// `ratePercent` (0..100, -1 = the voice's default) moved by `offset` points,
// clamped to 0..100. The default counts as 50, which it is for NVDA's
// percent and Prism's rate alike. Offset 0 returns the input untouched, so a
// default stays a default.
inline int OffsetRatePercent(int ratePercent, int offset) {
    if (offset == 0) return ratePercent;
    int r = (ratePercent < 0 ? 50 : ratePercent) + offset;
    return r < 0 ? 0 : (r > 100 ? 100 : r);
}

// Render with a NAMED voice and rate — used to imitate the screen reader when
// it speaks through a SAPI or OneCore voice. `voiceName` is the voice's
// display string (UTF-8), e.g. a registry token's default value "Microsoft
// Stefan - German (Germany)"; a Prism voice matches when its name equals it or
// is its leading part ("Microsoft Stefan"). `ratePercent` 0..100 (50 = the
// voice's default; -1 = the default) — exact for SAPI's -10..+10, an
// approximation of NVDA's own OneCore curve. Rendered at full volume (the
// player sets the loudness). False when the voice is missing.
//
// SAPI borrows the urgent channel's backend for the one render and restores
// its voice, rate and volume afterwards. OneCore uses a separate render-only
// backend, acquired on first use — it never speaks aloud.
bool RenderSapiVoiceToMemory(const char* voiceName, int ratePercent, const char* text,
                             RenderedSpeech& out);
bool RenderOneCoreToMemory(const char* voiceName, int ratePercent, const char* text,
                           RenderedSpeech& out);

// A line the mod speaks itself, outside both channels (positional speech
// renders and plays its own audio). Gives it what Speak / SpeakUrgent give
// every line besides the sound: the "Speech.spoke" audit log entry, the
// LastSpoken() capture for Ctrl+R, and the braille display.
void NoteSpokenElsewhere(const char* text);

// Cancel in-progress speech on every channel.
void Silence();

// ---- Last-utterance capture --------------------------------------------
//
// Every Speak/SpeakUrgent that actually reaches a backend records its text
// here, so a hotkey can act on "the thing I just heard" without each
// subsystem keeping its own copy. See speech_clipboard.cpp (Ctrl+R).
//
// Returns the bytes the caller passed, in the *game* codepage (not the UTF-8
// the backend receives) — widen with GetSpeechCodepage(). Never null; "" when
// nothing has been spoken yet. Stable until the next utterance.
const char* LastSpoken();

// One-shot: the next Speak/SpeakUrgent call does not overwrite LastSpoken().
// For the mod's own meta-announcements ("copied to clipboard") which would
// otherwise become the very text the user wanted to act on. Set it
// immediately before the speak call so exactly that call consumes it.
void SuppressNextCapture();

// e.g. L"NVDA"; L"none" on init failure. Stable for process lifetime.
const wchar_t* ActiveScreenReader();

// Drops IsAvailable() flag; Prism context stays — tearing it down from
// DLL_PROCESS_DETACH is unsafe under the loader lock. Process exit
// reclaims everything.
void Shutdown();

}  // namespace prism
