#include "stealth_watch.h"

#include <windows.h>  // GetTickCount64, SEH
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "engine_area.h"      // GetObjectPosition
#include "engine_player.h"    // GetPlayerServerCreature / GetPlayerPosition
#include "examine_view.h"     // IsHostileCreature
#include "log.h"
#include "narrated_target.h"
#include "prism.h"

namespace acc::stealth_watch {

namespace {

// CSWSCreature.stealth_mode — byte at +0x4d1, directly after detect_mode at
// +0x4d0. Both are Lane-named fields in the RE database (the NWScript
// ACTION_MODE_STEALTH / ACTION_MODE_DETECT toggles), confirmed against
// k1_win_gog_swkotor.exe.xml. Non-zero once the leader engages Stealth mode.
constexpr size_t kCreatureStealthModeOffset = 0x4d1;

// Announce once the metre reading has moved this far. 1 m (not 2) because
// stealth movement is slow — closing 2 m takes several seconds, which reads as
// "barely any feedback". Fine granularity suits the slow cadence.
constexpr int kStepMeters = 1;

// Floor between spoken numbers so a fast approach can't stack utterances.
constexpr ULONGLONG kMinSpeakGapMs = 250;

bool      g_prevStealth    = false;  // last-seen leader flag (for the log edge)
uint32_t  g_baselineHandle = 0;      // focus the current baseline belongs to
int       g_lastAnnounced  = -1;     // last spoken metre value (-1 = none yet)
ULONGLONG g_lastSpeakMs    = 0;
uint32_t  g_focusHandle    = 0;      // hostile focus we last logged as acquired
int       g_lastLoggedDist = -1;     // last distance we wrote a diag line for

bool ReadLeaderStealth(void* leader) {
    if (!leader) return false;
    __try {
        return *(reinterpret_cast<unsigned char*>(leader) +
                 kCreatureStealthModeOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ResetBaseline() {
    g_baselineHandle = 0;
    g_lastAnnounced  = -1;
    if (g_focusHandle != 0) {
        acclog::Write("StealthWatch", "focus lost (was handle=0x%08x)",
                      g_focusHandle);
        g_focusHandle    = 0;
        g_lastLoggedDist = -1;
    }
}

// Rounded 2-D (floor-plane) distance in metres between the leader and target,
// matching the examine panel's distance readout. -1 on any unresolved pos.
int Distance2DMeters(void* target) {
    Vector t{};
    if (!acc::engine::GetObjectPosition(target, t)) return -1;
    Vector p{};
    if (!acc::engine::GetPlayerPosition(p)) return -1;
    float dx = t.x - p.x;
    float dy = t.y - p.y;
    float d = std::sqrt(dx * dx + dy * dy);
    return static_cast<int>(d + 0.5f);
}

}  // namespace

void Tick() {
    // GetPlayerServerCreature resolves the controlled character (active leader,
    // Tab-cycled) — the creature whose Stealth toggle is what matters here.
    void* leader  = acc::engine::GetPlayerServerCreature();
    bool  stealth = ReadLeaderStealth(leader);

    if (stealth != g_prevStealth) {
        acclog::Write("StealthWatch", "leader stealth_mode %d -> %d",
                      static_cast<int>(g_prevStealth),
                      static_cast<int>(stealth));
        g_prevStealth = stealth;
        if (!stealth) ResetBaseline();
    }

    if (!stealth) return;

    acc::narrated_target::Slot slot;
    if (!acc::narrated_target::TryGet(slot) || slot.isMapPin) {
        ResetBaseline();
        return;
    }
    if (!acc::examine_view::IsHostileCreature(slot.obj)) {
        ResetBaseline();
        return;
    }

    int dist = Distance2DMeters(slot.obj);
    if (dist < 0) return;

    if (g_focusHandle != slot.handle) {
        acclog::Write("StealthWatch",
                      "hostile focus acquired handle=0x%08x dist=%d",
                      slot.handle, dist);
        g_focusHandle    = slot.handle;
        g_lastLoggedDist = dist;
    }

    // First qualifying tick for this focus — set a silent baseline so the
    // first number we speak is a real movement delta, not a redundant echo of
    // the name narration that just stamped this target.
    if (g_baselineHandle != slot.handle || g_lastAnnounced < 0) {
        g_baselineHandle = slot.handle;
        g_lastAnnounced  = dist;
        return;
    }

    if (std::abs(dist - g_lastAnnounced) < kStepMeters) return;

    ULONGLONG now = GetTickCount64();
    bool throttled = (g_lastSpeakMs != 0 && now - g_lastSpeakMs < kMinSpeakGapMs);

    // Full-fidelity diagnostic: one line per integer-distance change, whether
    // or not we spoke it, so the log shows the true movement cadence and lets
    // us tune the step/throttle from evidence. (Claude-consumed; not throttled.)
    if (dist != g_lastLoggedDist) {
        acclog::Write("StealthWatch", "dist=%d prev-spoken=%d throttled=%d",
                      dist, g_lastAnnounced, throttled ? 1 : 0);
        g_lastLoggedDist = dist;
    }

    if (throttled) return;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", dist);
    prism::SpeakUrgent(buf);
    g_lastAnnounced = dist;
    g_lastSpeakMs   = now;
}

}  // namespace acc::stealth_watch
