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

// Per-cue base volume, 0..127 in the engine's per-source scale (127 =
// engine-full / unity). The cue funnel multiplies this by the global cue
// slider (below) to get the byte handed to CExoSound's volume_byte slot.
//
// NOTE on the gain chain (decompile-verified, see project memory):
//   final ≈ priority_group.volume × source_volume × master_SFX_slider
// `source_volume` is the byte we control here; CExoSoundSource::SetVolume
// HARD-CLAMPS it to 127, so a single cue can never exceed unity by this
// lever. "Louder than full" is a property of the priority GROUP's volume
// (see GetCuePriorityGroup), not of this byte.
constexpr uint8_t kCueVolumeFull = 127;

// --- Global cue volume (the user-facing "hint sounds" slider) --------
// Percent in [0,100]; 100 = each cue at its base volume, 0 = muted.
// Lives here (not in the menu TU) so the audio funnel stays the single
// source of truth and the menu is just a UI over it. In-memory only,
// consistent with the other Mod-Settings toggles (persistence is the
// same project-wide follow-up).
void SetGlobalCueVolumePercent(int percent);  // clamps to [0,100]
int  GetGlobalCueVolumePercent();

// Priority group our one-shot cues ride. Resolved once at runtime by
// scanning the live CPriorityGroup table for our installer-stamped
// sentinel row (a dedicated full-volume group); falls back to a known
// vanilla full-volume group if the sentinel isn't present (e.g. the
// prioritygroups.2da edit hasn't been applied yet). Cached after the
// first successful resolve. See audio_bus.cpp for the mechanism.
uint8_t GetCuePriorityGroup();

// Near-field cue group — the installer's second sentinel row, a tight 1m/8m
// falloff band so loudness tracks distance across the passive proximity cues'
// ~5m awareness range (the flat group above is full-volume out to 10m, so
// near-field cues never varied with distance). Resolved/cached like
// GetCuePriorityGroup; falls back to the flat group if the spatial row is
// absent. Used by PlayCueAtPosition; pass it explicitly to PlayCue3D.
uint8_t GetSpatialCuePriorityGroup();

// 2D one-shot cue (centred, no spatial position).
//   resref:        ≤16-char .wav tag, resolved through Override\ →
//                  streamwaves\ → streamsounds\ → streammusic\ → BIF/RIM.
//   priorityGroup: index into the CPriorityGroup table. 0 (default) =
//                  GetCuePriorityGroup() (our full-volume cue group).
//                  Pass a specific group to override (e.g. combat watch
//                  pins its own full group). The group sets falloff,
//                  voice budget, pause-exemption AND its own volume.
//   baseVolume:    0..127 per-cue base; multiplied by the global slider.
//                  Default kCueVolumeFull. A muted result (slider 0)
//                  skips the engine call (volume_byte==0 would mean
//                  "group default full", not silence).
bool PlayCue(const char* resref,
             uint8_t priorityGroup = 0,
             uint8_t baseVolume    = kCueVolumeFull);

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
// baseVolume is the per-cue base (0..127); the global slider scales it.
// priorityGroup: 0 (default) = GetCuePriorityGroup(). Pass a specific
// group for cues that fire while the world is paused under a sub-screen's
// SetSoundMode (e.g. the map-edge cue): only groups 1/2/0xb stay audible
// under that pause, so such cues must ride 0xb — see project memory
// setsoundmode-priority-group-pause-exemption.
bool PlayCue3D(const char* resref, const Vector& worldPosition,
               uint8_t priorityGroup = 0,
               uint8_t baseVolume    = kCueVolumeFull);

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
