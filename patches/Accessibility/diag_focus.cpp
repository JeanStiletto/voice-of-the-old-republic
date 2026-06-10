#include "diag_focus.h"

#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "engine_input.h"
#include "log.h"

#pragma comment(lib, "ole32.lib")

namespace acc::diag::focus {

namespace {

// KOTOR has multiple game-owned top-level windows (SWMovieWindow on the
// movie-player thread, "Render Window" on the main render thread, an
// invisible "Exo - BioWare Corp." legacy window). Focus can route to
// any of them, so we subclass all game windows simultaneously and tag
// every event with the source class name.
//
// Append-only table: the polling thread is the sole writer; SubclassProc
// is a hot-path reader called from per-message wndproc dispatch on
// arbitrary threads. Writers fully populate the entry's fields BEFORE
// publishing via the release-store of g_subclassedCount; readers do
// acquire-load on the count and then scan up to that many entries.
// Lock-free + correct on any sane memory model.
struct SubclassedWindow {
    HWND     hwnd;
    WNDPROC  origWndProc;
    char     tag[40];          // class name, for log disambiguation
};

// KOTOR creates fresh top-level windows on every phase transition
// (bootstrap → game, intro start/end, post-intro main menu). A clean
// startup with 3 intros consumes ~10 slots before main menu first-sight;
// stress-test alt-tab cycles add more. 64 buys us a comfortable headroom
// for a full session up to and beyond main menu without filling.
// Append-only — never reused — so the lock-free reader path stays safe.
constexpr int kMaxSubclassedWindows = 64;
SubclassedWindow         g_subclassed[kMaxSubclassedWindows] = {};
std::atomic<int>         g_subclassedCount{0};
std::atomic<HANDLE>      g_pollThread{nullptr};

// --- Cold-start foreground guard (Game Bar / overlay focus-theft) ---
//
// On a cold launch an overlay (notably the Xbox Game Bar popup) can steal
// the foreground window for a few seconds right as the main menu comes up.
// DirectInput's keyboard is acquired at foreground cooperative level, so
// while a foreign window owns the foreground the engine cannot Acquire it —
// the menu is keyboard-dead until the user alt-tabs the game back. This
// guard, armed at MainMenu first-sight, watches for that theft during a
// bounded window and pulls the game back to the foreground, at which point
// the engine's activation handler re-Acquires input on its own.
//
// kGuardWindowMs: how long after arming we keep watching. The observed
// Game Bar steal lands within ~6 s of the menu; 10 s gives margin without
// trapping a user who wants to leave (after this we never reclaim again).
// kMaxReclaims caps the reclaim attempts so a persistent overlay can't
// drag us into an endless focus war — once hit, we give up and disarm.
constexpr DWORD kGuardWindowMs = 10000;
constexpr int   kMaxReclaims   = 6;

std::atomic<DWORD> g_guardDeadlineTick{0};  // GetTickCount() deadline; 0 = disarmed
std::atomic<int>   g_guardReclaims{0};
std::atomic<HWND>  g_liveRenderWindow{nullptr};  // newest visible "Render Window"

const SubclassedWindow* FindSubclassed(HWND hwnd) {
    int n = g_subclassedCount.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        if (g_subclassed[i].hwnd == hwnd) return &g_subclassed[i];
    }
    return nullptr;
}

const char* ApartmentTypeName(APTTYPE t) {
    switch (t) {
        case APTTYPE_STA:     return "STA";
        case APTTYPE_MTA:     return "MTA";
        case APTTYPE_NA:      return "NA";
        case APTTYPE_MAINSTA: return "MainSTA";
        default:              return "?";
    }
}

const char* ApartmentQualifierName(APTTYPEQUALIFIER q) {
    switch (q) {
        case APTTYPEQUALIFIER_NONE:               return "NONE";
        case APTTYPEQUALIFIER_IMPLICIT_MTA:       return "IMPLICIT_MTA";
        case APTTYPEQUALIFIER_NA_ON_MTA:          return "NA_ON_MTA";
        case APTTYPEQUALIFIER_NA_ON_STA:          return "NA_ON_STA";
        case APTTYPEQUALIFIER_NA_ON_IMPLICIT_MTA: return "NA_ON_IMPLICIT_MTA";
        case APTTYPEQUALIFIER_NA_ON_MAINSTA:      return "NA_ON_MAINSTA";
        case APTTYPEQUALIFIER_APPLICATION_STA:    return "APPLICATION_STA";
        default:                                  return "?";
    }
}

const char* ActivateName(WORD w) {
    switch (w) {
        case WA_INACTIVE:    return "WA_INACTIVE";
        case WA_ACTIVE:      return "WA_ACTIVE";
        case WA_CLICKACTIVE: return "WA_CLICKACTIVE";
        default:             return "?";
    }
}

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const SubclassedWindow* entry = FindSubclassed(hwnd);
    const char* tag = entry ? entry->tag : "?";
    switch (msg) {
        case WM_ACTIVATE:
            acclog::Write("Focus",
                "WM_ACTIVATE %s minimized=%d otherHwnd=%p hwnd=%p [%s]",
                ActivateName(LOWORD(wp)),
                static_cast<int>(HIWORD(wp)),
                reinterpret_cast<void*>(lp), hwnd, tag);
            break;
        case WM_ACTIVATEAPP:
            acclog::Write("Focus",
                "WM_ACTIVATEAPP active=%d otherThread=%lu hwnd=%p [%s]",
                static_cast<int>(wp),
                static_cast<DWORD>(lp), hwnd, tag);
            // Focus regained. When KOTOR runs windowed and an external app
            // repeatedly steals the foreground, each regain forces the engine
            // to recreate its render window and the DirectInput `active` flag
            // can stick at 1 with the keyboard actually unacquired — input
            // goes dead, even in menus. Flag a reacquire for the next tick to
            // drive a real SetActive(0)->(1) edge (the software alt-tab). We
            // defer rather than call inline: this wndproc runs mid-pump inside
            // the engine's own activation handling, before the new render
            // window's DirectInput is fully rebound. See engine_input.h
            // RequestInputReacquire for the full mechanism.
            if (wp != 0) {
                acc::engine::RequestInputReacquire();
            }
            break;
        case WM_SETFOCUS:
            acclog::Write("Focus",
                "WM_SETFOCUS prevHwnd=%p hwnd=%p [%s]",
                reinterpret_cast<void*>(wp), hwnd, tag);
            break;
        case WM_KILLFOCUS:
            acclog::Write("Focus",
                "WM_KILLFOCUS nextHwnd=%p hwnd=%p [%s]",
                reinterpret_cast<void*>(wp), hwnd, tag);
            break;
        default:
            break;
    }
    if (entry && entry->origWndProc) {
        return CallWindowProcW(entry->origWndProc, hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// True for game-owned windows we want focus events from. KOTOR enumerated
// three: "SWMovieWindow" (movie player thread), "Render Window" (main
// render thread, where engine input pump lives), and "Exo - BioWare
// Corp., (c) 1999 - Generic Blank Application" (legacy invisible).
// Everything else from the PID's window list — MSCTFIME UI, IME, etc. —
// is OS-managed input infrastructure we don't care about.
bool IsGameWindowClass(const char* cls) {
    if (!cls || !*cls) return false;
    if (strcmp(cls, "MSCTFIME UI") == 0) return false;
    if (strcmp(cls, "IME") == 0) return false;
    // SAPI spins up a transient "CSpThreadTask Window" per speech
    // utterance on our own speech-engine threads. These are NOT game
    // windows: subclassing them cross-thread races SAPI's teardown, and
    // because they're created/destroyed per utterance they flooded this
    // append-only table — 57 of 64 slots in a single ~35-min session
    // (patch-20260601-210737.log), starving real game windows and
    // burying the log under "table full" spam.
    if (strcmp(cls, "CSpThreadTask Window") == 0) return false;
    // Never touch the Bink movie player's window. KOTOR's movie player
    // is fragile about window/focus churn during playback (cf. the
    // Alt+Tab-during-intros queue-restart bug); replacing its wndproc
    // cross-thread risks aborting the movie queue, which surfaces as the
    // game closing instead of starting the next queued movie. The phase
    // machine in bringup_announce already derives "movie playing" from
    // GetForegroundWindow without needing to subclass the window.
    if (strcmp(cls, "SWMovieWindow") == 0) return false;
    return true;
}

void LogOneWindow(HWND hwnd, const char* origin) {
    char cls[64] = {};
    char title[128] = {};
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowTextA(hwnd, title, sizeof(title));
    LONG style   = GetWindowLongA(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
    HWND parent  = GetParent(hwnd);
    HWND owner   = GetWindow(hwnd, GW_OWNER);
    DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    acclog::Write("Focus",
        "Windows[%s]: hwnd=%p class=\"%s\" title=\"%s\" "
        "style=0x%08lx exStyle=0x%08lx visible=%d parent=%p owner=%p "
        "thread=%lu",
        origin, hwnd, cls, title,
        static_cast<unsigned long>(style),
        static_cast<unsigned long>(exStyle),
        IsWindowVisible(hwnd) ? 1 : 0,
        parent, owner, thread);
}

struct EnumDiagState { DWORD pid; int count; };

BOOL CALLBACK EnumDiagProc(HWND hwnd, LPARAM lp) {
    auto* state = reinterpret_cast<EnumDiagState*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == state->pid) {
        LogOneWindow(hwnd, "top");
        ++state->count;
    }
    return TRUE;
}

void LogAllProcessWindows(const char* origin) {
    EnumDiagState state{ GetCurrentProcessId(), 0 };
    acclog::Write("Focus",
        "Windows[%s]: enumerating top-level windows for pid=%lu",
        origin, state.pid);
    EnumWindows(EnumDiagProc, reinterpret_cast<LPARAM>(&state));
    acclog::Write("Focus",
        "Windows[%s]: %d top-level window(s) in pid=%lu",
        origin, state.count, state.pid);
}

// Polling-thread scan: called every iteration. Single-writer; only this
// thread mutates g_subclassed[] / g_subclassedCount. Idempotent against
// already-subclassed HWNDs.
struct ScanState { DWORD pid; int newlySubclassed; HWND visibleRender; };

BOOL CALLBACK ScanProc(HWND hwnd, LPARAM lp) {
    auto* state = reinterpret_cast<ScanState*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != state->pid) return TRUE;

    char cls[64] = {};
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (!IsGameWindowClass(cls)) return TRUE;

    // Track the current live render window for the foreground guard. KOTOR
    // recreates the "Render Window" a few times during startup; only the
    // live one is visible, so filter on IsWindowVisible. Last writer in a
    // scan wins — in practice exactly one render window is visible.
    if (strcmp(cls, "Render Window") == 0 && IsWindowVisible(hwnd)) {
        state->visibleRender = hwnd;
    }

    int n = g_subclassedCount.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        if (g_subclassed[i].hwnd == hwnd) return TRUE;  // already subclassed
    }
    if (n >= kMaxSubclassedWindows) {
        // Dedup per hwnd — without it the polling thread spams the log
        // every 100ms for every unsubclassed window, burying useful data.
        // acclog::Once emits the first occurrence only per unique tag.
        char dedup[64];
        _snprintf_s(dedup, sizeof(dedup), _TRUNCATE,
                    "FocusPoll.tablefull.%p", hwnd);
        acclog::Once(dedup,
            "FocusPoll: table full (cap=%d), missing hwnd=%p class=\"%s\"",
            kMaxSubclassedWindows, hwnd, cls);
        return TRUE;
    }

    // Populate the entry BEFORE publishing the count bump.
    SubclassedWindow& slot = g_subclassed[n];
    slot.hwnd = hwnd;
    strncpy_s(slot.tag, cls, _TRUNCATE);
    slot.origWndProc = nullptr;

    LONG_PTR prev = SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(SubclassProc));
    if (!prev) {
        acclog::Write("Focus",
            "FocusPoll: SetWindowLongPtr failed hwnd=%p class=\"%s\" err=%lu",
            hwnd, cls, GetLastError());
        slot.hwnd = nullptr;  // unpublished — count stays unincremented
        return TRUE;
    }
    slot.origWndProc = reinterpret_cast<WNDPROC>(prev);
    // Release-store publishes the new entry to readers.
    g_subclassedCount.store(n + 1, std::memory_order_release);
    ++state->newlySubclassed;
    acclog::Write("Focus",
        "FocusPoll: subclassed hwnd=%p class=\"%s\" origWndProc=%p",
        hwnd, cls, slot.origWndProc);
    return TRUE;
}

