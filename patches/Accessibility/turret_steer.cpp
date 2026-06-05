#include "turret_steer.h"

#include <windows.h>
#include <cmath>

#pragma comment(lib, "user32.lib")

#include "log.h"

namespace acc::turret_steer {

namespace {

// DirectInput scan codes — the engine reads the keyboard via DirectInput,
// which sees scancodes only (plain VK SendInput is invisible to it; see
// camera_orient.cpp). Azimuth = A/D, elevation = W/S — the native turret aim
// keys.
constexpr WORD kDikW = 0x11;
constexpr WORD kDikA = 0x1E;
constexpr WORD kDikS = 0x1F;
constexpr WORD kDikD = 0x20;

// ----- Tunables (feel-based — start here, dial in by ear in-game) -----------
// Assist yields to a SUSTAINED manual swing: once the player holds an axis key
// continuously past this, "I really want to swing around" — back off that axis.
// Quick stutter-taps never reach it, so hectic wiggling still gets corrected.
constexpr ULONGLONG kSwingHoldMs    = 350;
// ...and re-engage this long AFTER the sustained hold is released, so a brief
// stutter-release doesn't make the controller instantly grab and fight.
constexpr ULONGLONG kReleaseGraceMs = 250;
// Re-engage hysteresis: from idle, only start driving once the error exceeds
// the deadband by this margin, so a target sitting right at the cone edge
// doesn't chatter the key on and off every tick. Release happens AT the
// deadband (on target).
constexpr float kHystDeg = 2.0f;
// Self-calibration: the minimum observed aim motion (deg/tick) we trust as a
// real key effect (above sensor/jitter noise), and how many agreeing samples
// lock the key->direction sign for the rest of the session.
constexpr float kCalibMinDeltaDeg = 0.5f;
constexpr int   kCalibLockSamples = 2;

float Wrap180(float d) {
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

void SendKey(WORD scan, bool down) {
    INPUT inp = {};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wScan = scan;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &inp, sizeof(INPUT));
}

// One steered axis (azimuth or elevation). The controller is a per-tick
// bang-bang: hold the key that drives the aim toward the target while the
// error is outside the deadband; release inside it. The key->direction sign
// is learned at runtime so we don't hard-code the minigame's convention.
struct Axis {
    const char* name;       // "az" / "el" — log readability
    WORD  scanPos;          // hypothesized "increase the aim coord" key
    WORD  scanNeg;          // "decrease" key
    int   vkPos;            // matching VK for GetAsyncKeyState (player detect)
    int   vkNeg;
    bool  wraps;            // azimuth wraps at +/-180; elevation does not

    // Synthetic key we currently hold: the scancode, or 0 for none.
    WORD  heldScan = 0;

    // Self-calibration. calib: 0 = unknown (act on the hypothesis, keep
    // learning); +1 = scanPos confirmed to INCREASE the coord; -1 = inverted.
    int   calib        = 0;
    int   calibSign    = 0;     // running hypothesis while locking
    int   calibSamples = 0;
    WORD  lastHeldScan = 0;     // key held LEAVING the previous tick
    float prevCoord    = 0.0f;
    bool  havePrev     = false;

