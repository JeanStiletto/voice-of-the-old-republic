// AppManager resolve primitives. See engine_app.h for the chain and the
// reason this file exists.
//
// Each walk gets its own __try rather than sharing one: a fault reading the
// global is a different situation from a fault reading a facade field, and
// both have to yield nullptr cleanly to callers that run during engine
// teardown.

#include "engine_app.h"

#include <windows.h>

namespace acc::engine {

void* GetAppManager() {
    __try {
        return *reinterpret_cast<void**>(kAddrAppManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetServerApp() {
    void* appManager = GetAppManager();
    if (!appManager) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerServerAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetServerAppInternal() {
    void* serverApp = GetServerApp();
    if (!serverApp) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverApp) +
            kServerExoAppInternalOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace acc::engine
