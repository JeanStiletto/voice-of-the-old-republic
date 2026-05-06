#include "engine_options.h"

#include <windows.h>
#include <cstdint>

#include "engine_player.h"  // kAddrAppManagerPtr, kAppManagerClientAppOffset,
                            // kClientExoAppInternalOffset

namespace acc::engine {

namespace {

// Walk *kAddrAppManagerPtr → CClientExoApp → CClientExoAppInternal →
// CClientOptions. Distinct from GetPlayerServerObject's chain (different
// final destination). Returns nullptr at any null link or SEH fault.
void* GetClientOptions() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;

        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return nullptr;

        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(exoApp) +
            kClientExoAppInternalOffset);
        if (!internal) return nullptr;

        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) +
            kClientAppOptionsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

bool GetMouseLook(bool& out) {
    void* options = GetClientOptions();
    if (!options) return false;
    __try {
        unsigned int bits = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<unsigned char*>(options) +
            kClientOptionsBitFieldOffset);
        out = (bits & kClientOptionsMouseLookMask) != 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetMouseLook(bool enabled) {
    void* options = GetClientOptions();
    if (!options) return false;
    __try {
        auto* slot = reinterpret_cast<unsigned int*>(
            reinterpret_cast<unsigned char*>(options) +
            kClientOptionsBitFieldOffset);
        unsigned int bits = *slot;
        if (enabled) bits |=  kClientOptionsMouseLookMask;
        else         bits &= ~kClientOptionsMouseLookMask;
        *slot = bits;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ToggleMouseLook(bool& outNew) {
    bool current = false;
    if (!GetMouseLook(current)) return false;
    bool target = !current;
    if (!SetMouseLook(target)) return false;
    outNew = target;
    return true;
}

}  // namespace acc::engine
