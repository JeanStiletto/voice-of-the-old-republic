// Audio cue playback — thin wrappers over CExoSound's one-shot entry
// points. Goes through the engine's own audio pipeline so the SFX
// slider, focus-pause, EAX reverb, and voice budgeting all apply
// automatically.
//
// CExoSound* lives at *kAddrCExoSoundPtr; can be nullptr early in
// DLL-attach. All functions are nothrow, SEH-guarded, and return false
// on any failure. True = cue queued (doesn't guarantee audibility —
// engine may evict under voice-budget pressure, resref may not resolve).

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::audio {

// 2D one-shot cue (centred, no spatial position).
//   resref:        ≤16-char .wav tag, resolved through Override\ →
//                  streamwaves\ → streamsounds\ → streammusic\ → BIF/RIM.
//   priorityGroup: index into CPriorityGroup table. Each group has its
//                  own volume scalar / pitch variance / 3D falloff, so
//                  the group implicitly amplifies the cue. Observed
//                  values: 0x0F weapon swing, 0x13 footstep, 0x17 blaster,
//                  0x18 death. Default 0 = nav-cue behaviour.
//   volumeByte:    0..127 per-source on top of group bus; 0 = group
//                  default (127). Final ≈ group × volumeByte/127² ×
//                  SFX-slider, so the levers compound.
bool PlayCue(const char* resref,
             uint8_t priorityGroup = 0,
             uint8_t volumeByte    = 0);

// Engine's volume slot is a linear amplitude scalar (2.0 = +6dB). The
// curated cue vocabulary was mixed for UI/combat use; 4.0× lands it
// audible-but-clean against ambient/footstep audio.
constexpr float kAccCueGain = 4.0f;

// 3D positional one-shot. Engine listener is the camera (~3m behind the
// character, orbits during rotation), which gives every raw cue a
// constant forward bias and lateral swing. This wrapper shifts the
// source by (camera - character) so character-to-shifted-source matches
// listener-to-source, producing character-relative pan/distance without
// touching the listener (every other engine sound stays unaffected).
//
// During view mode the offset is skipped — the listener detour already
// substitutes the virtual cursor, and cues there should be cursor-relative.
//
// volume forwards to Play3DOneShotSound's volume slot; default kAccCueGain.
bool PlayCue3D(const char* resref, const Vector& worldPosition,
               float volume = kAccCueGain);

}  // namespace acc::audio

// CExoSound facade. *kAddrCExoSoundPtr; nullptr in early DLL-attach.
constexpr uintptr_t kAddrCExoSoundPtr = 0x007A39EC;

// CExoSound::PlayOneShotSound  — __thiscall, RET 0x18 (6 × 4-byte args).
constexpr uintptr_t kAddrCExoSoundPlayOneShotSound   = 0x005D5E00;

// CExoSound::Play3DOneShotSound — __thiscall, RET 0x28 (CResRef* +
// Vector + 7 × 4-byte slots).
constexpr uintptr_t kAddrCExoSoundPlay3DOneShotSound = 0x005D5E10;

// CExoSound::SetListenerPosition(Vector*) — __thiscall. Engine writes
// this every frame from the camera; we detour rather than race that
// from OnUpdate. Hook target for view-mode listener override.
constexpr uintptr_t kAddrCExoSoundSetListenerPosition = 0x005D5DF0;

// CExoSoundSourceInternal::CalculatePitchVarianceFrequency — __thiscall,
// returns the per-fire randomised playback frequency. We detour this
// to neutralise pitch jitter on accessibility cues: jitter shifts the
// HRTF response per fire, degrading spatial localisation and breaking
// per-cue identification.
constexpr uintptr_t kAddrCExoSoundSourceInternalCalculatePitchVarianceFrequency
    = 0x005DB3D0;

// CExoSoundSource — engine-managed source with full lifecycle (Stop,
// per-tick position update, looping). Use when a feature needs sustained
// spatial audio; for one-shots PlayCue / PlayCue3D are simpler.
//
// Lifecycle (all __thiscall, ECX = source):
//   1. Allocate ~16 bytes (CRT malloc fine — internal 0xa0 bytes are
//      allocated by the ctor via the engine's operator new).
//   2. Ctor (skips internal alloc if global DisableSound is set; every
//      method null-checks and no-ops, so this stays safe).
//   3. SetResRef → Set3D(1) → SetPosition → SetPriorityGroup →
//      SetLooping(1) → Play.
//   4. Per-tick SetPosition if moving.
//   5. Stop → dtor → free.
//
// CResRef-takers (Play, SetResRef) use the 16-byte CResRef struct.
constexpr uintptr_t kAddrCExoSoundSourceCtor             = 0x005D5870;
constexpr uintptr_t kAddrCExoSoundSourceCtorWithResRef   = 0x005D60E0;
constexpr uintptr_t kAddrCExoSoundSourceDtor             = 0x005D60A0;
constexpr uintptr_t kAddrCExoSoundSourceSetPriorityGroup = 0x005D5900;
constexpr uintptr_t kAddrCExoSoundSourceSet3D            = 0x005D5910;
constexpr uintptr_t kAddrCExoSoundSourcePlay             = 0x005D5930;
constexpr uintptr_t kAddrCExoSoundSourceSetVolume        = 0x005D5950;
constexpr uintptr_t kAddrCExoSoundSourceSetPitchVariance = 0x005D5980;
constexpr uintptr_t kAddrCExoSoundSourceSetLooping       = 0x005D59D0;
constexpr uintptr_t kAddrCExoSoundSourceSetPosition      = 0x005D59E0;
constexpr uintptr_t kAddrCExoSoundSourceStop             = 0x005D5A20;
constexpr uintptr_t kAddrCExoSoundSourceSetFixedVariance = 0x005D5A30;
constexpr uintptr_t kAddrCExoSoundSourceGetLooping       = 0x005D6190;
constexpr uintptr_t kAddrCExoSoundSourceSetDistance      = 0x005D61A0;
constexpr uintptr_t kAddrCExoSoundSourceSetResRef        = 0x005D61C0;
