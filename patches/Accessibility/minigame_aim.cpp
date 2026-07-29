// Shared minigame aim-assist facility — see minigame_aim.h for the design and
// the engine grounding (CSWMiniPlayer.offset +0x1c4 is the per-tick-integrated
// aim/lane field; writing it after Control runs steers the controlled object).

#include "minigame_aim.h"

#include <windows.h>

#include "engine_player.h"  // kAddrAppManagerPtr, kAppManagerClientAppOffset,
                            // kClientExoAppInternalOffset

namespace acc::minigame {

// ---- SEH read primitives ---------------------------------------------------
// Consolidated from minigame_turret.cpp, minigame_swoop_race.cpp and
// minigame_swoop_audio.cpp, which each carried a byte-identical private copy
// (Phase-2 duplication finding D2). Bodies are verbatim.

void* SafeReadPtr(void* base, size_t off) {
    if (!base) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(base) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

uint32_t SafeReadU32(void* base, size_t off) {
    if (!base) return 0;
    __try {
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(base) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

float SafeReadFloat(void* base, size_t off) {
    if (!base) return 0.0f;
    __try {
        return *reinterpret_cast<float*>(
            reinterpret_cast<unsigned char*>(base) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

bool SafeReadVector(void* base, size_t off, Vector& out) {
    if (!base) return false;
    __try {
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(base) + off);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---- Minigame object array -------------------------------------------------

void* ResolveMgoArray() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* clientApp = SafeReadPtr(appManager, kAppManagerClientAppOffset);
        if (!clientApp) return nullptr;
        void* clientInternal = SafeReadPtr(clientApp,
                                           kClientExoAppInternalOffset);
        if (!clientInternal) return nullptr;
        return SafeReadPtr(clientInternal, kClientInternalMgoArrayOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadFollowerPosition(void* follower, Vector& out) {
    if (!follower) return false;
    __try {
        // models is a CExoArrayList<undefined4>; data is its first member
        // (offset 0) and holds 4-byte pointers to model wrapper objects,
        // each with its own vtable. A null data[0] is the empty case, so
        // the list size at +0x6c never has to be read.
        void* modelsData = SafeReadPtr(follower,
                                       kTrackFollowerModelsDataOffset);
        if (!modelsData) return false;
        void* model = *reinterpret_cast<void**>(modelsData);
        if (!model) return false;
        void* vtable = *reinterpret_cast<void**>(model);
        if (!vtable) return false;
        void* fn = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vtable) +
            kModelVtableSlotGetPosition);
        if (!fn) return false;
        typedef Vector* (__thiscall* PFN_GetPositionThunk)(void* this_,
                                                           Vector* outBuf);
        Vector buf = {0.0f, 0.0f, 0.0f};
        Vector* ret = reinterpret_cast<PFN_GetPositionThunk>(fn)(model, &buf);
        out = ret ? *ret : buf;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* CallAsCast(void* obj, size_t vtableSlotOffset) {
    if (!obj) return nullptr;
    __try {
        void* vt = *reinterpret_cast<void**>(obj);
        if (!vt) return nullptr;
        void* fn = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vt) + vtableSlotOffset);
        if (!fn) return nullptr;
        typedef void* (__thiscall* PFN_AsCast)(void* this_);
        return reinterpret_cast<PFN_AsCast>(fn)(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadOffsetVector(void* player, Vector& out) {
    if (!player) return false;
    __try {
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(player) +
            kMiniPlayerOffsetVectorOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void WriteOffsetVector(void* player, const Vector& v) {
    if (!player) return;
    __try {
        *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(player) +
            kMiniPlayerOffsetVectorOffset) = v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

float MagnetGain(float t, const MagnetParams& p) {
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    // t² keeps the far end gentle (a guide) and the near end strong (sticky).
    return p.gainFar + t * t * (p.gainNear - p.gainFar);
}

float MagnetStep(float offsetVal, float mappedErr, float gain,
                 const MagnetParams& p) {
    float step = -mappedErr * gain;
    if (step >  p.maxStep) step =  p.maxStep;
    else if (step < -p.maxStep) step = -p.maxStep;
    return offsetVal + step;
}

}  // namespace acc::minigame
