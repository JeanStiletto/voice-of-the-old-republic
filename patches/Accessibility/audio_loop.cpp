#include "audio_loop.h"

#include <windows.h>
#include <cstdlib>
#include <cstring>

#include "audio_bus.h"      // kAddrCExoSoundSource* address constants
#include "engine_player.h"  // GetPlayerPosition / GetCameraPosition for the
                            //     same character-relative bias PlayCue3D
                            //     applies, so impact one-shots and the
                            //     sustained loop land at consistent
                            //     spatial positions
#include "log.h"
#include "view_mode.h"      // IsActive — same gate PlayCue3D uses to skip
                            //     the bias when the listener override is
                            //     substituting the virtual cursor

namespace acc::audio {

namespace {

// CResRef — 16-byte engine resource-reference tag. Local mirror of the
// definition in audio_bus.cpp; keeping a private copy avoids a header
// dependency for what is otherwise a trivial POD.
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

// The visible CExoSoundSource struct is { vtable*, internal* } = 8
// bytes (per decompile of the ctor). Round up to 16 for safety against
// any private fields we don't see in the decompile. The inner 0xa0-byte
// CExoSoundSourceInternal is allocated by the engine ctor via its own
// operator_new and freed by the engine dtor — we own only the outer
// struct.
constexpr size_t kSourceStructSize = 16;

// Engine thiscall signatures. All take `this` in ECX. See audio_bus.h
// constants for the addresses + memory project_cexosoundsource_loop_api
// .md for the lifecycle decompile.
typedef void* (__thiscall* PFN_SourceCtor)(void* this_);
// MSVC scalar-deleting destructor convention: param flag bit 0 = "engine
// should _free(this) after destruct". We always pass 0 so the engine
// destructs the internal but leaves the outer struct for our own free()
// (the engine's _free comes from its statically-linked CRT and may not
// match the CRT our DLL is linked against).
typedef void  (__thiscall* PFN_SourceDtor)(void* this_, unsigned char free_flag);
typedef void  (__thiscall* PFN_SetResRef)(void* this_, const CResRef* res, int param2);
typedef void  (__thiscall* PFN_Set3D)(void* this_, int enabled);
typedef void  (__thiscall* PFN_SetPosition)(void* this_, const Vector* pos, float z_offset);
typedef void  (__thiscall* PFN_SetLooping)(void* this_, int looping);
typedef int   (__thiscall* PFN_Play)(void* this_);
typedef void  (__thiscall* PFN_Stop)(void* this_);

// Mirror PlayCue3D's character-relative listener bias so loop sources
// land at the same world position the one-shot path would, keeping
// impact + scrape spatially consistent. See audio_bus.cpp lines 138-170
// for the rationale.
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

// Destruct + free the engine source. Used from both Stop() (normal
// teardown) and Start()'s error path (partial-construct cleanup). SEH-
// guarded because an already-torn-down engine can fault inside the
// dtor chain.
void TeardownEngineSource(void* src, const char* whence) {
    __try {
        auto dtor = reinterpret_cast<PFN_SourceDtor>(kAddrCExoSoundSourceDtor);
        dtor(src, 0);  // 0 = don't engine-free; we free with our CRT below
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
    // The ctor writes vtable + internal; defensively zero the rest in
    // case some unobserved field affects behaviour.
    std::memset(mem, 0, kSourceStructSize);

    Vector biased = BiasForListener(worldPosition);

    __try {
        // 1. Construct (sets vtable, allocates engine-side internal).
        auto ctor = reinterpret_cast<PFN_SourceCtor>(kAddrCExoSoundSourceCtor);
        ctor(mem);

        // 2. Configure. Order: resource first (SetResRef calls
        //    ShutDownSource internally and re-initialises the
        //    streaming chain, per decompile), then 3D + position +
        //    loop flag.
        CResRef res;
        FillResRef(res, resref);
        reinterpret_cast<PFN_SetResRef>(kAddrCExoSoundSourceSetResRef)(mem, &res, 0);
        reinterpret_cast<PFN_Set3D>(kAddrCExoSoundSourceSet3D)(mem, 1);
        reinterpret_cast<PFN_SetPosition>(kAddrCExoSoundSourceSetPosition)(mem, &biased, 0.0f);
        reinterpret_cast<PFN_SetLooping>(kAddrCExoSoundSourceSetLooping)(mem, 1);

        // 3. Play.
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
        // Engine torn down or source corrupted — drop the pointer so
        // we don't keep poking dead memory.
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
        // Still attempt teardown — Stop fault doesn't mean dtor will fault.
    }

    TeardownEngineSource(mem, "stop");
    acclog::Write("LoopSource", "stopped src=%p", mem);
}

}  // namespace acc::audio
