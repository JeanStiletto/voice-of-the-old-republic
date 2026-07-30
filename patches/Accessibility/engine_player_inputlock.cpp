// player input-disable state machine and its auto-restore watchdog.
//
// Split out of engine_player.cpp by the Phase-1 structure pass
// (refactoring candidate 5). SetPlayerInputEnabled hands control of the
// player to a scripted/queued action; the watchdog here restores it when
// the action queue drains, stalls, or becomes unreadable - so a failed
// dispatch can never leave the player permanently unable to move.
//
// The functions were interleaved with the party readers in the original
// file, so this is a two-piece cut rather than one slice - the definitions
// themselves are verbatim, including the session statics and the four
// timing constants the watchdog is tuned around.
//
// Declarations stay in engine_player.h; GetPlayerServerObject comes from
// engine_player_internal.h.

#include "engine_app.h"     // GetClientApp
#include "engine_player.h"
#include "engine_player_internal.h"

#include <windows.h>
#include <cstdint>

#include "log.h"

namespace acc::engine {

namespace {

// Auto-restore session. Set when SetPlayerInputEnabled(false) succeeds.
// TickPlayerInputRestore flips control back to the player when the engine's
// AI action queue (CSWSObject.action_nodes) shows the handed-off action is
// done. Three ways that resolves:
//   (1) queue drains to 0 after we saw it populate  → arrived / used / talked;
//   (2) queue still empty after a short grace       → nothing ever enqueued
//                                                      (a no-op dispatch);
//   (3) queue non-empty but the PC hasn't MOVED for kStallMs → stuck path.
// There is deliberately NO time cap while the PC is making progress, so a
// long but healthy walk (100m+ extended cycling) is never cut off — it ends
// naturally on (1) however long it takes. A time ceiling applies only when
// the queue can't be read at all (no live creature).
//
// `g_sawPending` latches once we observe a non-empty queue, so a 0 read in
// the first frame(s) after dispatch (before the engine enqueues) doesn't
// trip an early restore. A fresh disable arms the session; a *repeat* disable
// while one is already active does NOT re-arm (no window extension) — that
// was the janicebug livelock under repeated Enter presses.
constexpr DWORD kQueueGraceMs          = 300;    // post-dispatch enqueue race
constexpr DWORD kStallMs               = 4000;   // no-movement = stuck queue
constexpr DWORD kUnreadableCeilingMs   = 8000;   // queue-unreadable fallback
constexpr float kProgressEpsSq         = 0.25f;  // (0.5m)^2 — moved threshold
bool   g_disableActive    = false;
DWORD  g_disableExpiresAt = 0;
DWORD  g_disableArmedAt   = 0;
bool   g_sawPending       = false;
bool   g_haveProgress     = false;
Vector g_progressPos      = {0.0f, 0.0f, 0.0f};
DWORD  g_progressAt       = 0;
int    g_lastLoggedQueueDepth = -2;  // ActionQueue.Diag delta tracker

typedef void (__thiscall* PFN_CSWPlayerControlSetEnabled)(void* this_, int enabled);

// CClientExoAppInternal → CSWPlayerControl. Distinct from
// GetPlayerServerObject's chain — different destination. Returns nullptr on
// any failure.
void* GetPlayerControl() {
    void* internal = GetClientAppInternal();
    if (!internal) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) +
            kClientAppPlayerControlOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

// Player's pending AI-action count (CSWSObject.action_nodes @+0xfc →
// internal->count @+0x8). Returns -1 if the queue can't be read this tick
// (no live creature / fault); 0 means drained. See engine_offsets.h. Public
// so the interact feature's dialog-approach watchdog can tell "walk-to-talk
// still pending" (depth>0) from "nothing enqueued" (depth 0).
int GetPlayerActionQueueDepth() {
    void* obj = GetPlayerServerObject();
    if (!obj) return -1;
    __try {
        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(obj) + kObjectActionNodesOffset);
        if (!internal) return 0;  // unallocated list ⇒ no pending actions
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(internal) +
            kExoLinkedListInternalCountOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool SetPlayerInputEnabled(bool enabled, bool armAutoRestore) {
    void* playerControl = GetPlayerControl();
    if (!playerControl) return false;

    __try {
        auto fn = reinterpret_cast<PFN_CSWPlayerControlSetEnabled>(
            kAddrCSWPlayerControlSetEnabled);
        fn(playerControl, enabled ? 1 : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    bool wasActive = g_disableActive;
    if (enabled) {
        // Re-enable always clears the session regardless of the flag.
        g_disableActive = false;
        g_disableExpiresAt = 0;
        g_sawPending = false;
        g_haveProgress = false;
    } else if (armAutoRestore) {
        // Arm a queue-watched session. A *fresh* disable starts the latch,
        // progress tracker and unreadable-ceiling; a repeat disable while a
        // session is already active is a no-op for our bookkeeping — the
        // engine action is re-issued but the restore window must NOT extend
        // (the janicebug livelock was repeated presses pushing the deadline).
        if (!g_disableActive) {
            g_disableActive = true;
            g_disableArmedAt = GetTickCount();
            g_disableExpiresAt = g_disableArmedAt + kUnreadableCeilingMs;
            g_sawPending = false;
            g_haveProgress = false;
        }
    } else {
        // Sustained disable — caller manages the lifecycle (view mode).
        g_disableActive = false;
        g_disableExpiresAt = 0;
    }
    acclog::Write("PlayerInput", "SetEnabled(%s, armAutoRestore=%d) — was disabled=%d, "
        "now disabled=%d, expires=%lu",
        enabled ? "true" : "false", armAutoRestore ? 1 : 0,
        wasActive ? 1 : 0, g_disableActive ? 1 : 0,
        static_cast<unsigned long>(g_disableExpiresAt));
    return true;
}


void TickPlayerInputRestore() {
    if (!g_disableActive) return;
    DWORD now = GetTickCount();
    int depth = GetPlayerActionQueueDepth();

    bool restore = false;
    const char* reason = nullptr;
    if (depth > 0) {
        // Engine action genuinely in flight (walk-to-target / use). Latch
        // that we saw the queue populate, then keep manual input frozen as
        // long as the PC is making progress — NO time cap, so a long but
        // healthy walk completes naturally on queue-drain. Only bail if the
        // queue is non-empty AND the PC has not moved for kStallMs (a path
        // the engine can't finish, e.g. blocked destination).
        g_sawPending = true;
        Vector pos;
        if (GetPlayerPosition(pos)) {
            float dx = pos.x - g_progressPos.x;
            float dy = pos.y - g_progressPos.y;
            if (!g_haveProgress || (dx * dx + dy * dy) >= kProgressEpsSq) {
                g_progressPos = pos;
                g_progressAt = now;
                g_haveProgress = true;
            } else if (now - g_progressAt >= kStallMs) {
                restore = true;
                reason = "stalled (queue not draining, no movement)";
            }
        } else if (now >= g_disableExpiresAt) {
            // Can't read position to judge progress — fall back to the
            // unreadable ceiling so we never freeze indefinitely.
            restore = true;
            reason = "ceiling (position unreadable)";
        }
    } else if (depth == 0) {
        if (g_sawPending) {
            restore = true;
            reason = "queue drained";
        } else if (now - g_disableArmedAt >= kQueueGraceMs) {
            // Grace elapsed and nothing ever enqueued — the dispatch was a
            // no-op (e.g. out-of-range talk that didn't walk) or completed
            // within one tick. Don't sit frozen waiting on a phantom action.
            restore = true;
            reason = "no action enqueued (grace)";
        }
    } else if (now >= g_disableExpiresAt) {
        // depth < 0 ⇒ queue unreadable (no live creature). Time fallback.
        restore = true;
        reason = "ceiling (queue unreadable)";
    }
    if (!restore) return;

    acclog::Write("PlayerInput", "TickPlayerInputRestore — %s (now=%lu "
        "armed=%lu queueDepth=%d sawPending=%d progressAt=%lu)",
        reason, static_cast<unsigned long>(now),
        static_cast<unsigned long>(g_disableArmedAt),
        depth, g_sawPending ? 1 : 0,
        static_cast<unsigned long>(g_progressAt));
    SetPlayerInputEnabled(true);  // flips g_disableActive false on success
}

void TickActionQueueDiag() {
    // Low-volume diagnostic: log only when the player's pending-action
    // count changes, so we can watch how the queue behaves (e.g. confirm
    // ordinary combat runs off CSWSCombatRound and never populates this
    // queue, and that autowalk/use drain it on arrival). Gated on
    // GetPlayerPosition — the documented safe window for per-tick engine
    // probes against the PC slot (see chargen→world transient note).
    Vector pos;
    if (!GetPlayerPosition(pos)) return;
    int depth = GetPlayerActionQueueDepth();
    if (depth == g_lastLoggedQueueDepth) return;
    acclog::Write("ActionQueue.Diag", "DELTA depth %d -> %d (freezeActive=%d)",
        g_lastLoggedQueueDepth, depth, g_disableActive ? 1 : 0);
    g_lastLoggedQueueDepth = depth;
}

}  // namespace acc::engine
