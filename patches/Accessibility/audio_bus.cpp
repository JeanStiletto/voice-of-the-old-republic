#include "audio_bus.h"
#include "audio_cues.h"  // compile-verification of the cue table; no consumer yet

#include <windows.h>
#include <cstdint>
#include <cstring>

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

    __try {
        auto fn = reinterpret_cast<PFN_PlayOneShotSound>(
            kAddrCExoSoundPlayOneShotSound);
        fn(exoSound, &res,
           /*priority_group=*/0,
           /*delay_ms=*/0,
           /*looping=*/0,
           /*volume=*/1.0f,
           /*pan=*/0.0f);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PlayCue3D(const char* resref, const Vector& worldPosition) {
    if (!resref || !*resref) return false;
    void* exoSound = GetCExoSound();
    if (!exoSound) return false;

    CResRef res;
    FillResRef(res, resref);
    Vector pos = worldPosition;  // copy out before SEH frame (no aliasing)

    __try {
        auto fn = reinterpret_cast<PFN_Play3DOneShotSound>(
            kAddrCExoSoundPlay3DOneShotSound);
        fn(exoSound, &res,
           pos,
           /*z_offset=*/0.0f,
           /*priority_group=*/0,
           /*delay_ms=*/0,
           /*looping=*/0,
           /*volume=*/1.0f,
           /*max_distance=*/50.0f);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace acc::audio
