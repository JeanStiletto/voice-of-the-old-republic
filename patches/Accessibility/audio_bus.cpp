#include "audio_bus.h"
#include "audio_cues.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "audio_pitch.h"   // BeginScopedZero / EndScopedZero (per-call jitter)
#include "engine_player.h"
#include "log.h"
#include "mod_settings_store.h"  // persist the cue-volume slider across launches
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

// --- Global cue volume ----------------------------------------------
// Percent [0,100]; 100 = each cue at its base volume. Backed by the
// persistent store (acc_settings.ini) so it survives relaunch — lazily
// pulled on first use so a cue or the menu sees the saved value even if
// the other never ran this session.
constexpr const char* kCueVolumeKey = "CueVolumePercent";
int  g_cueVolumePercent = 100;
bool g_cueVolumeLoaded  = false;

void EnsureCueVolumeLoaded() {
    if (g_cueVolumeLoaded) return;
    g_cueVolumeLoaded = true;
    int v = acc::settings::GetInt(kCueVolumeKey, 100);
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    g_cueVolumePercent = v;
}

// baseVolume × slider, rounded, clamped to [0,127]. Returns 0 to mean
// "muted — skip the engine call" (volume_byte==0 means engine-full, not
// silence, so the callers must not forward a 0).
uint8_t EffectiveVolumeByte(uint8_t baseVolume) {
    EnsureCueVolumeLoaded();
    int v = (static_cast<int>(baseVolume) * g_cueVolumePercent + 50) / 100;
    if (v > 127) v = 127;
    if (v < 0)   v = 0;
    return static_cast<uint8_t>(v);
}

// --- Priority-group resolution (live CPriorityGroup table) ----------
// Layout mirrors probe_priority_groups.cpp (XML type DB 2026-05-14):
//   CExoSound facade   +0x00 -> CExoSoundInternal*
//   CExoSoundInternal  +0x4c -> CPriorityGroup* (heap array)
//   CPriorityGroup stride 0x18: +0x06 priority(byte) +0x07 volume(byte)
//                               +0x14 fade_time(ushort)
constexpr size_t   kSoundInternalOffset      = 0x00;
constexpr size_t   kPriorityGroupsPtrOff     = 0x4c;
constexpr size_t   kPriorityGroupStride      = 0x18;
constexpr size_t   kPriorityGroupPriorityOff = 0x06;
constexpr size_t   kPriorityGroupVolumeOff   = 0x07;
constexpr size_t   kPriorityGroupFadeTimeOff = 0x14;

// Our installer stamps this value into the FadeTime column of the custom
// full-volume row it appends to prioritygroups.2da. Vanilla fade times
// are 0 or 1000, so 31337 is an unmistakable fingerprint — and because we
// match on a struct field the engine actually loads (not the row index),
// we find our group wherever it lands, immune to row-index drift from
// other mods that also extend the table.
constexpr uint16_t kCueGroupSentinelFadeTime = 31337;

// Fallback when the sentinel row isn't present (prioritygroups.2da edit
// not yet applied): vanilla group 26 — volume 127, identical 20m/10m
// falloff to the legacy group 0, not pause-exempt. Index 26 is within the
// 27-row vanilla table.
constexpr uint8_t  kFallbackFullGroup        = 26;

constexpr int      kMaxGroupScan             = 40;

}  // namespace

void SetGlobalCueVolumePercent(int percent) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    g_cueVolumeLoaded  = true;  // we are the authoritative value now
    g_cueVolumePercent = percent;
    acc::settings::SetInt(kCueVolumeKey, percent);  // persist across launches
    acclog::Write("AudioBus", "cue volume set to %d%%", percent);
}

int GetGlobalCueVolumePercent() {
    EnsureCueVolumeLoaded();
    return g_cueVolumePercent;
}

uint8_t GetCuePriorityGroup() {
    // -1 = not yet resolved. Cached after a successful scan (the table is
    // built once at sound init and never moves). A null subsystem returns
    // the fallback WITHOUT caching, so a cue that fires before sound init
    // doesn't pin us to the fallback forever.
    static int s_resolved = -1;
    if (s_resolved >= 0) return static_cast<uint8_t>(s_resolved);

    void* exoSound = GetCExoSound();
    if (!exoSound) return kFallbackFullGroup;

    __try {
        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(exoSound) + kSoundInternalOffset);
        if (!internal) return kFallbackFullGroup;
        void* table = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(internal) + kPriorityGroupsPtrOff);
        if (!table) return kFallbackFullGroup;

        int garbageRun = 0;
        for (int i = 0; i < kMaxGroupScan; ++i) {
            auto* e = reinterpret_cast<unsigned char*>(table) +
                      static_cast<size_t>(i) * kPriorityGroupStride;
            uint8_t vol  = *(e + kPriorityGroupVolumeOff);
            uint8_t prio = *(e + kPriorityGroupPriorityOff);
            // Same off-the-end heuristic the probe uses: bail after a run
            // of clearly-invalid rows (both bytes > 127).
            if (vol > 127 && prio > 127) {
                if (++garbageRun >= 4) break;
                continue;
            }
            garbageRun = 0;
            uint16_t fade = *reinterpret_cast<uint16_t*>(
                e + kPriorityGroupFadeTimeOff);
            if (fade == kCueGroupSentinelFadeTime) {
                s_resolved = i;
                acclog::Write("AudioBus",
                    "cue group resolved to dedicated sentinel group %d (vol=%u)",
                    i, (unsigned)vol);
                return static_cast<uint8_t>(i);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return kFallbackFullGroup;
    }

    // Sentinel absent — cache the fallback so we don't rescan per cue.
    s_resolved = kFallbackFullGroup;
    acclog::Write("AudioBus",
        "cue group sentinel not found; using fallback full group %u",
        (unsigned)kFallbackFullGroup);
    return kFallbackFullGroup;
}

bool PlayCue(const char* resref, uint8_t priorityGroup,
             uint8_t baseVolume) {
    if (!resref || !*resref) return false;
    void* exoSound = GetCExoSound();
    if (!exoSound) return false;

    uint8_t vol = EffectiveVolumeByte(baseVolume);
    if (vol == 0) return false;  // muted — skip (0 would be engine-full)
    uint8_t group = priorityGroup ? priorityGroup : GetCuePriorityGroup();

    CResRef res;
    FillResRef(res, resref);

    pitch::BeginScopedZero();
    __try {
        auto fn = reinterpret_cast<PFN_PlayOneShotSound>(
            kAddrCExoSoundPlayOneShotSound);
        fn(exoSound, &res,
           group,
           /*delay_ms=*/0,
           vol,
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
               uint8_t priorityGroup, uint8_t baseVolume) {
    if (!resref || !*resref) return false;
    void* exoSound = GetCExoSound();
    if (!exoSound) return false;

    uint8_t vol = EffectiveVolumeByte(baseVolume);
    if (vol == 0) return false;  // muted — skip (0 would be engine-full)
    uint8_t group = priorityGroup ? priorityGroup : GetCuePriorityGroup();

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

    pitch::BeginScopedZero();
    __try {
        auto fn = reinterpret_cast<PFN_Play3DOneShotSound>(
            kAddrCExoSoundPlay3DOneShotSound);
        fn(exoSound, &res,
           pos,
           /*z_offset=*/0.0f,
           group,
           /*delay_ms=*/0,
           vol,
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
