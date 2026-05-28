// dinput8.dll proxy for KOTOR.
//
// Drops next to swkotor.exe. Because swkotor.exe statically imports
// DINPUT8.dll and the application directory wins over System32 in the
// loader search order, Windows maps this DLL at process start.
//
// DllMain (synchronous):
//   1. Load the real dinput8.dll from System32 and cache pointers to its
//      six exports so our naked tail-call stubs can forward each call
//      transparently — calling-convention-agnostic, no signature drift.
//   2. Spawn a worker thread.
//
// Worker thread (asynchronous):
//   1. Poll until a top-level visible window belongs to this PID. The
//      Steam DRM stub finishes decryption before the engine creates its
//      OpenGL window, so once a window exists the EXE bytes are settled
//      and KotorPatcher's per-hook byte-verify will pass.
//   2. LoadLibraryA("KotorPatcher.dll"). Its DllMain reads
//      patch_config.toml, applies static/detour hooks, and loads our
//      accessibility.dll.
//
// Failure of either stage is non-fatal: if the real dinput8 is missing
// the EXE simply can't initialize input; if KotorPatcher is missing the
// game runs vanilla. Both failures are logged via OutputDebugStringA.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HMODULE g_real = nullptr;
static void*   g_pfn_DirectInput8Create  = nullptr;
static void*   g_pfn_DllCanUnloadNow     = nullptr;
static void*   g_pfn_DllGetClassObject   = nullptr;
static void*   g_pfn_DllRegisterServer   = nullptr;
static void*   g_pfn_DllUnregisterServer = nullptr;
static void*   g_pfn_GetdfDIJoystick     = nullptr;

static bool LoadRealDInput8() {
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 16) return false;

    const char tail[] = "\\dinput8.dll";
    size_t len = 0;
    while (path[len]) ++len;
    for (size_t i = 0; i < sizeof(tail); ++i) path[len + i] = tail[i];

    g_real = LoadLibraryA(path);
    if (!g_real) return false;

    g_pfn_DirectInput8Create  = (void*)GetProcAddress(g_real, "DirectInput8Create");
    g_pfn_DllCanUnloadNow     = (void*)GetProcAddress(g_real, "DllCanUnloadNow");
    g_pfn_DllGetClassObject   = (void*)GetProcAddress(g_real, "DllGetClassObject");
    g_pfn_DllRegisterServer   = (void*)GetProcAddress(g_real, "DllRegisterServer");
    g_pfn_DllUnregisterServer = (void*)GetProcAddress(g_real, "DllUnregisterServer");
    g_pfn_GetdfDIJoystick     = (void*)GetProcAddress(g_real, "GetdfDIJoystick");

    return g_pfn_DirectInput8Create != nullptr;
}

struct EnumState { DWORD pid; HWND found; };

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    EnumState* state = (EnumState*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == state->pid && IsWindowVisible(hwnd)) {
        state->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI WorkerProc(LPVOID) {
    const DWORD pid = GetCurrentProcessId();
    // 600 * 50ms = 30s, matching the existing launcher's window-wait budget.
    for (int i = 0; i < 600; ++i) {
        EnumState state{ pid, nullptr };
        EnumWindows(EnumProc, (LPARAM)&state);
        if (state.found) {
            OutputDebugStringA("[loader] Game window detected; loading KotorPatcher.dll\n");
            if (!LoadLibraryA("KotorPatcher.dll")) {
                OutputDebugStringA("[loader] LoadLibraryA(KotorPatcher.dll) failed\n");
            }
            return 0;
        }
        Sleep(50);
    }
    OutputDebugStringA("[loader] Timeout waiting for game window; KotorPatcher not loaded\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        if (!LoadRealDInput8()) {
            OutputDebugStringA("[loader] Failed to load real dinput8.dll from System32\n");
            return FALSE;
        }

        HANDLE h = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}

// Naked tail-call stubs. Each export is a single `jmp [pfn]`, so the
// caller's stack frame passes through unchanged and the real function
// sees its arguments and return address exactly as if it were called
// directly. Calling-convention details (stdcall vs cdecl, arg cleanup)
// become a non-issue because no prologue or epilogue is generated.

extern "C" __declspec(naked) void DirectInput8Create() {
    __asm { jmp dword ptr [g_pfn_DirectInput8Create] }
}
extern "C" __declspec(naked) void DllCanUnloadNow() {
    __asm { jmp dword ptr [g_pfn_DllCanUnloadNow] }
}
extern "C" __declspec(naked) void DllGetClassObject() {
    __asm { jmp dword ptr [g_pfn_DllGetClassObject] }
}
extern "C" __declspec(naked) void DllRegisterServer() {
    __asm { jmp dword ptr [g_pfn_DllRegisterServer] }
}
extern "C" __declspec(naked) void DllUnregisterServer() {
    __asm { jmp dword ptr [g_pfn_DllUnregisterServer] }
}
extern "C" __declspec(naked) void GetdfDIJoystick() {
    __asm { jmp dword ptr [g_pfn_GetdfDIJoystick] }
}