// Pull `target` to the foreground from a thread that doesn't own it. The
// AttachThreadInput dance is the standard way around the Win32 foreground
// lock without injecting a synthetic ALT keypress (which a screen-reader
// user would not want). SEH-guarded — every call here is a Win32 boundary.
void ForceForeground(HWND target, HWND fg) {
    __try {
        DWORD myTid = GetCurrentThreadId();
        DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
        BOOL attached = FALSE;
        if (fgTid && fgTid != myTid) {
            attached = AttachThreadInput(myTid, fgTid, TRUE);
        }
        BringWindowToTop(target);
        SetForegroundWindow(target);
        if (attached) {
            AttachThreadInput(myTid, fgTid, FALSE);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Reclaim is best-effort; swallow any Win32-boundary fault.
    }
}

// Called once per poll iteration. While the guard is armed and within its
// window, reclaim the foreground if a non-game window has stolen it.
void MaybeReclaimForeground() {
    DWORD deadline = g_guardDeadlineTick.load(std::memory_order_acquire);
    if (deadline == 0) return;  // disarmed

    DWORD now = GetTickCount();
    if (static_cast<int>(now - deadline) >= 0) {  // wrap-safe elapsed check
        g_guardDeadlineTick.store(0, std::memory_order_release);
        acclog::Write("Focus",
            "StartupForegroundGuard: window elapsed; disarmed (reclaims=%d)",
            g_guardReclaims.load(std::memory_order_relaxed));
        return;
    }

    HWND render = g_liveRenderWindow.load(std::memory_order_acquire);
    if (!render || !IsWindow(render)) return;

    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == GetCurrentProcessId()) return;  // we already own foreground
    }

    int n = g_guardReclaims.load(std::memory_order_relaxed);
    if (n >= kMaxReclaims) {
        g_guardDeadlineTick.store(0, std::memory_order_release);
        acclog::Write("Focus",
            "StartupForegroundGuard: reclaim cap (%d) reached; giving up",
            kMaxReclaims);
        return;
    }
    g_guardReclaims.store(n + 1, std::memory_order_relaxed);

    acclog::Write("Focus",
        "StartupForegroundGuard: foreign window %p holds foreground; "
        "reclaiming render %p (attempt %d/%d)",
        fg, render, n + 1, kMaxReclaims);
    ForceForeground(render, fg);
}

