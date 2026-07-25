// SPDX-License-Identifier: MPL-2.0

// PC-Talker's client library, resolved at runtime. See raw/dynamic_library.h
// for why these entry points are not imported through a .def-generated import
// library.

#pragma once
#ifdef _WIN32
#include <raw/dynamic_library.h>
#include <windows.h>

#define PS_STATUS       0x10001
#define PS_PREV         0x10002
#define PS_NEXT         0x10003
#define PS_SETEX        0x20001
#define PS_UIACTION     0x20002
#define PS_UIACTION2    0x20003
#define PKDI_KEYGUIDE   0x18
#define PKDI_IME        0x13
#define PKDI_CURSOR     0x1C
#define PKDI_MOUSE      0x1D
#define PCTK_PRIORITY_DEFAULT   0
#define PCTK_PRIORITY_LOW       1
#define PCTK_PRIORITY_HIGH      2
#define PCTK_PRIORITY_OVERRIDE  3
#define PCTK_SPEAK_FORCE        0x0001
#define PCTK_SPEAK_RAW          0x0002
#define PCTK_SPEAK_EXTANALYSIS  0x0004
#define PCTK_SPEAK_SYNC         0x0040
#define PCTK_PIN_MODE_DEFAULT   0x00000000u
#define PCTK_PIN_MODE_1         0x40000000u
#define PCTK_PIN_MODE_2         0x80000000u
#define PCTK_PIN_MODE_3         0xC0000000u
#define PCTK_PIN_MODE_MASK      0xC0000000u

