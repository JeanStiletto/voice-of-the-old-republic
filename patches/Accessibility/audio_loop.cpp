#include "audio_loop.h"

#include <windows.h>
#include <cstdlib>
#include <cstring>

#include "audio_bus.h"
#include "engine_player.h"
#include "log.h"
#include "view_mode.h"

namespace acc::audio {

namespace {

// Local mirror of the 16-byte tag from audio_bus.cpp.
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

// Visible struct is { vtable*, internal* } = 8 bytes; 16 for safety.
// The 0xa0-byte CExoSoundSourceInternal is engine-owned (operator_new /
// dtor frees it); we own only the outer struct.
constexpr size_t kSourceStructSize = 16;

// ---- Live pitch (SetPitchMultiplier) -------------------------------------
// All decompile-verified against CExoSoundSourceInternal::SetPitchVariance
// @0x005dba40, which on a playing source recomputes a frequency and calls
// AIL_set_3D_sample_playback_rate(voiceHandle, freq) on the live Miles voice
// (no re-stream). We make the same call with an absolute rate, bypassing the
// engine's rand()-based variance. Field offsets confirmed in swkotor.exe.h
// (CExoSoundSourceInternal) and the SetPitchVariance byte dump.
constexpr size_t   kSoundSourceInternalOffset   = 0x04;  // CExoSoundSource.internal
constexpr size_t   kInternalBaseFrequencyOffset = 0x48;  // sample natural Hz
constexpr size_t   kInternalPitchVarFreqOffset  = 0x54;  // applied rate field
constexpr size_t   kInternalVoice3DOffset       = 0x3c;  // C3DVoice* (null until playing)
constexpr size_t   kVoiceHandleOffset           = 0x04;  // Miles handle inside the voice
// SetPitchVariance reaches the Miles setter via `call dword ptr [0x0073d4e8]`;
// the slot holds the resolved AIL_set_3D_sample_playback_rate import. Calling
// through the engine's own IAT slot avoids resolving mss32.dll ourselves.
constexpr uintptr_t kIatAilSet3DPlaybackRate    = 0x0073D4E8;
typedef void (__stdcall* PFN_AIL_set_3D_sample_playback_rate)(void* handle, int rate);

typedef void* (__thiscall* PFN_SourceCtor)(void* this_);
// MSVC scalar-deleting dtor: bit 0 = "engine _free(this) after destruct".
// We always pass 0 — engine's CRT may not match our DLL's; we free outer
// with our own free().
typedef void  (__thiscall* PFN_SourceDtor)(void* this_, unsigned char free_flag);
typedef void  (__thiscall* PFN_SetResRef)(void* this_, const CResRef* res, int param2);
typedef void  (__thiscall* PFN_Set3D)(void* this_, int enabled);
typedef void  (__thiscall* PFN_SetPosition)(void* this_, const Vector* pos, float z_offset);
typedef void  (__thiscall* PFN_SetDistance)(void* this_, float maxVolDist, float minVolDist);
typedef void  (__thiscall* PFN_SetLooping)(void* this_, int looping);
typedef void  (__thiscall* PFN_SetPriorityGroup)(void* this_, unsigned char group);
typedef void  (__thiscall* PFN_SetVolume)(void* this_, unsigned char volume);
typedef int   (__thiscall* PFN_Play)(void* this_);
typedef void  (__thiscall* PFN_Stop)(void* this_);

// Mirrors PlayCue3D's character-relative bias so loops + impacts land
// at consistent world positions.
Vector BiasForListener(const Vector& worldPosition) {
    Vector pos = worldPosition;
    if (acc::view_mode::IsActive()) return pos;
    Vector charPos, camPos;
    if (acc::engine::GetPlayerPosition(charPos) &&
        acc::engine::GetCameraPosition(camPos)) {
        pos.x += (camPos.x - charPos.x);
        pos.y += (camPos.y - charPos.y);
        pos.z += (camPos.z - charPos.z);
    }
    return pos;
}

// Used by Stop and the Start error path. SEH-guarded — torn-down engine
// can fault inside the dtor chain.
void TeardownEngineSource(void* src, const char* whence) {
    __try {
        auto dtor = reinterpret_cast<PFN_SourceDtor>(kAddrCExoSoundSourceDtor);
        dtor(src, 0);  // 0 = no engine _free; we own the outer alloc
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("LoopSource", "dtor faulted (%s) src=%p — leaking 16B",
                      whence, src);
        return;
    }
    std::free(src);
}

}  // namespace

LoopSource::~LoopSource() {
    Stop();
}

bool LoopSource::Start(const char* resref, const Vector& worldPosition,
                       bool looping, bool spatial, int priorityGroup,
                       int volumeByte, float maxVolDist, float minVolDist) {
    Stop();  // Drop any prior loop on this handle.
    if (!resref || !*resref) return false;

    void* mem = std::malloc(kSourceStructSize);
    if (!mem) return false;
    // Defensive zero — ctor writes vtable + internal, but unobserved
    // fields might still matter.
    std::memset(mem, 0, kSourceStructSize);

    // 2D (spatial=false) skips the listener bias and 3D position entirely
    // and plays centred — position is irrelevant for the glossary audition.
    Vector biased = spatial ? BiasForListener(worldPosition) : worldPosition;

    __try {
        // Construct (vtable + engine-side internal alloc).
        auto ctor = reinterpret_cast<PFN_SourceCtor>(kAddrCExoSoundSourceCtor);
        ctor(mem);

        // SetResRef first — re-initialises the streaming chain
        // (ShutDownSource called internally).
        CResRef res;
        FillResRef(res, resref);
        reinterpret_cast<PFN_SetResRef>(kAddrCExoSoundSourceSetResRef)(mem, &res, 0);
        reinterpret_cast<PFN_Set3D>(kAddrCExoSoundSourceSet3D)(mem, spatial ? 1 : 0);
        if (spatial) {
            reinterpret_cast<PFN_SetPosition>(kAddrCExoSoundSourceSetPosition)(mem, &biased, 0.0f);
            // Override the 3D falloff band when the caller asked for one. The
            // engine ctor defaults to max_volume_distance=10 / min=20, which is
            // tuned for in-world placement; callers on a compressed distance
            // scale need the gradient pulled in (see header).
            if (maxVolDist >= 0.0f && minVolDist >= 0.0f) {
                reinterpret_cast<PFN_SetDistance>(kAddrCExoSoundSourceSetDistance)(
                    mem, maxVolDist, minVolDist);
            }
        }
        // The ctor leaves the source in priority group 0x17, which the
        // engine's menu-pause (SetSoundMode(4) -> PauseAllSounds) silences.
        // A caller that needs to stay audible under that pause (the glossary
        // audition) passes group 0xb — the same group the engine's own GUI
        // click sounds use (CSWGuiManager::LoadGuiSounds), which mode 4
        // explicitly exempts. priorityGroup < 0 leaves the engine default.
        if (priorityGroup >= 0) {
            reinterpret_cast<PFN_SetPriorityGroup>(kAddrCExoSoundSourceSetPriorityGroup)(
                mem, static_cast<unsigned char>(priorityGroup));
        }
        reinterpret_cast<PFN_SetLooping>(kAddrCExoSoundSourceSetLooping)(mem, looping ? 1 : 0);
        // Per-source volume (cue-volume slider preview). < 0 leaves the
        // ctor's full 0x7f. Engine clamps >127 internally.
        if (volumeByte >= 0) {
            unsigned char v = volumeByte > 127 ? 127
                                               : static_cast<unsigned char>(volumeByte);
            reinterpret_cast<PFN_SetVolume>(kAddrCExoSoundSourceSetVolume)(mem, v);
        }

        reinterpret_cast<PFN_Play>(kAddrCExoSoundSourcePlay)(mem);

        // Cache the sample's natural playback rate for SetPitchMultiplier.
        // base_frequency is filled by SetResRef; read it here (3D only —
        // pitch control acts on the 3D voice). 0 disables pitch control.
        base_hz_ = 0;
        if (spatial) {
            void* internal = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(mem) + kSoundSourceInternalOffset);
            if (internal) {
                base_hz_ = *reinterpret_cast<int*>(
                    reinterpret_cast<unsigned char*>(internal) +
                    kInternalBaseFrequencyOffset);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("LoopSource", "start faulted for resref=%s", resref);
        TeardownEngineSource(mem, "start-error");
        return false;
    }

    source_ = mem;
    acclog::Write("LoopSource",
                  "started resref=%s pos=(%.1f,%.1f,%.1f) loop=%d 3d=%d pg=%d vol=%d "
                  "dist=[%.1f,%.1f] base_hz=%d src=%p",
                  resref, biased.x, biased.y, biased.z,
                  looping ? 1 : 0, spatial ? 1 : 0, priorityGroup, volumeByte,
                  maxVolDist, minVolDist, base_hz_, mem);
    return true;
}

void LoopSource::UpdatePosition(const Vector& worldPosition) {
    if (!source_) return;
    Vector biased = BiasForListener(worldPosition);
    __try {
        reinterpret_cast<PFN_SetPosition>(kAddrCExoSoundSourceSetPosition)(
            source_, &biased, 0.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Engine torn down — drop the pointer, don't poke dead memory.
        acclog::Write("LoopSource",
                      "update faulted, dropping source %p", source_);
        source_ = nullptr;
    }
}

void LoopSource::UpdateVolume(int volumeByte) {
    if (!source_) return;
    unsigned char v = volumeByte < 0   ? 0
                    : volumeByte > 127 ? 127
                                       : static_cast<unsigned char>(volumeByte);
    __try {
        reinterpret_cast<PFN_SetVolume>(kAddrCExoSoundSourceSetVolume)(source_, v);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Engine torn down — drop the pointer, don't poke dead memory.
        acclog::Write("LoopSource",
                      "volume update faulted, dropping source %p", source_);
        source_ = nullptr;
    }
}

void LoopSource::SetPitchMultiplier(float multiplier) {
    if (!source_ || base_hz_ <= 0) return;
    // Clamp to +/- 2 octaves; the engine clamps internally too (minPitch/
    // maxPitch globals), but keep our request sane.
    if (multiplier < 0.25f)      multiplier = 0.25f;
    else if (multiplier > 4.0f)  multiplier = 4.0f;
    const int rate = static_cast<int>(base_hz_ * multiplier + 0.5f);

    __try {
        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(source_) + kSoundSourceInternalOffset);
        if (!internal) return;
        // Mirror the rate into the field the engine would re-push from, so a
        // periodic re-assert keeps OUR pitch rather than reverting to base.
        *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(internal) + kInternalPitchVarFreqOffset) = rate;
        void* voice = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) + kInternalVoice3DOffset);
        if (!voice) return;  // 3D voice not created yet — pitch applies next tick
        void* handle = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(voice) + kVoiceHandleOffset);
        if (!handle) return;
        auto fn = *reinterpret_cast<PFN_AIL_set_3D_sample_playback_rate*>(
            kIatAilSet3DPlaybackRate);
        if (fn) fn(handle, rate);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Voice torn down between reads — ignore; re-evaluated next tick.
    }
}

void LoopSource::Stop() {
    if (!source_) return;
    void* mem = source_;
    source_ = nullptr;

    __try {
        reinterpret_cast<PFN_Stop>(kAddrCExoSoundSourceStop)(mem);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("LoopSource", "stop faulted src=%p", mem);
        // Still attempt teardown.
    }

    TeardownEngineSource(mem, "stop");
    acclog::Write("LoopSource", "stopped src=%p", mem);
}

}  // namespace acc::audio
