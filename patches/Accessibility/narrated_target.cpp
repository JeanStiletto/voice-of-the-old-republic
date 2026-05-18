#include "narrated_target.h"

#include <windows.h>  // GetTickCount
#include <cstdint>

#include "engine_area.h"  // ResolveServerObjectHandle for validation
#include "log.h"

namespace acc::narrated_target {

namespace {

Slot g_slot;

}  // namespace

void Stamp(void* obj, uint32_t serverHandle) {
    if (!obj || serverHandle == 0u || serverHandle == 0xFFFFFFFFu ||
        serverHandle == 0x7F000000u) {
        return;
    }
    bool changed = (g_slot.obj != obj || g_slot.handle != serverHandle);
    g_slot.obj       = obj;
    g_slot.handle    = serverHandle;
    g_slot.tickStamp = GetTickCount();
    if (changed) {
        acclog::Write("NarratedTarget", "stamp obj=%p handle=0x%08x", obj,
                      serverHandle);
    }
}

void Clear() {
    if (g_slot.obj || g_slot.handle) {
        acclog::Write("NarratedTarget", "clear (was obj=%p handle=0x%08x)",
                      g_slot.obj, g_slot.handle);
    }
    g_slot = {};
}

bool TryGet(Slot& out) {
    out = {};
    if (!g_slot.obj || g_slot.handle == 0u) return false;

    // Validate the slot is still live: resolve the server handle and
    // confirm it points back at the stamped object. Catches the two
    // failure modes that survive a missing Clear() call — the engine
    // destroyed the object (handle now resolves to nullptr / a different
    // pointer) or the area transitioned without our area-change branch
    // firing in time.
    void* resolved = acc::engine::ResolveServerObjectHandle(g_slot.handle);
    if (!resolved || resolved != g_slot.obj) {
        acclog::Write("NarratedTarget", "stale slot (obj=%p handle=0x%08x "
                      "resolved=%p) — clearing",
                      g_slot.obj, g_slot.handle, resolved);
        g_slot = {};
        return false;
    }

    out = g_slot;
    return true;
}

}  // namespace acc::narrated_target
