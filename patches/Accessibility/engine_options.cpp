#include "engine_options.h"

#include <windows.h>
#include <cstdint>

#include "engine_app.h"     // GetClientAppInternal

namespace acc::engine {

// CClientExoAppInternal → CClientOptions. Distinct from
// GetPlayerServerObject's chain (different final destination). Returns
// nullptr at any null link or SEH fault.
void* GetClientOptions() {
    void* internal = GetClientAppInternal();
    if (!internal) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) +
            kClientAppOptionsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

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

bool GetActionMenuAutoPause(bool& out) {
    void* options = GetClientOptions();
    if (!options) return false;
    __try {
        unsigned int bits = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<unsigned char*>(options) +
            kClientOptionsAutoPauseFlagsOffset);
        out = (bits & kClientOptionsActionMenuAutoPauseMask) != 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

namespace {

bool WriteMouseLook(void* options, bool enabled) {
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

}  // namespace

bool SetMouseLook(bool enabled) {
    void* options = GetClientOptions();
    if (!options) return false;
    return WriteMouseLook(options, enabled);
}

bool ToggleMouseLook(bool& outNew) {
    void* options = GetClientOptions();
    if (!options) return false;
    bool current = false;
    __try {
        unsigned int bits = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<unsigned char*>(options) +
            kClientOptionsBitFieldOffset);
        current = (bits & kClientOptionsMouseLookMask) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    bool target = !current;
    if (!WriteMouseLook(options, target)) return false;
    outNew = target;
    return true;
}

}  // namespace acc::engine
