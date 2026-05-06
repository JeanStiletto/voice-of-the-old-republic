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

// Fire a 2D one-shot cue (no spatial position; centred, full volume).
//   resref: ≤16-char .wav resource tag, case-insensitive. Resolution
//           chain (investigation Q8): Override\ → streamwaves\ →
//           streamsounds\ → streammusic\ → BIF/RIM.
bool PlayCue(const char* resref);

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

// Fire a 3D positional one-shot cue at a world position. The engine pans
// and attenuates relative to the current listener — by default the
// player-camera anchor; see future audio_listener.{h,cpp} if we ever
// override. Distance falloff curve is the engine's Miles default
// (investigation: not measured; tune by ear if needed).
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
