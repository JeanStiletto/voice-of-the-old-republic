#include "probe_camera_distance.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>

#pragma comment(lib, "user32.lib")

#include "engine_player.h"   // AppManager chain constants
#include "hotkeys.h"
#include "log.h"
#include "tolk.h"

namespace acc::probe_camera_distance {

namespace {

// ----- Chain walk constants -------------------------------------------------
constexpr size_t kClientInternalModuleOffset = 0x18;
constexpr size_t kCSWCModuleCameraOffset     = 0x40;

// Camera::vtable index for GetBehavior(tag). vtable[0x80/4 = 32]. Called
// with 0xFFFFFFFF to fetch the currently-active behavior. Matches
// AcclTurnCamera's pattern.
constexpr size_t kCameraVtblGetBehaviorOffset = 0x80;

// CAurBehavior::vtable index for "as state struct" — vtable[0x1c/4 = 7].
// Returns the inner state struct AcclTurnCamera writes yaw into at +0x40.
constexpr size_t kBehaviorVtblAsStateOffset = 0x1c;

// CSWCameraOnAStick fields verified via Ghidra decomp of
// Control_ComputeDesiredPosition + Control. field46_0x110 is the target
// orbit radius; field29_0x84 enables the per-frame auto-fit recompute.
constexpr size_t kBehAutoFitFlagOffset = 0x84;
constexpr size_t kBehTargetDistOffset  = 0x110;
constexpr size_t kBehZOffsetOffset     = 0x120;

// Engine primitives.
constexpr uintptr_t kAddrCameraGetDist  = 0x0045C1D0;
constexpr uintptr_t kAddrCameraGetYaw   = 0x0045C170;
constexpr uintptr_t kAddrCameraGetPitch = 0x0045C1A0;
constexpr uintptr_t kAddrZoomCamera     = 0x006401D0;

// Camera-tuning globals identified via ListSymbolsByName.java. Read-only in
// the probe — logged on snapshot so we can correlate a stomped clamp with
// e.g. a non-zero personal-space buffer that's bumping the post-collision
// position back out.
//   cameraPersonalSpace — additive bubble in Control_HitCheckCamera's
//                          final position adjustment (`(cameraPersonalSpace
//                          + 0.15) * ratio`). If this is ~0.5 at default
//                          it would explain a "distance=0 doesn't actually
//                          put camera AT the character" outcome.
//   cameraInterpDistAmt — Control_ComputeNewCameraPosition smoothing factor
//                          for current-distance vs target-distance lerp.
//                          A non-zero value means our clamp lerps in rather
//                          than snapping.
//   cameraFreeStyle      — non-zero short-circuits Control to
//                          CameraFreeStyleControl (debug/screenshot mode).
constexpr uintptr_t kAddrCameraPersonalSpace = 0x007A241C;
constexpr uintptr_t kAddrCameraInterpAmt1    = 0x007A2430;
constexpr uintptr_t kAddrCameraInterpDistAmt = 0x007A243C;
constexpr uintptr_t kAddrCameraFreeStyle     = 0x00833938;

typedef float (__thiscall* PFN_CameraGetFloat)(void* this_);
typedef void (__thiscall* PFN_ZoomCamera)(void* this_, float delta);
typedef void* (__thiscall* PFN_CameraGetBehavior)(void* this_, unsigned int tag);

// ----- Chain walk -----------------------------------------------------------

void* SafeDeref(void* base, size_t offset) {
    if (!base) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

template <typename T>
T SafeRead(void* base, size_t offset, T fallback) {
    if (!base) return fallback;
    __try {
        return *reinterpret_cast<T*>(
            reinterpret_cast<unsigned char*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

template <typename T>
bool SafeWrite(void* base, size_t offset, T value) {
    if (!base) return false;
    __try {
        *reinterpret_cast<T*>(
            reinterpret_cast<unsigned char*>(base) + offset) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* GetCSWCModule() {
    void* appManager = SafeDeref(
        reinterpret_cast<void*>(kAddrAppManagerPtr), 0);
    if (!appManager) return nullptr;
    void* clientApp = SafeDeref(appManager, kAppManagerClientAppOffset);
    if (!clientApp) return nullptr;
    void* clientInternal = SafeDeref(clientApp, kClientExoAppInternalOffset);
    if (!clientInternal) return nullptr;
    return SafeDeref(clientInternal, kClientInternalModuleOffset);
}

void* GetCamera() {
    void* module = GetCSWCModule();
    if (!module) return nullptr;
    return SafeDeref(module, kCSWCModuleCameraOffset);
}

// Camera::GetBehavior(0xffffffff) — fetches the active behavior. Wraps the
// virtual call defensively (vtable could be torn down during area
// transitions / engine shutdown).
void* GetActiveBehavior(void* camera) {
    if (!camera) return nullptr;
    __try {
        void* vtable = *reinterpret_cast<void**>(camera);
        if (!vtable) return nullptr;
        void* fnSlot = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vtable) +
            kCameraVtblGetBehaviorOffset);
        if (!fnSlot) return nullptr;
        auto fn = reinterpret_cast<PFN_CameraGetBehavior>(fnSlot);
        return fn(camera, 0xFFFFFFFFu);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Behavior::vtable[7]() — returns the inner state struct (where AcclTurnCamera
// writes yaw at +0x40). Logged for completeness; on free-roam (CSWCameraOnAStick)
// it returns a pointer into the behavior itself per Ghidra's view of GetYaw.
void* GetBehaviorStateStruct(void* behavior) {
    if (!behavior) return nullptr;
    __try {
        void* vtable = *reinterpret_cast<void**>(behavior);
        if (!vtable) return nullptr;
        void* fnSlot = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vtable) +
            kBehaviorVtblAsStateOffset);
        if (!fnSlot) return nullptr;
        // vtable[7] is a zero-arg thiscall returning the state struct.
        typedef void* (__thiscall* PFN)(void* this_);
        auto fn = reinterpret_cast<PFN>(fnSlot);
        return fn(behavior);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

float SafeCallGetFloat(uintptr_t addr, void* this_) {
    if (!this_) return 0.0f;
    __try {
        auto fn = reinterpret_cast<PFN_CameraGetFloat>(addr);
        return fn(this_);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

void SafeCallZoom(void* module, float delta) {
    if (!module) return;
    __try {
        auto fn = reinterpret_cast<PFN_ZoomCamera>(kAddrZoomCamera);
        fn(module, delta);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // swallow
    }
}

// ----- Clamp mode state -----------------------------------------------------
//
// Per-tick force-write of CSWCameraOnAStick.field46_0x110. Each tick we
// write `g_clampTarget` and read back what's there after the write — if
// the engine recomputes immediately on the same frame (auto-fit branch in
// Control fires AFTER our OnUpdate callsite) the readback won't match.

enum class ClampMode : int {
    Off       = 0,
    Zero      = 1,   // distance = 0.0 (camera at character)
    Half      = 2,   // distance = 0.5 (just inside the character)
    Two       = 3,   // distance = 2.0 (close third-person)
    COUNT
};

ClampMode g_clampMode = ClampMode::Off;
float     g_clampTarget = 0.0f;  // value written this mode

// Stomp accounting: log a rate-summary every second so a clamp that's
// getting overwritten produces obvious "wrote=0.00 read=4.23 stomps=60/60"
// lines without per-tick spam beyond the existing one-line-per-tick state.
DWORD s_lastRateLogMs   = 0;
int   s_ticksSinceRate  = 0;
int   s_stompsSinceRate = 0;

const char* ModeName(ClampMode m) {
    switch (m) {
    case ClampMode::Off:  return "off";
    case ClampMode::Zero: return "0.0m";
    case ClampMode::Half: return "0.5m";
    case ClampMode::Two:  return "2.0m";
    default:              return "?";
    }
}

float ModeTarget(ClampMode m) {
    switch (m) {
    case ClampMode::Zero: return 0.0f;
    case ClampMode::Half: return 0.5f;
    case ClampMode::Two:  return 2.0f;
    default:              return 0.0f;
    }
}

void AdvanceClampMode() {
    int next = (static_cast<int>(g_clampMode) + 1)
             % static_cast<int>(ClampMode::COUNT);
    g_clampMode = static_cast<ClampMode>(next);
    g_clampTarget = ModeTarget(g_clampMode);

    // Spoken feedback so the user knows which mode we're in without
    // checking the log mid-test.
    char msg[64];
    std::snprintf(msg, sizeof(msg), "Camera distance probe: %s",
                  ModeName(g_clampMode));
    tolk::Speak(msg, /*interrupt=*/true);

    // Reset rate accounting on every mode change so the per-second
    // summary aligns with the new mode's lifetime.
    s_lastRateLogMs   = GetTickCount();
    s_ticksSinceRate  = 0;
    s_stompsSinceRate = 0;

    acclog::Write("CamDistProbe",
        "Clamp mode -> %s (target=%.2f)",
        ModeName(g_clampMode), g_clampTarget);
}

// One-shot full state dump. Called on Ctrl+F12 rising edge.
void DumpSnapshot() {
    void* module   = GetCSWCModule();
    void* camera   = GetCamera();
    void* behavior = GetActiveBehavior(camera);
    void* state    = GetBehaviorStateStruct(behavior);

    float getDist  = SafeCallGetFloat(kAddrCameraGetDist,  camera);
    float getYaw   = SafeCallGetFloat(kAddrCameraGetYaw,   camera);
    float getPitch = SafeCallGetFloat(kAddrCameraGetPitch, camera);

    // CSWCModule cached fields (per probe_camera_state notes):
    //   +0x98 = cached camera yaw (written by AcclTurnCamera)
    //   +0x9c = cached camera distance (written by ZoomCamera)
    float modYaw  = SafeRead<float>(module, 0x98, 0.0f);
    float modDist = SafeRead<float>(module, 0x9c, 0.0f);

    // Behavior fields we care about (CSWCameraOnAStick).
    uint32_t autoFit  = SafeRead<uint32_t>(behavior, kBehAutoFitFlagOffset, 0);
    float    tgtDist  = SafeRead<float>(behavior, kBehTargetDistOffset, 0.0f);
    float    zOffset  = SafeRead<float>(behavior, kBehZOffsetOffset, 0.0f);

    // Deep state struct (vtable[7] return) — yaw at +0x40, distance at +0x34
    // per AcclTurnCamera + ZoomCamera. May be the same address as behavior;
    // log both so we can see.
    float stateDist = SafeRead<float>(state, 0x34, 0.0f);
    float stateYaw  = SafeRead<float>(state, 0x40, 0.0f);

    // Camera tuning globals.
    float persSpace   = SafeRead<float>(
        reinterpret_cast<void*>(kAddrCameraPersonalSpace), 0, 0.0f);
    float interp1     = SafeRead<float>(
        reinterpret_cast<void*>(kAddrCameraInterpAmt1), 0, 0.0f);
    float interpDist  = SafeRead<float>(
        reinterpret_cast<void*>(kAddrCameraInterpDistAmt), 0, 0.0f);
    uint32_t freeStyle = SafeRead<uint32_t>(
        reinterpret_cast<void*>(kAddrCameraFreeStyle), 0, 0);

    acclog::Write("CamDistProbe",
        "DUMP: mod=%p cam=%p beh=%p state=%p | "
        "GetDist=%.3f GetYaw=%.3f GetPitch=%.3f | "
        "modCache yaw=%.3f dist=%.3f | "
        "beh autoFit=%u tgtDist=%.3f zOffset=%.3f | "
        "stateDist=%.3f stateYaw=%.3f | "
        "persSpace=%.3f interp1=%.3f interpDist=%.3f freeStyle=%u",
        module, camera, behavior, state,
        getDist, getYaw, getPitch,
        modYaw, modDist,
        autoFit, tgtDist, zOffset,
        stateDist, stateYaw,
        persSpace, interp1, interpDist, freeStyle);
}

// Per-tick clamp + readback while a non-off clamp mode is active.
void TickClamp() {
    void* behavior = GetActiveBehavior(GetCamera());
    if (!behavior) return;

    // Read PRE-write so we see what the engine had at the start of our
    // tick (i.e. result of last frame's Control + collision pass).
    float pre = SafeRead<float>(behavior, kBehTargetDistOffset, 0.0f);

    // Write our target.
    bool wrote = SafeWrite<float>(behavior, kBehTargetDistOffset, g_clampTarget);

    // Immediate readback to confirm the store landed at the offset we
    // think it did. If post != target the OFFSET is wrong.
    float post = SafeRead<float>(behavior, kBehTargetDistOffset, 0.0f);

    // Also dampen the auto-fit recompute trigger: zero field29_0x84. The
    // Control() function recomputes field46_0x110 from view-cone math
    // ONLY when this flag is non-zero, so zeroing it should prevent the
    // most common stomp source. We do this every tick (cheap) and log it
    // alongside the readback so we can see if engine flips it back on.
    uint32_t autoFitPre = SafeRead<uint32_t>(behavior, kBehAutoFitFlagOffset, 0);
    SafeWrite<uint32_t>(behavior, kBehAutoFitFlagOffset, 0u);

    // Stomp accounting: if post-write readback doesn't equal what we
    // wrote, the write didn't take. (Different from "engine recomputed
    // next frame" — that shows up as pre != target on the NEXT tick.)
    // We also count "pre != target" as a stomp since that's the per-frame
    // engine recompute the auto-fit branch produces.
    bool storeStomped = std::fabs(post - g_clampTarget) > 0.001f;
    bool frameStomped = std::fabs(pre  - g_clampTarget) > 0.001f;
    if (storeStomped || frameStomped) {
        s_stompsSinceRate++;
    }
    s_ticksSinceRate++;

    DWORD now = GetTickCount();
    if (now - s_lastRateLogMs >= 1000u) {
        acclog::Write("CamDistProbe",
            "CLAMP %s target=%.2f wrote=%d pre=%.3f post=%.3f "
            "autoFitPre=%u | last-1s ticks=%d stomps=%d",
            ModeName(g_clampMode),
            g_clampTarget, wrote ? 1 : 0,
            pre, post, autoFitPre,
            s_ticksSinceRate, s_stompsSinceRate);
        s_lastRateLogMs   = now;
        s_ticksSinceRate  = 0;
        s_stompsSinceRate = 0;
    }
}

}  // namespace

void Tick() {
    if (acc::hotkeys::Pressed(acc::hotkeys::Action::ProbeCameraDistDump)) {
        DumpSnapshot();
    }
    if (acc::hotkeys::Pressed(
            acc::hotkeys::Action::ProbeCameraDistClampToggle)) {
        AdvanceClampMode();
    }
    if (g_clampMode != ClampMode::Off) {
        TickClamp();
    }
}

}  // namespace acc::probe_camera_distance
