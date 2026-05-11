#include "audio_bus.h"
#include "audio_cues.h"  // compile-verification of the cue table; no consumer yet

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "audio_pitch.h"  // BeginScopedZero / EndScopedZero — flips the
                          // per-thread flag the pitch-variance detour
                          // reads so only the source THIS call creates
                          // gets jitter neutralised
#include "engine_player.h"  // GetPlayerPosition / GetCameraPosition for
                            // the character-relative listener offset
                            // applied to PlayCue3D source positions
#include "log.h"
#include "view_mode.h"  // IsActive + TryGetCursorPosition for the listener
                        // override hook

namespace acc::audio {

namespace {

// CResRef — 16-byte engine resource-reference tag. SARIF DATATYPE shows
// it as { char string[16]; } with a ulong[4] alias; we use the char form.
// Tags are case-insensitive at lookup; the engine's resource manager
// hashes case-folded so a mixed-case input still resolves, but we lowercase
// here defensively (matches every existing engine callsite that builds a
// resref from a string literal).
struct CResRef {
    char string[16];
};

void FillResRef(CResRef& out, const char* tag) {
    std::memset(out.string, 0, sizeof(out.string));
    if (!tag) return;
    size_t n = std::strlen(tag);
    if (n > sizeof(out.string)) n = sizeof(out.string);
    for (size_t i = 0; i < n; ++i) {
        char c = tag[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        out.string[i] = c;
    }
}

typedef void (__thiscall* PFN_PlayOneShotSound)(
    void*    this_,
    const CResRef* res,
    uint8_t  priority_group,
    uint32_t delay_ms,
    uint8_t  looping,
    float    volume,
    float    pan);

typedef void (__thiscall* PFN_Play3DOneShotSound)(
    void*    this_,
    const CResRef* res,
    Vector   position,        // pass-by-value; 12 bytes on the stack
    float    z_offset,
    uint8_t  priority_group,
    uint32_t delay_ms,
    uint8_t  looping,
    float    volume,
    float    max_distance);

void* GetCExoSound() {
    __try {
        return *reinterpret_cast<void**>(kAddrCExoSoundPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

bool PlayCue(const char* resref) {
    if (!resref || !*resref) return false;
    void* exoSound = GetCExoSound();
    if (!exoSound) return false;

    CResRef res;
    FillResRef(res, resref);

    pitch::BeginScopedZero();
    __try {
        auto fn = reinterpret_cast<PFN_PlayOneShotSound>(
            kAddrCExoSoundPlayOneShotSound);
        fn(exoSound, &res,
           /*priority_group=*/0,
           /*delay_ms=*/0,
           /*looping=*/0,
           /*volume=*/1.0f,
           /*pan=*/0.0f);
        pitch::EndScopedZero();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pitch::EndScopedZero();
        return false;
    }
}

bool PlayCue3D(const char* resref, const Vector& worldPosition,
               float volume) {
    if (!resref || !*resref) return false;
    void* exoSound = GetCExoSound();
    if (!exoSound) return false;

    CResRef res;
    FillResRef(res, resref);
    Vector pos = worldPosition;  // copy out before SEH frame (no aliasing)

    // Character-relative listener emulation. The engine's listener follows
    // the gameplay camera (3m behind the character, orbits during rotation,
    // springs in/out on wall collision). For navigation cues we want
    // listener-to-source = character-to-source so distance and direction
    // are stable and tied to where the player actually *is*, not where
    // the camera happens to be looking from.
    //
    // Trick: shift the source position by (camera - character). By
    // construction, listener-to-shifted = source-to-character (both
    // magnitude and direction). The engine's 3D pipeline runs untouched
    // against the engine's own camera listener; only the coordinates we
    // hand it are pre-biased. All other engine audio (footsteps, ambient,
    // dialogue, combat) keeps using its native world positions — the
    // engine listener is unchanged.
    //
    // Skipped while view mode is active: the OnSetListenerPosition detour
    // already substitutes the virtual cursor for the camera listener,
    // and cues fired in view mode are intended to sound cursor-relative
    // (the cursor IS the user's frame of reference there). Applying the
    // offset on top would double-compensate. View mode keeps the raw
    // world position.
    //
    // Fail-safe: if either position read fails (pre-spawn, area-load,
    // engine teardown) we fall through to the raw world position — the
    // pre-2026-05-11 behaviour. No silent breakage on chain failure.
    if (!acc::view_mode::IsActive()) {
        Vector charPos, camPos;
        if (acc::engine::GetPlayerPosition(charPos) &&
            acc::engine::GetCameraPosition(camPos)) {
            pos.x += (camPos.x - charPos.x);
            pos.y += (camPos.y - charPos.y);
            pos.z += (camPos.z - charPos.z);
        }
    }

    pitch::BeginScopedZero();
    __try {
        auto fn = reinterpret_cast<PFN_Play3DOneShotSound>(
            kAddrCExoSoundPlay3DOneShotSound);
        fn(exoSound, &res,
           pos,
           /*z_offset=*/0.0f,
           /*priority_group=*/0,
           /*delay_ms=*/0,
           /*looping=*/0,
           volume,
           /*max_distance=*/50.0f);
        pitch::EndScopedZero();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pitch::EndScopedZero();
        return false;
    }
}

}  // namespace acc::audio

// CExoSound::SetListenerPosition detour @0x005d5df0. Phase 4 lay-off 4
// rework — the per-frame camera-driven listener write goes through this
// function (single xref from CClientExoAppInternal::UpdateSoundEngine
// at 0x005f5626). Lay-off 4's per-tick OnUpdate listener write was
// stomped because the engine writes here every frame AFTER our OnUpdate
// callsite; the only winning move is to detour the engine's write
// itself.
//
// Bytes verified via DumpBytes 2026-05-06:
//   0x5d5df0: 8b 09             MOV  ECX, [ECX]      ; this->internal
//   0x5d5df2: 85 c9             TEST ECX, ECX
//   0x5d5df4: 74 05             JZ   +5  -> 0x5d5dfb (RET 4)
//   0x5d5df6: e9 05 08 00 00    JMP  CExoSoundInternal::SetListenerPosition (0x5d6600)
//   0x5d5dfb: c2 04 00          RET  4
//
// Hook design: skip_original_bytes=true at 0x5d5df0 — handler replicates
// the entire function body (null-check on this->internal, then dispatch
// to CExoSoundInternal::SetListenerPosition @0x5d6600), substituting
// the virtual cursor's position for the engine's camera-derived Vector
// when view mode is active. consumed_exit_address = 0x5d5dfb so we
// always exit through the function's natural RET 4. Avoids relocating
// the rel8 JZ + rel32 JMP in the cut.
//
// Calling convention: __thiscall, ECX = this (CExoSound*), one stack
// arg = Vector* param_1. The framework's `source = "esp+4"` for a
// pointer gives us the *address of the slot* per
// project_kpatchmanager_lea_bug.md — we deref once to get Vector*.
//
// Always returns 1 to take the consumed exit (we did the work
// ourselves; never let the original cut bytes "execute").
namespace acc::audio {
namespace {

typedef void (__thiscall* PFN_InternalSetListenerPosition)(
    void* internal_, Vector* pos);

// CExoSoundInternal::SetListenerPosition — the inner function the
// CExoSound wrapper jumps to once it has resolved this->internal. We
// dispatch to it manually after substituting the position vector.
constexpr uintptr_t kAddrCExoSoundInternalSetListenerPosition = 0x005D6600;

// Diagnostic toggle introduced 2026-05-07. Tested innocent in
// patch-20260507-082230.log: with the override gated off (always
// passthrough), view-mode empty-cursor WalkTo still silently no-ops AND
// Trigger-1 3D cues remain inaudible. Both failures are independent of
// this substitution. Restored to true so the listener stays cursor-
// anchored during view mode (the original purpose). The flag stays as
// a quick toggle if a future regression re-implicates the override.
constexpr bool kSubstituteCursorForListener = true;

}  // namespace
}  // namespace acc::audio

extern "C" int __cdecl OnSetListenerPosition(void* exoSound,
                                             Vector** posSlot) {
    // posSlot is the address of the stack slot holding the Vector*
    // param (per the framework's LEA-vs-MOV behaviour for esp+X
    // sources). One deref to recover the engine's argument.
    Vector* enginePos = nullptr;
    if (posSlot) {
        __try {
            enginePos = *posSlot;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            enginePos = nullptr;
        }
    }

    // Pick the position to forward. View-mode active → cursor; else
    // passthrough engine value.
    //
    // Diagnostic gate (2026-05-07): kSubstituteCursorForListener=false
    // forces passthrough even while view mode is active. Used to isolate
    // the listener-override hook as suspect for empty-cursor WalkTo
    // silently no-oping after view-mode-exit, and Trigger-1 3D cues
    // being inaudible during view mode.
    Vector chosen = { 0.0f, 0.0f, 0.0f };
    bool   override_active = false;
    if (acc::audio::kSubstituteCursorForListener &&
        acc::view_mode::IsActive() &&
        acc::view_mode::TryGetCursorPosition(chosen)) {
        override_active = true;
    } else if (enginePos) {
        __try {
            chosen = *enginePos;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 1;  // bad pointer; consume to avoid further dispatch
        }
    } else {
        return 1;  // null arg; consume (matches engine's behaviour with
                   // null this->internal: RET 4 with no work)
    }

    // Replicate the engine's `this->internal` null-check before
    // dispatching to the inner function.
    if (!exoSound) return 1;
    void* internal = nullptr;
    __try {
        internal = *reinterpret_cast<void**>(exoSound);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
    if (!internal) return 1;

    __try {
        auto fn = reinterpret_cast<acc::audio::PFN_InternalSetListenerPosition>(
            acc::audio::kAddrCExoSoundInternalSetListenerPosition);
        fn(internal, &chosen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Engine internal faulted — consume anyway; nothing else for us
        // to do at this layer.
    }

    // Diagnostic kept long-term but quiet: edge-trigger on override
    // state change (so view-mode enter/exit lands one line each), plus
    // a 30-second heartbeat so a long session still shows "yes the
    // hook is alive and the engine is still calling it." That's
    // sufficient signal to debug a silent-failure scenario without the
    // per-frame spam the bring-up build needed.
    static int   s_last_logged_state    = -1;
    static DWORD s_last_heartbeat_ms    = 0;
    DWORD now_ms = GetTickCount();
    int   state  = override_active ? 1 : 0;
    bool  edge   = (state != s_last_logged_state);
    bool  beat   = (now_ms - s_last_heartbeat_ms >= 30000u);
    if (edge || beat) {
        acclog::Write("ListenerHook", "override=%d engine_at=(%.2f,%.2f,%.2f) "
            "chosen=(%.2f,%.2f,%.2f) reason=%s",
            state,
            enginePos ? enginePos->x : 0.0f,
            enginePos ? enginePos->y : 0.0f,
            enginePos ? enginePos->z : 0.0f,
            chosen.x, chosen.y, chosen.z,
            edge ? "transition" : "heartbeat");
        s_last_logged_state = state;
        s_last_heartbeat_ms = now_ms;
    }

    return 1;  // consumed_exit_address — wrapper jumps to RET 4
}