    // Assist manual-yield tracking.
    bool      prevPlayerActive = false;
    ULONGLONG holdStartMs      = 0;     // rising edge of a manual hold
    ULONGLONG lastManualMs     = 0;     // last tick a manual key was down
    bool      sustainedLatch   = false; // a deliberate swing is/was in progress
};

Axis g_axis[2] = {
    {"az", kDikD, kDikA, 'D', 'A', /*wraps=*/true},
    {"el", kDikW, kDikS, 'W', 'S', /*wraps=*/false},
};

void ReleaseAxis(Axis& ax) {
    if (ax.heldScan != 0) {
        SendKey(ax.heldScan, /*down=*/false);
        acclog::Write("TurretSteer", "%s release (key up scan=0x%02x)",
                      ax.name, ax.heldScan);
        ax.heldScan = 0;
    }
}

// Switch the synthetic key held on this axis to `scan` (0 = none).
void SetHeld(Axis& ax, WORD scan) {
    if (scan == ax.heldScan) return;
    if (ax.heldScan != 0) SendKey(ax.heldScan, /*down=*/false);
    if (scan != 0)        SendKey(scan, /*down=*/true);
    acclog::Write("TurretSteer", "%s key 0x%02x -> 0x%02x", ax.name,
                  ax.heldScan, scan);
    ax.heldScan = scan;
}

// Learn (or refine) which key increases the aim coordinate, from the motion
// observed while we held a single key last tick.
void Calibrate(Axis& ax, float cur) {
    if (ax.calib != 0) { ax.prevCoord = cur; ax.havePrev = true; return; }
    if (ax.havePrev &&
        (ax.lastHeldScan == ax.scanPos || ax.lastHeldScan == ax.scanNeg)) {
        float delta = cur - ax.prevCoord;
        if (ax.wraps) delta = Wrap180(delta);
        if (std::fabs(delta) >= kCalibMinDeltaDeg) {
            const int deltaSign = (delta > 0.0f) ? 1 : -1;
            // The sign scanPos produces: direct if we held scanPos, inverted
            // if we held scanNeg.
            const int posSign =
                (ax.lastHeldScan == ax.scanPos) ? deltaSign : -deltaSign;
            if (ax.calibSamples == 0 || posSign == ax.calibSign) {
                ax.calibSign = posSign;
                ++ax.calibSamples;
            } else {
                ax.calibSign = posSign;   // contradiction — restart the count
                ax.calibSamples = 1;
            }
            if (ax.calibSamples >= kCalibLockSamples) {
                ax.calib = ax.calibSign;
                acclog::Write("TurretSteer",
                              "%s calibrated: scanPos(0x%02x) %s the aim",
                              ax.name, ax.scanPos,
                              ax.calib > 0 ? "INCREASES" : "DECREASES");
            }
        }
    }
    ax.prevCoord = cur;
    ax.havePrev  = true;
}

// True while the player is physically driving this axis, EXCLUDING the key we
// are injecting (SendInput sets the OS key state, so our own injected key
// reads as down — we must not mistake it for the player). The opposite key is
// the only one that can conflict, and it's unambiguously the player's.
bool PlayerActive(const Axis& ax) {
    const bool posDown = (GetAsyncKeyState(ax.vkPos) & 0x8000) != 0 &&
                         ax.heldScan != ax.scanPos;
    const bool negDown = (GetAsyncKeyState(ax.vkNeg) & 0x8000) != 0 &&
                         ax.heldScan != ax.scanNeg;
    return posDown || negDown;
}

// Update the assist yield latch for one axis. Returns true if the controller
// should YIELD this axis to the player (release and don't steer).
bool UpdateYield(Axis& ax, ULONGLONG now) {
    const bool active = PlayerActive(ax);
    if (active && !ax.prevPlayerActive) ax.holdStartMs = now;  // rising edge
    if (active) {
        ax.lastManualMs = now;
        if (now - ax.holdStartMs >= kSwingHoldMs) ax.sustainedLatch = true;
    } else if (ax.sustainedLatch && now - ax.lastManualMs >= kReleaseGraceMs) {
        ax.sustainedLatch = false;
    }
    ax.prevPlayerActive = active;
    return ax.sustainedLatch;
}

// Drive one axis toward its target. `deadband` releases inside it (on target).
void DriveAxis(Axis& ax, float cur, float err, float deadband,
               bool fullAuto, ULONGLONG now) {
    Calibrate(ax, cur);

    // Assist mode yields to a sustained manual swing; full-auto owns the keys.
    const bool yield = UpdateYield(ax, now);
    if (!fullAuto && yield) {
        ReleaseAxis(ax);
        ax.lastHeldScan = 0;
        return;
    }

    // err = wrap(cur - tgt). err > 0 => aim is past the target => DECREASE the
    // coord; err < 0 => INCREASE. Map the coord direction to a key via calib
    // (treat unknown calib as the +1 hypothesis and learn from the result).
    const int cal = (ax.calib != 0) ? ax.calib : 1;
    const WORD increaseKey = (cal > 0) ? ax.scanPos : ax.scanNeg;
    const WORD decreaseKey = (cal > 0) ? ax.scanNeg : ax.scanPos;

    int dir = 0;  // desired coord change: -1 decrease, +1 increase, 0 hold
    if (err >  deadband) dir = -1;
    else if (err < -deadband) dir = +1;
    // Re-engage hysteresis: starting from idle, require a margin past the
    // deadband so the cone edge doesn't chatter the key.
    if (ax.heldScan == 0 && dir != 0 && std::fabs(err) < deadband + kHystDeg)
        dir = 0;

    WORD want = (dir > 0) ? increaseKey : (dir < 0) ? decreaseKey : 0;
    SetHeld(ax, want);
    ax.lastHeldScan = want;
}

}  // namespace

void Tick(float curAz, float curEl, float tgtAz, float tgtEl,
          float deadbandDeg, bool engage, bool fullAuto, bool gameForeground) {
    if (!engage || !gameForeground) {
        ReleaseAll();
        // Keep the manual-hold edge state coherent so the next engage doesn't
        // read a stale rising edge.
        for (Axis& ax : g_axis) {
            ax.prevPlayerActive = PlayerActive(ax);
            ax.lastHeldScan = 0;
        }
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const float errAz = Wrap180(curAz - tgtAz);
    const float errEl = curEl - tgtEl;
    DriveAxis(g_axis[0], curAz, errAz, deadbandDeg, fullAuto, now);
    DriveAxis(g_axis[1], curEl, errEl, deadbandDeg, fullAuto, now);
}

void ReleaseAll() {
    for (Axis& ax : g_axis) ReleaseAxis(ax);
}

}  // namespace acc::turret_steer
