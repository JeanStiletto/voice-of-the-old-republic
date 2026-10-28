// Positional (3D) speech for combat announcements — both games.
//
// A line is rendered to PCM by the mod and played as an in-memory WAV through
// the engine's own 3D sound path (LoopSource::StartFromMemory; mechanism in
// docs/llm-docs/in-memory-sound-resource.md), FROM the direction of the
// creature it is about. So it gets the engine's panning, volume sliders and
// reverb, like any cue.
//
// Placement (2026-10-28, the swoop beacons' method): the engine's own
// distance falloff is switched off and the source sits at a fixed radius from
// the listener, in the subject's direction, so the engine supplies only the
// pan. Loudness is the mod's own curve over the real distance: full up close,
// down to a floor that stays clearly audible. Over the last metres before
// that floor the direction eases to straight ahead, so lines from far away
// end up centred without a jump in level or position.
//
// Placement rule (decided 2026-10-27): a line sounds from WHERE ITS EFFECT
// LANDS — a party attack from its target (the engine already plays swing and
// hit sounds at the attacker), an incoming hit from the hit party member, a
// status / save / kill / absorb from the affected creature, a cast with no
// target from the caster. The CALLER picks the subject; this module only
// plays the line there.
//
// Voices:
//   Normal — imitates the screen reader's voice where the mod can reproduce
//            it: NVDA speaking through IBMTTS (Eloquence, an already-installed
//            engine only — never shipped), a SAPI voice or a OneCore voice,
//            with NVDA's saved rate. Anything else (JAWS, NVDA's eSpeak, ...)
//            falls back to the urgent voice.
//   Urgent — the announcement (SAPI) voice the user picked in Mod settings.
// The screen reader itself can never be positional: it plays its own audio
// and gives the mod no way to get at it.
//
// Each channel speaks one line at a time, in order; the two channels may
// overlap, as the screen reader and the SAPI urgent voice do today.
//
// Volume and speed are the mod's own per channel (Mod settings → Combat
// announcements), NOT the screen reader's: its volume is tuned for centred
// menu speech at system level, while these lines play inside the game's mix
// after distance. Speed is an offset from the voice's own rate, so it still
// follows a change made in NVDA.
//
// NEVER drops a line: when the feature is off, when the subject cannot be
// found, a render fails, or the engine gives the source no 3D voice (no free
// voice), the line is spoken the usual centred way instead.

#pragma once

namespace acc::positional_speech {

enum class Channel { Normal, Urgent };

// True while lines will be played positionally: the Mod-settings toggle is on.
// (A build whose resource-manager addresses don't resolve still falls back,
// line by line, inside Speak.)
bool IsEnabled();

// Speaks `text` (game codepage) from the position of the object named
// `subject` — a creature, or a door or placeable the party attacks — by its
// name exactly as the combat log prints it. When several share that name,
// the one closest to the object named `nearName` wins (an attack's target:
// closest to its attacker), else the one closest to the player.
//
// Returns true when the line was taken over. False means the caller must
// speak it the usual way right now (disabled, or nothing by that name);
// later failures fall back inside this module.
bool Speak(Channel channel, const char* text, const char* subject,
           const char* nearName = nullptr);

// Speaks `text` on `channel` straight ahead of the listener, at the current
// volume and speed and with no distance loss: the combat sliders' preview.
// Replaces whatever the channel was playing, and rides the priority group
// that stays audible while the in-game menu pauses the world. Centred the
// usual way when nothing can be placed (no area loaded, main menu).
void Preview(Channel channel, const char* text);

// ---- Settings (Mod settings → Combat announcements; persisted) -------------

// Loudness of a channel's lines, percent [kMinVolumePercent, 100], default
// 100 = the engine's full source volume at close range. The floor keeps the
// slider from muting the lines: switching the feature off does that job.
constexpr int kMinVolumePercent = 10;
int  GetVolumePercent(Channel channel);
void SetVolumePercent(Channel channel, int percent);

// Speed as an offset from the rate of the voice the channel uses (NVDA's
// saved rate, or the announcement voice's), in rate percent points
// [-kMaxRateOffset, kMaxRateOffset]. 0 = the same speed (the default).
constexpr int kMaxRateOffset = 50;
int  GetRateOffset(Channel channel);
void SetRateOffset(Channel channel, int offset);

// Per tick: collects background renders, starts the next line of each
// channel, falls back on failures. Cheap when idle.
void Tick();

}  // namespace acc::positional_speech
