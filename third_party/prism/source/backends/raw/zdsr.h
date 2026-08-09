// SPDX-License-Identifier: MPL-2.0

// ZDSR's client library, resolved at runtime. See raw/dynamic_library.h for
// why these entry points are not imported through a .def-generated import
// library.

#pragma once
#ifdef _WIN32
#include <raw/dynamic_library.h>
#include <windows.h>

namespace prism::raw::zdsr {

// Required by the backend; non-null once load() has returned true.
inline int(WINAPI *InitTTS)(int type, const WCHAR *channelName,
                            BOOL bKeyDownInterrupt) = nullptr;
inline int(WINAPI *Speak)(const WCHAR *text, BOOL bInterrupt) = nullptr;
inline int(WINAPI *GetSpeakState)() = nullptr;
inline void(WINAPI *StopSpeak)() = nullptr;

// Optional; may be null even when load() succeeded, so check before calling.
// Upstream 0.17.3 imports Braille unconditionally, which would make an older
// ZDSRAPI that predates it fail to bind and cost those users speech as well as
// braille. Resolved separately so a missing Braille costs only braille.
inline int(WINAPI *Braille)(const WCHAR *text, BOOL bFlashMessage) = nullptr;

// Resolves the entry points above on first call; thread-safe and idempotent.
// The module is deliberately left loaded for the lifetime of the process,
// which is what the delay-load helper did as well. Returns false when ZDSR is
// not installed or its client library does not export the full set, in which
// case every pointer above is null and the backend must report itself
// unavailable.
inline bool load() {
  static const bool loaded = [] {
#ifdef _WIN64
    constexpr const wchar_t *file_name = L"ZDSRAPI_x64.dll";
    constexpr const wchar_t *install_key = L"SOFTWARE\\WOW6432Node\\zhiduo\\zdsr";
#else
    constexpr const wchar_t *file_name = L"ZDSRAPI.dll";
    constexpr const wchar_t *install_key = L"SOFTWARE\\zhiduo\\zdsr";
#endif
    HMODULE module = load_vendor_library(file_name);
    if (module == nullptr)
      module = load_vendor_library_from_registry(HKEY_LOCAL_MACHINE, install_key,
                                                 L"path", file_name);
    if (module == nullptr)
      return false;
    resolve(module, Braille, "Braille");
    return resolve(module, InitTTS, "InitTTS") &&
           resolve(module, Speak, "Speak") &&
           resolve(module, GetSpeakState, "GetSpeakState") &&
           resolve(module, StopSpeak, "StopSpeak");
  }();
  return loaded;
}

} // namespace prism::raw::zdsr

#endif
