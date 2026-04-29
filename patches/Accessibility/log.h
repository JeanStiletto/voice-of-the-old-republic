// Logging primitives shared by the accessibility patch DLL.
//
// All output is mirrored to OutputDebugStringA so a DebugView session captures
// the same lines even if the file write fails. Log file path is resolved at
// DLL_PROCESS_ATTACH from the DLL's own location (see InitLogPaths).

#pragma once

#include <windows.h>

namespace acclog {

// Resolve <install>\logs\patch-<utc>.log + <install>\patches dir from the DLL's
// own absolute path. Must be called from DllMain on DLL_PROCESS_ATTACH.
void Init(HINSTANCE hinstDLL);

// printf-style append to the log file + OutputDebugStringA.
void Write(const char* fmt, ...);

// Absolute path of the directory that hosts our patch DLL
// (<install>\patches). Empty before Init runs. Used by tolk.cpp to point
// SetDllDirectory at the bundled Tolk.dll + nvdaControllerClient32.dll.
const char* PatchDir();

}  // namespace acclog
