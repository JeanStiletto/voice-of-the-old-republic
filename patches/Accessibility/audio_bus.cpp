#include "audio_bus.h"
#include "audio_cues.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "audio_pitch.h"   // BeginScopedZero / EndScopedZero (per-call jitter)
#include "engine_player.h"
#include "log.h"
#include "view_mode.h"

namespace acc::audio {

namespace {

// Engine resource-reference tag. Case-insensitive at lookup; we lowercase
// defensively to match every engine callsite.
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

// CExoSound::PlayOneShotSound — __thiscall. Verified by decompile of the
// inner CExoSoundInternal::PlayOneShotSound. Earlier typedef mislabelled
// param_4 as "looping" and param_5/6 as "volume"/"pan"; the real layout is:
//   priority_group, delay_ms, volume_byte, fixed_variance, pitch_variance.
typedef void (__thiscall* PFN_PlayOneShotSound)(
    void*    this_,
    const CResRef* res,
    uint8_t  priority_group,
    uint32_t delay_ms,
    uint8_t  volume_byte,     // 0 = group default (127)
    float    fixed_variance,
    float    pitch_variance);

// CExoSound::Play3DOneShotSound — __thiscall. Same fields as the 2D
// variant plus position (pass-by-value, 12 bytes) and z_offset.
typedef void (__thiscall* PFN_Play3DOneShotSound)(
    void*    this_,
    const CResRef* res,
    Vector   position,
    float    z_offset,
    uint8_t  priority_group,
    uint32_t delay_ms,
    uint8_t  volume_byte,
    float    fixed_variance,
    float    pitch_variance);

void* GetCExoSound() {
    __try {
        return *reinterpret_cast<void**>(kAddrCExoSoundPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

bool PlayCue(const char* resref, uint8_t priorityGroup,
             uint8_t volumeByte) {
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
           priorityGroup,
           /*delay_ms=*/0,
           volumeByte,
           /*fixed_variance=*/0.0f,
           /*pitch_variance=*/0.0f);
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

    // Character-relative listener: shift source by (camera - character).
    // By construction listener-to-shifted equals character-to-source, so
    // we get character-relative pan/distance with the engine's untouched
    // camera listener — no effect on other engine audio.
    //
    // Skipped in view mode (the listener detour already substitutes the
    // cursor; double-compensation would mis-place cues).
    //
    // Fail-safe on chain failure: pass raw position through.
    if (!acc::view_mode::IsActive()) {
        Vector charPos, camPos;
        if (acc::engine::GetPlayerPosition(charPos) &&
            acc::engine::GetCameraPosition(camPos)) {
            pos.x += (camPos.x - charPos.x);
            pos.y += (camPos.y - charPos.y);
            pos.z += (camPos.z - charPos.z);
        }
    }

    // Vestigial. The float was being passed into fixed_variance under the
    // mislabelled typedef (always neutralised by the pitch detour). With
    // the typedef fixed, taking it literally as volume_byte would crush
    // every cue to 3-6% loudness — pass 0 (group default = 127), which
    // matches the historical audible result. API cleanup to uint8_t is
    // out of scope; not worth touching 13 callsites yet.
    (void)volume;

    pitch::BeginScopedZero();
    __try {
        auto fn = reinterpret_cast<PFN_Play3DOneShotSound>(
            kAddrCExoSoundPlay3DOneShotSound);
        fn(exoSound, &res,
           pos,
           /*z_offset=*/0.0f,
           /*priority_group=*/0,    // group 0, vol=106 — every nav cue
           /*delay_ms=*/0,
           /*volume_byte=*/0,       // 0 = group default
           /*fixed_variance=*/0.0f,
           /*pitch_variance=*/0.0f);
        pitch::EndScopedZero();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pitch::EndScopedZero();
        return false;
    }
}

}  // namespace acc::audio

// CExoSound::SetListenerPosition detour. Engine writes this every frame
// from the camera (via UpdateSoundEngine); we substitute the virtual
// cursor when view mode is active. Detour is the only race-free site:
// per-tick listener writes from OnUpdate get stomped.
//
// Hook config (in hooks.toml): skip_original_bytes=true; handler
// replicates this->internal null-check + dispatch to the inner function;
// consumed_exit_address = 0x5d5dfb so we exit through the natural RET 4
// without relocating the rel8 JZ + rel32 JMP in the cut.
//
// __thiscall, ECX = this (CExoSound*), one stack arg = Vector* param.
// Framework's "esp+4" source gives the address of the slot
// (LEA-vs-MOV bug); deref once for Vector*.
namespace acc::audio {
namespace {

typedef void (__thiscall* PFN_InternalSetListenerPosition)(
    void* internal_, Vector* pos);

constexpr uintptr_t kAddrCExoSoundInternalSetListenerPosition = 0x005D6600;

// Diagnostic toggle. False = always passthrough (verified independent of
// view-mode WalkTo and Trigger-1 cue silence). Kept as a quick override
// switch.
constexpr bool kSubstituteCursorForListener = true;

}  // namespace
}  // namespace acc::audio

extern "C" int __cdecl OnSetListenerPosition(void* exoSound,
                                             Vector** posSlot) {
    Vector* enginePos = nullptr;
    if (posSlot) {
        __try {
            enginePos = *posSlot;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            enginePos = nullptr;
        }
    }

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
            return 1;
        }
    } else {
        return 1;  // matches engine's RET-with-no-work on null arg
    }

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
        // Engine internal faulted — consume anyway.
    }

    // Edge on override-state change + 30s heartbeat to keep the per-frame
    // log volume sane.
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

    return 1;  // consumed_exit_address — jump to RET 4
}
