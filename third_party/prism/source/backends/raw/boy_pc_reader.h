// SPDX-License-Identifier: MPL-2.0

// BoyPC Reader's client library, resolved at runtime. See
// raw/dynamic_library.h for why these entry points are not imported through a
// .def-generated import library.

#pragma once
#ifdef _WIN32
#include <raw/dynamic_library.h>
#include <windows.h>

namespace prism::raw::boy_pc_reader {

typedef void(__stdcall *BoyCtrlSpeakCompleteFunc)(int reason);

typedef enum {
  e_bcerr_success = 0,
  e_bcerr_fail = 1,
  e_bcerr_arg = 2,
  e_bcerr_unavailable = 3,
} BoyCtrlError;

typedef enum {
  e_bcspf_none = 0,
  e_bcspf_withSlave = 1,
  e_bcspf_append = 2,
  e_bcspf_allowBreak = 4,
  e_bcspf_isReader = 8,
} BoyCtrlSpeakFlags;

typedef enum {
  e_bcirm_time,
  e_bcirm_date,
} BoyCtrlInfoReportMode;

// Required by the backend; non-null once load() has returned true.
inline BoyCtrlError(__stdcall *BoyCtrlInitializeU8)(const char *logPath) =
    nullptr;
inline BoyCtrlError(__stdcall *BoyCtrlSpeak)(
    const wchar_t *text, bool append,
    BoyCtrlSpeakCompleteFunc onCompletion) = nullptr;
inline BoyCtrlError(__stdcall *BoyCtrlStopSpeaking)() = nullptr;
inline void(__stdcall *BoyCtrlUninitialize)() = nullptr;
inline bool(__stdcall *BoyCtrlIsReaderRunning)() = nullptr;

// Optional; may be null even when load() succeeded, so check before calling.
// Upstream renamed this export BoyCtrlSetAnyKeyBreak in 0.17.3. Which spelling
// a given BoyCtrl build exports is not something we can verify from here, and
// no backend method calls it, so both names are tried and neither is allowed to
// decide whether the library loaded at all.
inline bool(__stdcall *BoyCtrlSetAnyKeyBreak)(bool withSlave) = nullptr;
inline BoyCtrlError(__stdcall *BoyCtrlInitialize)(const wchar_t *logPath) =
    nullptr;
inline BoyCtrlError(__stdcall *BoyCtrlInitializeAnsi)(const char *logPath) =
    nullptr;
inline BoyCtrlError(__stdcall *BoyCtrlSpeakEx)(
    const wchar_t *text, int flags,
    BoyCtrlSpeakCompleteFunc onCompletion) = nullptr;
inline BoyCtrlError(__stdcall *BoyCtrlSpeakAnsi)(
    const char *text, bool append,
    BoyCtrlSpeakCompleteFunc onCompletion) = nullptr;
inline BoyCtrlError(__stdcall *BoyCtrlSpeakU8)(
    const char *text, bool append,
    BoyCtrlSpeakCompleteFunc onCompletion) = nullptr;
inline BoyCtrlError(__stdcall *BoyCtrlStopSpeakingEx)(int flags) = nullptr;
inline BoyCtrlError(__stdcall *BoyCtrlPauseScreenReader)(int ms) = nullptr;
inline int(__stdcall *BoyCtrlGetReaderState)() = nullptr;
inline bool(__stdcall *BoyCtrlVerify)(const char *key) = nullptr;
inline bool(__stdcall *BoyCtrlReportInfo)(int mode) = nullptr;
inline bool(__stdcall *BoyCtrlStartTextToAudio)(
    int taskId, const wchar_t *inputFilePath, const wchar_t *outputFilePath,
    const wchar_t *speechCase, int interval, const wchar_t *format,
    unsigned hwnd, unsigned notifyBaseMsg) = nullptr;
inline bool(__stdcall *BoyCtrlCancelTextToAudio)(int taskId) = nullptr;
inline bool(__stdcall *BoyCtrlActivateYTApp)(const wchar_t *appName,
                                             unsigned msg, unsigned wParam,
                                             unsigned lParam) = nullptr;

// Resolves the entry points above on first call; thread-safe and idempotent.
// Returns false when BoyPC Reader is not installed or its client library is
// missing one of the required entry points.
//
// Two file names are tried: the vendor SDK and Tolk both ship the library as
// BoyCtrl.dll / BoyCtrl-x64.dll, while this backend was originally written
// against byctrl.dll / byctrl-x64.dll.
inline bool load() {
  static const bool loaded = [] {
#ifdef _WIN64
    constexpr const wchar_t *file_name = L"BoyCtrl-x64.dll";
    constexpr const wchar_t *legacy_file_name = L"byctrl-x64.dll";
    constexpr const wchar_t *install_key =
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
        L"\\{1F0FDAE0-3E94-4B86-8F08-C68E70D5D87D}_is1";
#else
    constexpr const wchar_t *file_name = L"BoyCtrl.dll";
    constexpr const wchar_t *legacy_file_name = L"byctrl.dll";
    constexpr const wchar_t *install_key =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
        L"\\{1F0FDAE0-3E94-4B86-8F08-C68E70D5D87D}_is1";
#endif
    HMODULE module = load_vendor_library(file_name);
    if (module == nullptr)
      module = load_vendor_library(legacy_file_name);
    if (module == nullptr)
      module = load_vendor_library_from_registry(
          HKEY_LOCAL_MACHINE, install_key, L"InstallLocation", file_name);
    if (module == nullptr)
      module = load_vendor_library_from_registry(
          HKEY_LOCAL_MACHINE, install_key, L"InstallLocation",
          legacy_file_name);
    if (module == nullptr)
      return false;
    resolve(module, BoyCtrlInitialize, "BoyCtrlInitialize");
    resolve(module, BoyCtrlInitializeAnsi, "BoyCtrlInitializeAnsi");
    resolve(module, BoyCtrlSpeakEx, "BoyCtrlSpeakEx");
    resolve(module, BoyCtrlSpeakAnsi, "BoyCtrlSpeakAnsi");
    resolve(module, BoyCtrlSpeakU8, "BoyCtrlSpeakU8");
    resolve(module, BoyCtrlStopSpeakingEx, "BoyCtrlStopSpeakingEx");
    resolve(module, BoyCtrlPauseScreenReader, "BoyCtrlPauseScreenReader");
    resolve(module, BoyCtrlGetReaderState, "BoyCtrlGetReaderState");
    resolve(module, BoyCtrlVerify, "BoyCtrlVerify");
    resolve(module, BoyCtrlReportInfo, "BoyCtrlReportInfo");
    resolve(module, BoyCtrlStartTextToAudio, "BoyCtrlStartTextToAudio");
    resolve(module, BoyCtrlCancelTextToAudio, "BoyCtrlCancelTextToAudio");
    resolve(module, BoyCtrlActivateYTApp, "BoyCtrlActivateYTApp");
    if (!resolve(module, BoyCtrlSetAnyKeyBreak, "BoyCtrlSetAnyKeyBreak"))
      resolve(module, BoyCtrlSetAnyKeyBreak, "BoyCtrlSetAnyKeyStopSpeaking");
    return resolve(module, BoyCtrlInitializeU8, "BoyCtrlInitializeU8") &&
           resolve(module, BoyCtrlSpeak, "BoyCtrlSpeak") &&
           resolve(module, BoyCtrlStopSpeaking, "BoyCtrlStopSpeaking") &&
           resolve(module, BoyCtrlUninitialize, "BoyCtrlUninitialize") &&
           resolve(module, BoyCtrlIsReaderRunning, "BoyCtrlIsReaderRunning");
  }();
  return loaded;
}

} // namespace prism::raw::boy_pc_reader

#endif
