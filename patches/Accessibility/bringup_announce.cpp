#include "bringup_announce.h"

#include <windows.h>
#include <atomic>
#include <cstring>

#include "log.h"
#include "prism.h"
#include "strings.h"

namespace acc::bringup_announce {

namespace {

enum class Phase : int {
    Booting       = 0,   // no game window in foreground yet
    MoviesPlaying = 1,   // foreground is SWMovieWindow — stay silent
    Loading       = 2,   // foreground is Render Window, intros done,
                         //   pump not yet live — announce nag window
    Responsive    = 3,   // input pump confirmed live — done
};

const char* PhaseName(Phase p) {
    switch (p) {
        case Phase::Booting:       return "Booting";
        case Phase::MoviesPlaying: return "MoviesPlaying";
        case Phase::Loading:       return "Loading";
        case Phase::Responsive:    return "Responsive";
    }
    return "?";
}

std::atomic<Phase>     g_phase{Phase::Booting};
std::atomic<bool>      g_seenMovie{false};       // sticky — set once any SWMovieWindow goes foreground
std::atomic<bool>      g_announcedWait{false};   // stage 1: "please wait" said
std::atomic<bool>      g_announcedStuck{false};  // stage 2: "press Alt F4" said
std::atomic<ULONGLONG> g_waitNagMs{0};           // wall clock at stage-1 fire (for stage-2 elapsed check)
std::atomic<HANDLE>    g_thread{nullptr};

// User has to be still pressing nav keys ≥this long after stage 1 for
// stage 2 to fire. Engine recovers within a few seconds in most cases,
// so 15 s is a comfortable "nope, it's actually stuck" threshold.
constexpr DWORD kStuckThresholdMs = 15000;

// True iff `hwnd` belongs to the current process AND its class is the
// given target. SEH-free — class-name lookup tolerates dead handles
// (returns 0, comparison fails, we report "not us").
bool IsOurWindowOfClass(HWND hwnd, const char* targetClass) {
    if (!hwnd) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) return false;
    char cls[64] = {};
    if (GetClassNameA(hwnd, cls, sizeof(cls)) <= 0) return false;
    return strcmp(cls, targetClass) == 0;
}

void TransitionTo(Phase next, const char* reason) {
    Phase prev = g_phase.exchange(next, std::memory_order_acq_rel);
    if (prev == next) return;
    acclog::Write("BringupAnnounce",
        "phase %s -> %s (%s)",
        PhaseName(prev), PhaseName(next), reason);
    // Phase change → reset both nag latches + the stage-1 timestamp so
    // the user gets a fresh chance to be told if they enter Loading
    // again later (e.g. movies → loading → movies → loading cycle).
    g_announcedWait.store(false, std::memory_order_release);
    g_announcedStuck.store(false, std::memory_order_release);
    g_waitNagMs.store(0, std::memory_order_release);
}

// Map current foreground-window class to a phase. Returns the current
// phase unchanged if the foreground window is foreign (Steam overlay,
// taskbar, etc.) — we only react to OUR windows.
Phase ClassifyForeground() {
    HWND fg = GetForegroundWindow();
    if (IsOurWindowOfClass(fg, "SWMovieWindow")) {
        g_seenMovie.store(true, std::memory_order_release);
        return Phase::MoviesPlaying;
    }
    if (IsOurWindowOfClass(fg, "Render Window")) {
        // Only call this Loading if movies have already played AND the
        // pump isn't live yet. Render Window before any movie = early
        // bootstrap; Render Window after Responsive = normal gameplay.
        if (g_phase.load(std::memory_order_acquire) == Phase::Responsive) {
            return Phase::Responsive;
        }
        if (g_seenMovie.load(std::memory_order_acquire)) {
            return Phase::Loading;
        }
        return Phase::Booting;  // pre-movie Render Window
    }
    return g_phase.load(std::memory_order_acquire);
}

bool UserNavKeyHeld() {
    // GetAsyncKeyState's high bit = currently down. Polling at 100ms
    // catches anything held for ≥100ms, which is essentially every
    // human keypress.
    static const int kKeys[] = {
        VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
        VK_RETURN, VK_SPACE,
    };
    for (int vk : kKeys) {
        if (GetAsyncKeyState(vk) & 0x8000) return true;
    }
    return false;
}

DWORD WINAPI PollProc(LPVOID) {
    acclog::Write("BringupAnnounce",
        "polling thread started (pid=%lu, tid=%lu)",
        GetCurrentProcessId(), GetCurrentThreadId());
    for (;;) {
        Phase classified = ClassifyForeground();
        Phase current    = g_phase.load(std::memory_order_acquire);
        if (classified != current) {
            // Don't allow regression from Responsive (gameplay must
            // stay quiet about loading even if focus bounces).
            if (current == Phase::Responsive) {
                // ignore
            } else {
                TransitionTo(classified, "foreground class match");
            }
        }
        // Two-stage nag check: only during Loading.
        //   Stage 1: first nav-key press → "please wait" (most sessions
        //            recover within a few seconds, so this is the
        //            polite default).
        //   Stage 2: ≥kStuckThresholdMs after stage 1 AND user is still
        //            pressing nav keys AND still in Loading → upgrade
        //            to "press Alt F4 and cancel the dialog" because
        //            spontaneous recovery is unlikely at this point.
        //            Engine-side bug: the panel-activation queue
        //            doesn't drain until poked, and the dialog-construct
        //            + dialog-cancel cycle is the known poke.
        if (g_phase.load(std::memory_order_acquire) == Phase::Loading) {
            bool wait_already  = g_announcedWait.load(std::memory_order_acquire);
            bool stuck_already = g_announcedStuck.load(std::memory_order_acquire);
            if (!wait_already && UserNavKeyHeld()) {
                g_announcedWait.store(true, std::memory_order_release);
                g_waitNagMs.store(GetTickCount64(), std::memory_order_release);
                const char* msg = acc::strings::Get(
                    acc::strings::Id::LoadingPleaseWait);
                acclog::Write("BringupAnnounce",
                    "stage 1: nav-key pressed during Loading — speaking \"%s\"",
                    msg);
                prism::Speak(msg, /*interrupt=*/true);
            } else if (wait_already && !stuck_already && UserNavKeyHeld()) {
                ULONGLONG since = GetTickCount64() -
                    g_waitNagMs.load(std::memory_order_acquire);
                if (since >= kStuckThresholdMs) {
                    g_announcedStuck.store(true, std::memory_order_release);
                    const char* msg = acc::strings::Get(
                        acc::strings::Id::LoadingStuckWorkaround);
                    acclog::Write("BringupAnnounce",
                        "stage 2: still in Loading %llums after stage 1 — speaking \"%s\"",
                        static_cast<unsigned long long>(since), msg);
                    prism::Speak(msg, /*interrupt=*/true);
                }
            }
        }
        Sleep(100);
    }
}

}  // namespace

void Start() {
    HANDLE expected = nullptr;
    HANDLE created = CreateThread(
        nullptr, 0, PollProc, nullptr, CREATE_SUSPENDED, nullptr);
    if (!created) {
        acclog::Write("BringupAnnounce",
            "Start: CreateThread failed err=%lu", GetLastError());
        return;
    }
    if (!g_thread.compare_exchange_strong(expected, created)) {
        CloseHandle(created);
        return;
    }
    ResumeThread(created);
    acclog::Write("BringupAnnounce",
        "Start: polling thread launched (handle=%p)", created);
}

void NotifyInputPumpLive() {
    TransitionTo(Phase::Responsive, "input pump confirmed live");
}

}  // namespace acc::bringup_announce