DWORD WINAPI PollProc(LPVOID) {
    acclog::Write("Focus",
        "FocusPoll: thread started (pid=%lu, tid=%lu); scanning every 100ms",
        GetCurrentProcessId(), GetCurrentThreadId());

    // First pass: log baseline window inventory so we have a snapshot
    // of "what existed when the polling thread woke up". After this,
    // only newly-subclassed windows get individual log lines.
    LogAllProcessWindows("poll_start");

    int iter = 0;
    for (;;) {
        ScanState st{ GetCurrentProcessId(), 0, nullptr };
        EnumWindows(ScanProc, reinterpret_cast<LPARAM>(&st));
        if (st.visibleRender) {
            g_liveRenderWindow.store(st.visibleRender, std::memory_order_release);
        }
        MaybeReclaimForeground();
        Sleep(100);
        ++iter;
        // Heartbeat once per minute so we know the thread is still
        // alive (silent intervals during hangs are otherwise ambiguous).
        if (iter % 600 == 0) {
            acclog::Write("Focus",
                "FocusPoll: heartbeat iter=%d subclassed=%d",
                iter, g_subclassedCount.load(std::memory_order_relaxed));
        }
    }
}

}  // namespace

void LogComApartment(const char* tag) {
    APTTYPE          apt = APTTYPE_STA;
    APTTYPEQUALIFIER qual = APTTYPEQUALIFIER_NONE;
    HRESULT hr = CoGetApartmentType(&apt, &qual);
    if (FAILED(hr)) {
        acclog::Write("Prism",
            "apartment[%s]: CoGetApartmentType hr=0x%lx (thread uninitialized)",
            tag, static_cast<unsigned long>(hr));
        return;
    }
    acclog::Write("Prism",
        "apartment[%s]: %s (type=%d) qualifier=%s",
        tag, ApartmentTypeName(apt), static_cast<int>(apt),
        ApartmentQualifierName(qual));
}

void StartFocusProbe() {
    HANDLE expected = nullptr;
    HANDLE created = CreateThread(
        nullptr, 0, PollProc, nullptr, CREATE_SUSPENDED, nullptr);
    if (!created) {
        acclog::Write("Focus",
            "StartFocusProbe: CreateThread failed err=%lu", GetLastError());
        return;
    }
    if (!g_pollThread.compare_exchange_strong(expected, created)) {
        // Lost the race — another caller already started the thread.
        CloseHandle(created);
        return;
    }
    ResumeThread(created);
    acclog::Write("Focus",
        "StartFocusProbe: polling thread launched (handle=%p)", created);
}

void ArmStartupForegroundGuard() {
    g_guardReclaims.store(0, std::memory_order_relaxed);
    DWORD deadline = GetTickCount() + kGuardWindowMs;
    if (deadline == 0) deadline = 1;  // 0 is the disarmed sentinel
    g_guardDeadlineTick.store(deadline, std::memory_order_release);
    acclog::Write("Focus",
        "StartupForegroundGuard: armed for %lu ms (watching for overlay "
        "focus-theft, e.g. Game Bar)", kGuardWindowMs);
}

}  // namespace acc::diag::focus
