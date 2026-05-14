// Audio cue playback — thin wrappers over CExoSound's one-shot entry points.
//
// Layer: audio/ (depends on engine_offsets.h's Vector). Uses the engine's
// own audio pipeline so the user's SFX volume slider, focus-pause, EAX
// reverb, and voice-budget management all "just work" automatically.
//
// Singleton resolution: investigation Q8 "Singleton resolution"
// (closed 2026-05-03). CExoSound* lives at *kAddrCExoSoundPtr; can be
// nullptr early in DLL-attach before the engine's audio init has run, so
// every call defends against it.
//
// All functions are nothrow, SEH-guarded, and return false on any failure
// (singleton not ready, resref-resolution miss, raised exception during
// the engine call). True means the cue was queued — does not guarantee
// audibility, since the engine may evict under voice-budget pressure or
// the resref may resolve to nothing.
//
// Phase 1 lay-off 3. No consumer yet; lay-off 4 wires the test fixture.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::audio {

// Fire a 2D one-shot cue (no spatial position; centred).
//   resref:        ≤16-char .wav resource tag, case-insensitive.
//                  Resolution chain (investigation Q8): Override\ →
//                  streamwaves\ → streamsounds\ → streammusic\ →
//                  BIF/RIM.
//   priorityGroup: 0..N index into the engine's CPriorityGroup table.
//                  Each group has its OWN volume scalar / pitch
//                  variance / 3D falloff. The group determines both
//                  voice-eviction priority AND the implicit volume
//                  scaling, so changing the group can implicitly
//                  amplify the cue several × without touching
//                  volumeByte. Empirical values observed in engine
//                  callers (decompile 2026-05-14):
//                    0x0F (15) — weapon swing
//                    0x13 (19) — player footstep
//                    0x17 (23) — minigame blaster fire
//                    0x18 (24) — death (mgs_accelpad uses 0x17)
//                  Default 0 keeps the legacy nav-cue behaviour.
//   volumeByte:    0..127 per-source volume on top of the group bus.
//                  0 = use group default (127, i.e. max).
//                  Final amplitude ≈ group_volume × volumeByte / 127²
//                  × SFX-slider, so the levers compound.
bool PlayCue(const char* resref,
             uint8_t priorityGroup = 0,
             uint8_t volumeByte    = 0);

// Default amplitude multiplier for accessibility cues. The engine's
// Play3DOneShotSound `volume` slot is a linear amplitude scalar — 1.0
// is unity, 2.0 is +6 dB, 4.0 is +12 dB. The curated Pillar 1 vocabulary
// (gui_select, gui_close, fs_metal_droid2, ...) is sourced from in-game
// SFX that were mixed for occasional UI/combat use, not for sustained
// navigation playback, so they sit too quiet against ambient/footstep
// audio at unity. 4.0× was confirmed audible-but-clean at the Pillar 1
// callsite (2026-05-06); kept centralised here so cycle/passive-narrate/
// view-mode cues match the spatial-change cues' loudness.
constexpr float kAccCueGain = 4.0f;

// Fire a 3D positional one-shot cue at a world position. The engine
// listener is the gameplay camera (~3m behind the character, orbits
// during rotation, springs on wall collision); leaving cue positions
// raw means distance + pan are camera-relative, which produces a
// constant ~3m forward bias and lateral swing as the camera orbits.
//
// This wrapper compensates by shifting the source position by
// (camera - character) before handing it to the engine. By construction
// listener-to-shifted equals character-to-source, so the engine's 3D
// math produces character-relative distance + direction without any
// listener-side change. All other engine audio (footsteps, ambient,
// dialogue, combat) is unaffected.
//
// During view mode the offset is skipped — the OnSetListenerPosition
// detour already substitutes the virtual cursor for the listener, and
// cues in that mode are intended to be cursor-relative.
//
// Distance falloff curve is the engine's Miles default (investigation:
// not measured; tune by ear if needed).
//
// `volume` is forwarded to the engine's Play3DOneShotSound `volume` slot.
// Defaults to kAccCueGain so all accessibility cues land at the same
// loudness; pass an explicit value if a specific cue needs different
// gain.
bool PlayCue3D(const char* resref, const Vector& worldPosition,
               float volume = kAccCueGain);

}  // namespace acc::audio

// CExoSound singleton. *kAddrCExoSoundPtr holds the live facade pointer;
// nullptr in early DLL-attach (before the engine's audio init).
//
// Resolved 2026-05-03 by SARIF Recipe 4 + headless-Ghidra DumpBytes at
// four randomly-sampled callers of Play3DOneShotSound — all show
// MOV ECX, [0x007A39EC]; CALL 0x5D5E10. See navsystem-progress.md
// Phase 1 lay-off 2 and investigation Q8 "Singleton resolution".
constexpr uintptr_t kAddrCExoSoundPtr = 0x007A39EC;

// CExoSound::PlayOneShotSound — __thiscall, signature CONFIRMED
// (investigation Q8). RET 0x18 = 24 bytes of stack args (6 × 4-byte slots
// post-`this`). The function null-checks this->internal at offset 0,
// then tail-calls CExoSoundInternal::PlayOneShotSound at 0x5D7550.
constexpr uintptr_t kAddrCExoSoundPlayOneShotSound   = 0x005D5E00;

// CExoSound::Play3DOneShotSound — __thiscall, signature CONFIRMED.
// RET 0x28 = 40 bytes of stack args (CResRef* + Vector + 7 × 4-byte
// slots). Dispatches identically to the 2D variant after a null-check.
constexpr uintptr_t kAddrCExoSoundPlay3DOneShotSound = 0x005D5E10;

// CExoSound::SetListenerPosition(Vector*) — __thiscall, decomp CONFIRMED
// (investigation §"Listener pose — already managed by the engine").
// Delegates to CExoSoundInternal; backing field at +0x98 inside the
// internal. Address registered in `hooks.toml` as the OnSetListener-
// Position detour target — the listener-override mechanism for view
// mode (the engine writes this every frame from the camera, so we
// detour rather than try to win the OnUpdate race).
constexpr uintptr_t kAddrCExoSoundSetListenerPosition = 0x005D5DF0;

// CExoSoundSourceInternal::CalculatePitchVarianceFrequency() — __thiscall,
// returns int (the per-fire pitch-randomised playback frequency). The
// engine calls this when a fresh source instance starts so each play of
// the same WAV sample lands at a slightly-different pitch (anti-
// repetition cosmetic). For accessibility cues, that randomisation
// degrades localisation — HRTF response is frequency-dependent, so
// jittered pitch shifts the perceived spatial cue from one fire to the
// next, and breaks "high beep = Wall, low thunk = Door"-style
// per-cue identification. The detour at this address neutralises the
// randomisation so each fire of a given resref is sample-identical.
//
// Function range per Lane's SARIF: 0x005db3d0..0x005db44d (125 bytes).
// Used from the Phase 4 pitch-stability hook.
constexpr uintptr_t kAddrCExoSoundSourceInternalCalculatePitchVarianceFrequency
    = 0x005DB3D0;
