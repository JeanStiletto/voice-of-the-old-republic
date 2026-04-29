#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace acclog {

namespace {

char g_logDir[MAX_PATH]   = "logs";
char g_logPath[MAX_PATH]  = "logs\\patch.log";
char g_patchDir[MAX_PATH] = "";

}  // namespace

void Init(HINSTANCE hinstDLL) {
    char dllPath[MAX_PATH] = "";
    DWORD n = GetModuleFileNameA(hinstDLL, dllPath, sizeof(dllPath));
    if (n == 0 || n >= sizeof(dllPath)) {
        OutputDebugStringA("[accessibility] GetModuleFileNameA failed; using CWD-relative paths\n");
        return;
    }

    char* slash = strrchr(dllPath, '\\');
    if (!slash) return;
    *slash = '\0';                          // <install>\patches
    strncpy_s(g_patchDir, dllPath, _TRUNCATE);

    slash = strrchr(dllPath, '\\');
    if (!slash) return;
    *slash = '\0';                          // <install>

    snprintf(g_logDir, sizeof(g_logDir), "%s\\logs", dllPath);

    SYSTEMTIME ts;
    GetSystemTime(&ts);
    snprintf(g_logPath, sizeof(g_logPath),
        "%s\\patch-%04d%02d%02d-%02d%02d%02d.log",
        g_logDir,
        ts.wYear, ts.wMonth, ts.wDay,
        ts.wHour, ts.wMinute, ts.wSecond);

    char dbg[MAX_PATH + 64];
    snprintf(dbg, sizeof(dbg), "[accessibility] log path: %s\n", g_logPath);
    OutputDebugStringA(dbg);
}

void Write(const char* fmt, ...) {
    SYSTEMTIME ts;
    GetSystemTime(&ts);

    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    char line[640];
    snprintf(line, sizeof(line),
        "%04d-%02d-%02dT%02d:%02d:%02dZ [accessibility] %s\n",
        ts.wYear, ts.wMonth, ts.wDay,
        ts.wHour, ts.wMinute, ts.wSecond,
        message);

    OutputDebugStringA(line);

    CreateDirectoryA(g_logDir, nullptr);
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "ab") != 0 || !f) {
        OutputDebugStringA("[accessibility] WARNING: log file open failed\n");
        return;
    }
    fputs(line, f);
    fclose(f);
}

const char* PatchDir() {
    return g_patchDir;
}

}  // namespace acclog
