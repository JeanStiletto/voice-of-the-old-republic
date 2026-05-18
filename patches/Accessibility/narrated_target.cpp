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
    bool changed = (g_slot.obj != obj || g_slot.handle != serverHandle ||
                    g_slot.isMapPin);
    g_slot.obj       = obj;
    g_slot.handle    = serverHandle;
    g_slot.pos       = {0, 0, 0};   // game-object slot re-reads pos on use
    g_slot.tickStamp = GetTickCount();
    g_slot.isMapPin  = false;
    if (changed) {
        acclog::Write("NarratedTarget", "stamp obj=%p handle=0x%08x", obj,
                      serverHandle);
    }
}

void StampMapPin(void* pin, const Vector& pos) {
    if (!pin) return;
    bool changed = (g_slot.obj != pin || !g_slot.isMapPin);
    g_slot.obj       = pin;
    g_slot.handle    = 0;
    g_slot.pos       = pos;
    g_slot.tickStamp = GetTickCount();
    g_slot.isMapPin  = true;
    if (changed) {
        acclog::Write("NarratedTarget",
                      "stamp(map-pin) pin=%p pos=(%.2f,%.2f,%.2f)",
                      pin, pos.x, pos.y, pos.z);
    }
}

void Clear() {
    if (g_slot.obj || g_slot.handle) {
        acclog::Write("NarratedTarget", "clear (was obj=%p handle=0x%08x "
                      "isMapPin=%d)",
                      g_slot.obj, g_slot.handle, (int)g_slot.isMapPin);
    }
    g_slot = {};
}

namespace {

// Walk the current player's client area's map_pins[] and return true
// iff `pin` is still present. Used to validate the map-pin slot on
// activation — pins removed by quest scripts shouldn't keep stale
// focus.
bool IsMapPinStillPresent(void* pin) {
    if (!pin) return false;
    void* area = acc::engine::GetCurrentArea();
    if (!area) return false;
    void* clientArea = acc::engine::GetClientArea(area);
    int   count = acc::engine::GetMapPinCount(clientArea);
    for (int i = 0; i < count; ++i) {
        if (acc::engine::GetMapPinAt(clientArea, i) == pin) return true;
    }
    return false;
}

}  // namespace

bool TryGet(Slot& out) {
    out = {};
    if (!g_slot.obj) return false;

    if (g_slot.isMapPin) {
        // Validate the pin is still in the client area's pin array.
        // Pins can be removed by SetMapPinEnabled(off) + later area
        // unload + reload — defensive walk.
        if (!IsMapPinStillPresent(g_slot.obj)) {
            acclog::Write("NarratedTarget",
                          "stale map-pin slot (pin=%p) — clearing",
                          g_slot.obj);
            g_slot = {};
            return false;
        }
        out = g_slot;
        return true;
    }

    if (g_slot.handle == 0u) return false;

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
