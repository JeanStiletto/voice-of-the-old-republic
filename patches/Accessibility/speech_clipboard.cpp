#include "speech_clipboard.h"

#include <windows.h>
#include <cstdlib>
#include <cstring>

// user32.lib for the clipboard API. The patch build links only sqlite3 by
// default; pragma keeps the link config local to the TU that needs it.
#pragma comment(lib, "user32.lib")

#include "hotkeys.h"
#include "log.h"
#include "prism.h"
#include "strings.h"

namespace acc::speech_clipboard {

namespace {

using S = acc::strings::Id;

// OpenClipboard fails while another process holds the clipboard open. That
// window is normally sub-millisecond, so a few retries beat failing the copy.
// The retries are affordable because this runs on a worker thread, not on the
// engine's main thread — see RunCopy.
constexpr int kOpenAttempts = 16;

// ---- Why the copy runs on its own thread ---------------------------------
//
// The engine's main thread is where the message loop, the DirectInput
// dispatch and our whole tick live, and prism's SAPI backend has already put
// it in an STA (logged at startup as "apartment[post_prism_init]: MainSTA").
// Clipboard calls from an STA are not inert: EmptyClipboard notifies the
// previous owner, SetClipboardData wakes every clipboard listener on the
// system — NVDA and Windows 11's clipboard history among them — and the
// cross-apartment round trips that follow can pump messages underneath us.
// The codebase already refuses to start WinHTTP on this thread for the same
// class of reason (see the update_checker note in core_dllmain.cpp).
//
// (The input freeze seen in the first live tests was NOT this — it was the
// OS opening its Alt menu mode, fixed by swallowing SC_KEYMENU in
// focus_guard.cpp. Measured worker time is 0 ms. The thread stays because
// waking every clipboard listener on the system still does not belong on the
// engine's message-loop thread.)
//
// Handshake: the tick owns the text and the speech; the worker owns nothing
// but a private copy of the string. Result comes back through s_result and is
// spoken by the next tick, so the confirmation still reports what actually
// happened rather than what we hoped would happen.
enum Result : LONG { kNone = 0, kOk = 1, kFailed = 2 };
volatile LONG s_busy   = 0;      // 1 while a worker is in flight
volatile LONG s_result = kNone;  // worker -> tick

// Speak mod feedback without letting it become the thing Ctrl+R copies next
// time. Without the suppress the second press would copy "Copied to
// clipboard" instead of re-copying the text the user actually wanted.
void SpeakFeedback(S id) {
    const char* text = acc::strings::Get(id);
    if (!text || !*text) return;
    prism::SuppressNextCapture();
    prism::Speak(text, /*interrupt=*/true);
}

// Publish `text` (game codepage) as CF_UNICODETEXT. Unicode rather than
// CF_TEXT so umlauts / Cyrillic survive the paste; Windows synthesises CF_TEXT
// from it for consumers that want ANSI, so one format is enough.
bool CopyToClipboard(const char* text) {
    const UINT cp = prism::GetSpeechCodepage();
    int wideLen = MultiByteToWideChar(cp, 0, text, -1, nullptr, 0);
    if (wideLen <= 0) {
        acclog::Write("Clipboard", "MultiByteToWideChar(cp=%u) failed, err=%lu",
                      cp, GetLastError());
        return false;
    }

    bool opened = false;
    for (int i = 0; i < kOpenAttempts; ++i) {
        if (OpenClipboard(nullptr)) { opened = true; break; }
        Sleep(1);
    }
    if (!opened) {
        acclog::Write("Clipboard", "OpenClipboard failed after %d attempts, err=%lu",
                      kOpenAttempts, GetLastError());
        return false;
    }

    bool ok = false;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE,
                              static_cast<SIZE_T>(wideLen) * sizeof(wchar_t));
    if (mem) {
        wchar_t* dst = static_cast<wchar_t*>(GlobalLock(mem));
        if (dst) {
            int got = MultiByteToWideChar(cp, 0, text, -1, dst, wideLen);
            GlobalUnlock(mem);
            if (got > 0) {
                EmptyClipboard();
                // On success the clipboard owns the handle — do NOT free it.
                if (SetClipboardData(CF_UNICODETEXT, mem) != nullptr) {
                    ok  = true;
                    mem = nullptr;
                } else {
                    acclog::Write("Clipboard", "SetClipboardData failed, err=%lu",
                                  GetLastError());
                }
            }
        }
        if (mem) GlobalFree(mem);
    } else {
        acclog::Write("Clipboard", "GlobalAlloc(%d wchars) failed", wideLen);
    }

    CloseClipboard();
    return ok;
}

// Worker entry. `param` is a private heap copy of the text, owned by us.
DWORD WINAPI RunCopy(LPVOID param) {
    char* text = static_cast<char*>(param);
    DWORD started = GetTickCount();
    bool  ok      = CopyToClipboard(text);
    acclog::Write("Clipboard", "worker %s after %lums, %zu chars: %s",
                  ok ? "ok" : "FAILED", GetTickCount() - started,
                  strlen(text), text);
    free(text);
    InterlockedExchange(&s_result, ok ? kOk : kFailed);
    InterlockedExchange(&s_busy, 0);
    return 0;
}

}  // namespace

void PollWin32() {
    // Speak the previous press's outcome first — the worker finished at some
    // point between ticks and the confirmation has to come from this thread.
    LONG done = InterlockedExchange(&s_result, kNone);
    if (done == kOk)          SpeakFeedback(S::ClipboardCopied);
    else if (done == kFailed) SpeakFeedback(S::ClipboardFailed);

    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::CopyLastSpoken)) return;

    // No game-state gate on purpose: the value of this key is that it behaves
    // identically in dialog, in the journal, in a menu and in the world.
    const char* text = prism::LastSpoken();
    if (!text || !*text) {
        acclog::Write("Clipboard", "Ctrl+R with nothing spoken yet");
        SpeakFeedback(S::ClipboardNothingToCopy);
        return;
    }

    if (InterlockedCompareExchange(&s_busy, 1, 0) != 0) {
        // A previous copy is still in flight. Dropping the press is right:
        // its confirmation is about to be spoken anyway, and the text it is
        // copying is the same text this press would copy.
        acclog::Write("Clipboard", "Ctrl+R ignored — a copy is still in flight");
        return;
    }

    size_t len  = strlen(text);
    char*  copy = static_cast<char*>(malloc(len + 1));
    if (!copy) {
        InterlockedExchange(&s_busy, 0);
        acclog::Write("Clipboard", "malloc(%zu) failed", len + 1);
        SpeakFeedback(S::ClipboardFailed);
        return;
    }
    memcpy(copy, text, len + 1);

    HANDLE th = CreateThread(nullptr, 0, RunCopy, copy, 0, nullptr);
    if (!th) {
        acclog::Write("Clipboard", "CreateThread failed, err=%lu", GetLastError());
        free(copy);
        InterlockedExchange(&s_busy, 0);
        SpeakFeedback(S::ClipboardFailed);
        return;
    }
    CloseHandle(th);
}

}  // namespace acc::speech_clipboard
