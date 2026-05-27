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

typedef void* (__thiscall* PFN_SourceCtor)(void* this_);
// MSVC scalar-deleting dtor: bit 0 = "engine _free(this) after destruct".
// We always pass 0 — engine's CRT may not match our DLL's; we free outer
// with our own free().
typedef void  (__thiscall* PFN_SourceDtor)(void* this_, unsigned char free_flag);
typedef void  (__thiscall* PFN_SetResRef)(void* this_, const CResRef* res, int param2);
typedef void  (__thiscall* PFN_Set3D)(void* this_, int enabled);
typedef void  (__thiscall* PFN_SetPosition)(void* this_, const Vector* pos, float z_offset);
typedef void  (__thiscall* PFN_SetLooping)(void* this_, int looping);
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

bool LoopSource::Start(const char* resref, const Vector& worldPosition) {
    Stop();  // Drop any prior loop on this handle.
    if (!resref || !*resref) return false;

    void* mem = std::malloc(kSourceStructSize);
    if (!mem) return false;
    // Defensive zero — ctor writes vtable + internal, but unobserved
    // fields might still matter.
    std::memset(mem, 0, kSourceStructSize);

    Vector biased = BiasForListener(worldPosition);

    __try {
        // Construct (vtable + engine-side internal alloc).
        auto ctor = reinterpret_cast<PFN_SourceCtor>(kAddrCExoSoundSourceCtor);
        ctor(mem);

        // SetResRef first — re-initialises the streaming chain
        // (ShutDownSource called internally).
        CResRef res;
        FillResRef(res, resref);
        reinterpret_cast<PFN_SetResRef>(kAddrCExoSoundSourceSetResRef)(mem, &res, 0);
        reinterpret_cast<PFN_Set3D>(kAddrCExoSoundSourceSet3D)(mem, 1);
        reinterpret_cast<PFN_SetPosition>(kAddrCExoSoundSourceSetPosition)(mem, &biased, 0.0f);
        reinterpret_cast<PFN_SetLooping>(kAddrCExoSoundSourceSetLooping)(mem, 1);

        reinterpret_cast<PFN_Play>(kAddrCExoSoundSourcePlay)(mem);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("LoopSource", "start faulted for resref=%s", resref);
        TeardownEngineSource(mem, "start-error");
        return false;
    }

    source_ = mem;
    acclog::Write("LoopSource",
                  "started resref=%s pos=(%.1f,%.1f,%.1f) src=%p",
                  resref, biased.x, biased.y, biased.z, mem);
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
