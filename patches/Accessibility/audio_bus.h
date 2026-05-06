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

// Fire a 3D positional one-shot cue at a world position. The engine pans
// and attenuates relative to the current listener — by default the
// player-camera anchor; see future audio_listener.{h,cpp} if we ever
// override. Distance falloff curve is the engine's Miles default
// (investigation: not measured; tune by ear if needed).
bool PlayCue3D(const char* resref, const Vector& worldPosition);

// Override the engine's 3D-audio listener to a chosen world position.
// Wraps `CExoSound::SetListenerPosition @0x5d5df0`, which writes
// `CExoSoundInternal.listener_position +0x98`. The engine writes the
// camera-driven listener every frame as part of its render-side update;
// callers must therefore re-issue this every tick they want the override
// to hold (see `view_mode::Tick`). On stopping the per-tick re-issue,
// the engine reclaims the field on its next pass and 3D pan / attenuation
// resume tracking the camera. Returns false on null singleton or SEH
// fault inside the engine call.
//
// Phase 4 lay-off 4 view mode is the only consumer; if anything else
// ever wants the override, gate against view-mode active so we don't
// stomp each other.
bool SetListener(const Vector& pos);

// Read the engine's current listener position
// (`CExoSoundInternal.listener_position +0x98`). Used by the lay-off-4
// scaffolding probe to verify whether our `SetListener` write survives
// the next engine update or gets stomped before our next tick. Returns
// false on null singleton, null internal pointer, or SEH fault. The
// probe call site is throwaway diagnostic code; production callers
// should not need this — keep it private to view-mode bring-up.
bool GetListener(Vector& out);

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
// internal. Used by view mode to override the listener to the virtual
// cursor; engine re-writes every frame from the camera, so callers must
// re-issue per tick.
constexpr uintptr_t kAddrCExoSoundSetListenerPosition = 0x005D5DF0;

// CExoSoundInternal.listener_position — Vector @+0x98 inside the
// instance reached via *((void**)CExoSound) (CExoSound::internal at +0).
// Read directly by GetListener; written via SetListenerPosition.
constexpr size_t    kCExoSoundInternalListenerPosOffset = 0x98;
