#include "probe_mouselook.h"

#include <windows.h>

#pragma comment(lib, "user32.lib")

#include "engine_options.h"
#include "engine_player.h"
#include "hotkeys.h"
#include "log.h"
#include "strings.h"
#include "prism.h"

namespace acc::probe_mouselook {

namespace {

// Synthetic-sweep state machine. Stays inert until a toggle-to-ON kicks
// it off; ticks across multiple OnUpdate calls so the engine sees a
// stream of small mouse-motion deltas instead of a single jump.
//
// Park-at-apex shape (0.3s ramp out → 1.5s hold → 0.3s ramp back). The
// continuous-triangle variant (1.5s back-and-forth) was at the threshold
// of audibility — the user reported pan on one of three sweeps. Holding
// the listener at a constant off-axis position for 1.5s gives the ear
// a stable signal to lock onto.
//
// Cursor restoration:
// User-observed 2026-05-06: with Mouse Look ON, the OS cursor still
// moves with each SendInput delta — the engine does NOT capture the
// cursor to the window. Over a +1000px sweep the cursor escapes the
// game window and can land on a different monitor. We capture the
// pre-sweep cursor position via GetCursorPos and restore it on END.
// Implication for view mode: continuous mouse-driven rotation will
// need explicit per-emit recentering (SetCursorPos to window centre)
// to keep the cursor anchored. Documented in lay-off plan.
struct SweepState {
    bool   active     = false;
    DWORD  started_at = 0;
    int    emitted_dx = 0;
    int    emit_count = 0;
    POINT  cursor_at_start{0, 0};
    bool   cursor_captured = false;
};

SweepState g_sweep;

constexpr DWORD kRampMs    = 300;   // ramp out / ramp back duration
constexpr DWORD kHoldMs    = 1500;  // hold-at-apex duration
constexpr int   kApexDxPx  = 1000;  // total dx accumulated at apex
constexpr DWORD kSweepEndMs = 2 * kRampMs + kHoldMs;  // 2100ms total

// Cumulative dx target curve for elapsed-ms `t`:
//   [0, kRampMs)              → ramp up:   0 → kApexDxPx (linear)
//   [kRampMs, kRampMs+kHoldMs)→ hold:      kApexDxPx (no emits — chunk=0)
//   [kRampMs+kHoldMs, end)    → ramp down: kApexDxPx → 0 (linear)
int TargetCumulativeDx(DWORD t) {
    if (t >= kSweepEndMs) return 0;
    if (t < kRampMs) {
        return static_cast<int>(
            (static_cast<int64_t>(t) * kApexDxPx) / kRampMs);
    }
    if (t < kRampMs + kHoldMs) {
        return kApexDxPx;
    }
    DWORD t2 = t - (kRampMs + kHoldMs);
    int dropped = static_cast<int>(
        (static_cast<int64_t>(t2) * kApexDxPx) / kRampMs);
    return kApexDxPx - dropped;
}

void EmitMouseDelta(int dx) {
    if (dx == 0) return;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = 0;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;  // relative motion
    UINT sent = SendInput(1, &input, sizeof(INPUT));
    if (sent != 1) {
        acclog::Write("ProbeMouseLook", "SendInput returned %u (expected 1) for dx=%d "
            "lastErr=%lu",
            sent, dx, static_cast<unsigned long>(GetLastError()));
    }
}

void StartSweep() {
    g_sweep.active     = true;
    g_sweep.started_at = GetTickCount();
    g_sweep.emitted_dx = 0;
    g_sweep.emit_count = 0;
    g_sweep.cursor_captured = (GetCursorPos(&g_sweep.cursor_at_start) != 0);
    acclog::Write("ProbeMouseLook", "sweep START t=%lu shape=park ramp_ms=%lu "
        "hold_ms=%lu apex_px=%d total_ms=%lu cursor_at_start=(%ld,%ld) "
        "captured=%d",
        static_cast<unsigned long>(g_sweep.started_at),
        static_cast<unsigned long>(kRampMs),
        static_cast<unsigned long>(kHoldMs),
        kApexDxPx,
        static_cast<unsigned long>(kSweepEndMs),
        g_sweep.cursor_at_start.x, g_sweep.cursor_at_start.y,
        g_sweep.cursor_captured ? 1 : 0);
}

}  // namespace

void PollWin32() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::ProbeMouseLookToggle)) return;

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        acclog::Write("ProbeMouseLook", "Shift+AltGr fired without player loaded; "
            "skipping toggle");
        return;
    }

    bool before = false;
    if (!acc::engine::GetMouseLook(before)) {
        acclog::Write("ProbeMouseLook", "Shift+AltGr fired but GetMouseLook failed "
            "(chain null or SEH); skipping toggle");
        return;
    }

    bool after = false;
    if (!acc::engine::ToggleMouseLook(after)) {
        acclog::Write("ProbeMouseLook", "Shift+AltGr — pre-state=%d but ToggleMouseLook "
            "failed (chain null or SEH on write)", before ? 1 : 0);
        return;
    }

    bool readback = false;
    bool readbackOk = acc::engine::GetMouseLook(readback);

    acclog::Write("ProbeMouseLook", "Shift+AltGr toggle: before=%d after=%d readback=%d "
        "(readbackOk=%d)",
        before ? 1 : 0, after ? 1 : 0, readback ? 1 : 0,
        readbackOk ? 1 : 0);

    prism::Speak(
        acc::strings::Get(after
            ? acc::strings::Id::MouseLookOn
            : acc::strings::Id::MouseLookOff),
        /*interrupt=*/true);

    if (after && !g_sweep.active) {
        StartSweep();
    }
}

void TickSweep() {
    if (!g_sweep.active) return;

    DWORD now = GetTickCount();
    DWORD elapsed = now - g_sweep.started_at;

    if (elapsed >= kSweepEndMs) {
        // Push back any rounding residual so the camera lands exactly
        // where it started (in mouse-delta accounting; engine's per-
        // frame quantisation may differ).
        int residual = -g_sweep.emitted_dx;
        if (residual != 0) {
            EmitMouseDelta(residual);
            g_sweep.emit_count++;
        }
        // Restore the OS cursor — sweep moved it across the desktop
        // (Mouse Look ON doesn't capture the cursor in KOTOR).
        bool restored = false;
        if (g_sweep.cursor_captured) {
            restored = (SetCursorPos(g_sweep.cursor_at_start.x,
                                     g_sweep.cursor_at_start.y) != 0);
        }
        acclog::Write("ProbeMouseLook", "sweep END elapsed_ms=%lu emits=%d net_dx=%d "
            "(residual=%d) cursor_restored=%d",
            static_cast<unsigned long>(elapsed), g_sweep.emit_count,
            g_sweep.emitted_dx + residual, residual,
            restored ? 1 : 0);
        g_sweep = SweepState{};
        return;
    }

    int target = TargetCumulativeDx(elapsed);
    int chunk = target - g_sweep.emitted_dx;
    if (chunk != 0) {
        EmitMouseDelta(chunk);
        g_sweep.emitted_dx += chunk;
        g_sweep.emit_count++;
        acclog::Write("ProbeMouseLook", "sweep emit t_ms=%lu chunk=%d cum_dx=%d "
            "emits=%d",
            static_cast<unsigned long>(elapsed), chunk, g_sweep.emitted_dx,
            g_sweep.emit_count);
    }
}

}  // namespace acc::probe_mouselook