namespace prism::raw::pc_talker {

// Required by the backend; non-null once load() has returned true.
inline BOOL(__stdcall *PCTKStatus)(void) = nullptr;
inline BOOL(__stdcall *PCTKPReadW)(const wchar_t *text, int priority,
                                   BOOL analyze) = nullptr;
inline void(__stdcall *PCTKVReset)(void) = nullptr;
inline BOOL(__stdcall *PCTKGetVStatus)(void) = nullptr;
inline BOOL(__stdcall *PCTKPinStatus)(void) = nullptr;
inline BOOL(__stdcall *PCTKPinFocusW)(LONG_PTR context, const wchar_t *text,
                                      DWORD disp_flags, const wchar_t *aux_text,
                                      LONG_PTR aux_param) = nullptr;
inline BOOL(__stdcall *PCTKPinIsFocus)(LONG_PTR context) = nullptr;
inline BOOL(__stdcall *PCTKPinWriteW)(const wchar_t *text, int mode,
                                      int flags) = nullptr;

// Optional; may be null even when load() succeeded, so check before calling.
inline DWORD(__stdcall *PCTKGetVersion)(void) = nullptr;
inline BOOL(__stdcall *PCTKPRead)(const char *text, int priority,
                                  BOOL analyze) = nullptr;
inline BOOL(__stdcall *PCTKPReadEx)(const char *text, int priority,
                                    BOOL analyze, int flags) = nullptr;
inline BOOL(__stdcall *PCTKPReadExW)(const wchar_t *text, int priority,
                                     BOOL analyze, int flags) = nullptr;
inline void(__stdcall *PCTKVoiceGuide)(const char *text) = nullptr;
inline void(__stdcall *PCTKBeep)(int type, int param) = nullptr;
inline BOOL(__stdcall *PCTKCGuide)(const char *text, DWORD mode) = nullptr;
inline BOOL(__stdcall *PCTKCGuideEx)(const char *text, DWORD mode,
                                     int flags) = nullptr;
inline BOOL(__stdcall *PCTKCGuideW)(const wchar_t *text, DWORD mode) = nullptr;
inline BOOL(__stdcall *PCTKCGuideExW)(const wchar_t *text, DWORD mode,
                                      int flags) = nullptr;
inline DWORD(__stdcall *PCTKGetStatus)(UINT item, LPVOID param1,
                                       LPVOID param2) = nullptr;
inline DWORD(__stdcall *PCTKSetStatus)(UINT item, LPVOID param1,
                                       LPVOID param2) = nullptr;
inline int(__stdcall *PCTKCommand)(const char *cmdstr, LPARAM param1,
                                   LPARAM param2) = nullptr;
inline BOOL(__stdcall *PCTKLoadUserDict)(void) = nullptr;
inline BOOL(__stdcall *PCTKPinFocus)(LONG_PTR context, const char *text,
                                     DWORD disp_flags, const char *aux_text,
                                     LONG_PTR aux_param) = nullptr;
inline BOOL(__stdcall *PCTKPinWrite)(const char *text, int mode,
                                     int flags) = nullptr;
inline BOOL(__stdcall *PCTKPinEDWrite)(const char *text, int edit_mode,
                                       int cursor_pos, int sel_start,
                                       int sel_len, int line_offset,
                                       int char_attr) = nullptr;
inline BOOL(__stdcall *PCTKPinEDWriteW)(const wchar_t *text, int edit_mode,
                                        int cursor_pos, int sel_start,
                                        int sel_len, int line_offset,
                                        int char_attr) = nullptr;
inline BOOL(__stdcall *PCTKPinStatusCell)(const void *cell_data,
                                          void *out_buf) = nullptr;
inline BOOL(__stdcall *PCTKPinStatusCellW)(const void *cell_data,
                                           void *out_buf) = nullptr;
inline void(__stdcall *PCTKPinReset)(void) = nullptr;
inline BOOL(__stdcall *SoundMessage)(const char *text, int flags) = nullptr;
inline BOOL(__stdcall *SoundStatus)(void) = nullptr;
inline BOOL(__stdcall *SoundModifyMode)(int on, int off) = nullptr;
inline BOOL(__stdcall *SoundPause)(BOOL sw) = nullptr;
inline int(__stdcall *AGSEvent)(int event_type, LPARAM param1,
                                LPARAM param2) = nullptr;
inline LONGLONG(__stdcall *GetUIActionMode)(void) = nullptr;
inline BOOL(__stdcall *IsImmInput)(HWND hwnd) = nullptr;
inline int(__stdcall *PCTKEventHook)(void) = nullptr;
inline int(__stdcall *PCTKGetVoiceLog)(void) = nullptr;
inline int(__stdcall *SetKeyBreak)(void) = nullptr;
inline void(__stdcall *EncodeFlags)(void) = nullptr;
inline int(__stdcall *dic_regist)(void) = nullptr;
inline int(__stdcall *dic_regist_detail)(void) = nullptr;
inline int(__stdcall *dic_reg_from_file)(void) = nullptr;
inline int(__stdcall *dic_text_out)(void) = nullptr;
inline int(__stdcall *dic_reg_detail_from_file)(void) = nullptr;
inline int(__stdcall *dic_detail_text_out)(void) = nullptr;

// PCTKUSR.dll also exports all-uppercase aliases of most of the above
// (PCTKSTATUS, PCTKPREADW, ...). They are not declared here because nothing
// calls them; add them as further optional pointers if that changes.

// Resolves the entry points above on first call; thread-safe and idempotent.
// Returns false when PC-Talker is not installed or its client library is
// missing one of the required entry points.
inline bool load() {
  static const bool loaded = [] {
    const HMODULE module = load_vendor_library(L"PCTKUSR.dll");
    if (module == nullptr)
      return false;
    resolve(module, PCTKGetVersion, "PCTKGetVersion");
    resolve(module, PCTKPRead, "PCTKPRead");
    resolve(module, PCTKPReadEx, "PCTKPReadEx");
    resolve(module, PCTKPReadExW, "PCTKPReadExW");
    resolve(module, PCTKVoiceGuide, "PCTKVoiceGuide");
    resolve(module, PCTKBeep, "PCTKBeep");
    resolve(module, PCTKCGuide, "PCTKCGuide");
    resolve(module, PCTKCGuideEx, "PCTKCGuideEx");
    resolve(module, PCTKCGuideW, "PCTKCGuideW");
    resolve(module, PCTKCGuideExW, "PCTKCGuideExW");
    resolve(module, PCTKGetStatus, "PCTKGetStatus");
    resolve(module, PCTKSetStatus, "PCTKSetStatus");
    resolve(module, PCTKCommand, "PCTKCommand");
    resolve(module, PCTKLoadUserDict, "PCTKLoadUserDict");
    resolve(module, PCTKPinFocus, "PCTKPinFocus");
    resolve(module, PCTKPinWrite, "PCTKPinWrite");
    resolve(module, PCTKPinEDWrite, "PCTKPinEDWrite");
    resolve(module, PCTKPinEDWriteW, "PCTKPinEDWriteW");
    resolve(module, PCTKPinStatusCell, "PCTKPinStatusCell");
    resolve(module, PCTKPinStatusCellW, "PCTKPinStatusCellW");
    resolve(module, PCTKPinReset, "PCTKPinReset");
    resolve(module, SoundMessage, "SoundMessage");
    resolve(module, SoundStatus, "SoundStatus");
    resolve(module, SoundModifyMode, "SoundModifyMode");
    resolve(module, SoundPause, "SoundPause");
    resolve(module, AGSEvent, "AGSEvent");
    resolve(module, GetUIActionMode, "GetUIActionMode");
    resolve(module, IsImmInput, "IsImmInput");
    resolve(module, PCTKEventHook, "PCTKEventHook");
    resolve(module, PCTKGetVoiceLog, "PCTKGetVoiceLog");
    resolve(module, SetKeyBreak, "SetKeyBreak");
    resolve(module, EncodeFlags, "EncodeFlags");
    resolve(module, dic_regist, "dic_regist");
    resolve(module, dic_regist_detail, "dic_regist_detail");
    resolve(module, dic_reg_from_file, "dic_reg_from_file");
    resolve(module, dic_text_out, "dic_text_out");
    resolve(module, dic_reg_detail_from_file, "dic_reg_detail_from_file");
    resolve(module, dic_detail_text_out, "dic_detail_text_out");
    return resolve(module, PCTKStatus, "PCTKStatus") &&
           resolve(module, PCTKPReadW, "PCTKPReadW") &&
           resolve(module, PCTKVReset, "PCTKVReset") &&
           resolve(module, PCTKGetVStatus, "PCTKGetVStatus") &&
           resolve(module, PCTKPinStatus, "PCTKPinStatus") &&
           resolve(module, PCTKPinFocusW, "PCTKPinFocusW") &&
           resolve(module, PCTKPinIsFocus, "PCTKPinIsFocus") &&
           resolve(module, PCTKPinWriteW, "PCTKPinWriteW");
  }();
  return loaded;
}

} // namespace prism::raw::pc_talker

#endif
