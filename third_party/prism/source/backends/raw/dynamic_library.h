// SPDX-License-Identifier: MPL-2.0

// Helper for backends that talk to a screen reader's vendor client library
// (ZDSR, BoyPC Reader, PC-Talker). Those libraries are resolved at runtime
// rather than through a generated import library plus /delayload, because:
//
//   * The 32-bit builds of these libraries export undecorated names, while an
//     import library generated from a .def for __stdcall functions asks for
//     the decorated forms (InitTTS@12, BoyCtrlSpeak@12, ...). No shipped
//     vendor DLL exports those, so on 32-bit builds the delay-load helper
//     could never bind. It could not fall back on the stub table in
//     source/delayimp.cpp either, since that table is keyed on the
//     undecorated names, so every call raised ERROR_PROC_NOT_FOUND
//     (0xC06D007F) instead of degrading to a stub.
//   * A missing or partial vendor DLL raises a structured exception out of the
//     delay-load helper, which propagates through initialize() and takes the
//     host process down with it. Resolving explicitly turns "this reader is
//     not installed" into an ordinary backend-unavailable result.
//   * Delay loading is not supported under MinGW; runtime resolution is.
//
// The search order below reproduces what the delay-load failure hook did for
// these libraries: the standard search order, then prism's own directory, then
// the reader's installation path from the registry.

#pragma once
#ifdef _WIN32
#include <string>
#include <windows.h>

namespace prism::raw {

// GetProcAddress with the function-pointer cast folded in. Leaves `fn` null
// and returns false when the export is absent, so a caller can require some
// entry points and treat others as optional.
template <typename Fn>
inline bool resolve(HMODULE module, Fn &fn, const char *symbol) {
  fn = reinterpret_cast<Fn>(
      reinterpret_cast<void (*)()>(GetProcAddress(module, symbol)));
  return fn != nullptr;
}

// Path to `file_name` in the directory prism itself was loaded from, so a host
// application can ship a vendor client library next to prism. Empty on
// failure.
inline std::wstring module_relative_path(const wchar_t *file_name) {
  static const int anchor = 0;
  HMODULE self = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&anchor), &self) == 0)
    return {};
  std::wstring buffer(MAX_PATH, L'\0');
  const DWORD len =
      GetModuleFileNameW(self, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (len == 0 || len >= buffer.size())
    return {};
  buffer.resize(len);
  const auto separator = buffer.find_last_of(L"\\/");
  if (separator == std::wstring::npos)
    return {};
  buffer.resize(separator + 1);
  buffer += file_name;
  return buffer;
}

// Loads `file_name` through the standard search order, then from prism's own
// directory. Null when neither location has it.
inline HMODULE load_vendor_library(const wchar_t *file_name) {
  if (HMODULE module = LoadLibraryW(file_name); module != nullptr)
    return module;
  const auto path = module_relative_path(file_name);
  if (path.empty())
    return nullptr;
  return LoadLibraryW(path.c_str());
}

// Loads `file_name` from an installation directory named by a registry value,
// e.g. the path ZDSR records under HKLM\SOFTWARE\zhiduo\zdsr. Null when the
// key, the value or the file is missing.
inline HMODULE load_vendor_library_from_registry(HKEY root,
                                                 const wchar_t *sub_key,
                                                 const wchar_t *value,
                                                 const wchar_t *file_name) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, sub_key, 0, KEY_QUERY_VALUE | KEY_READ, &key) !=
      ERROR_SUCCESS)
    return nullptr;
  std::wstring path(MAX_PATH, L'\0');
  auto size = static_cast<DWORD>(path.size() * sizeof(wchar_t));
  const auto res =
      RegQueryValueExW(key, value, nullptr, nullptr,
                       reinterpret_cast<LPBYTE>(path.data()), &size);
  RegCloseKey(key);
  if (res != ERROR_SUCCESS)
    return nullptr;
  path.resize(wcsnlen(path.c_str(), path.size()));
  if (path.empty())
    return nullptr;
  if (path.back() != L'\\' && path.back() != L'/')
    path += L'\\';
  path += file_name;
  return LoadLibraryW(path.c_str());
}

} // namespace prism::raw

#endif
