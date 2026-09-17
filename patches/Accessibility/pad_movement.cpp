#include "pad_movement.h"

#include "camera_orient.h"    // IsActive — the snap-turn owns the turn key
#include "engine_keymap.h"    // MoveAxisScancode / TurnScancode
#include "hotkeys.h"          // IsForegroundGame
#include "key_inject.h"
#include "log.h"
#include "minigame_swoop_race.h"  // IsActive — the race steers, it does not walk
#include "pad_input.h"
#include "pad_quickmenu.h"    // IsOpen — the quick menu stands the stick down

namespace acc::pad::movement {

namespace {

// The directions the two sticks drive, in the order the arrays below use. The
// last pair is the swoop race's steer, which is its own bind (see Tick).
enum Dir {
    kFwd = 0, kBack, kStrafeL, kStrafeR, kTurnL, kTurnR, kSteerL, kSteerR,
    kDirCount
};

const char* const kDirName[kDirCount] = {
    "forward", "back", "strafe left", "strafe right",
    "camera left", "camera right", "race steer left", "race steer right",
};

// Which scancode each direction presses. Resolved per press rather than
// cached: swkotor.ini is re-read when the player changes their binds, and a
// cached scancode would keep driving the old key. Also, a function-local
// static is unavailable in much of this codebase for SEH reasons and caching
// here would buy nothing measurable.
int ScancodeFor(int dir) {
    using MA = acc::engine_keymap::MoveAxis;
    switch (dir) {
        case kFwd:     return acc::engine_keymap::MoveAxisScancode(MA::Forward);
        case kBack:    return acc::engine_keymap::MoveAxisScancode(MA::Backward);
        // NAMING TRAP: MoveAxis::TurnLeft / TurnRight carry the STRAFE binds
        // (Action281) in MoveAxisPrimaryVk / MoveAxisScancode — the union
        // buckets track turning, the primaries track walking, and the enum is
        // named for the first of those. See engine_keymap.h.
        case kStrafeL: return acc::engine_keymap::MoveAxisScancode(MA::TurnLeft);
        case kStrafeR: return acc::engine_keymap::MoveAxisScancode(MA::TurnRight);
        case kTurnL:   return acc::engine_keymap::TurnScancode(/*left=*/true);
        case kTurnR:   return acc::engine_keymap::TurnScancode(/*left=*/false);
        case kSteerL:  return acc::engine_keymap::MinigameSteerScancode(/*left=*/true);
        case kSteerR:  return acc::engine_keymap::MinigameSteerScancode(/*left=*/false);
        default:       return 0;
    }
}

// Scancode currently held per direction, 0 = not held. The scancode is stored
// rather than a bool so a rebind mid-hold still releases the key we actually
// pressed.
int g_held[kDirCount] = {0, 0, 0, 0, 0, 0, 0, 0};

// Press above, release below. The gap is what stops a stick resting near a
// diagonal boundary from chattering a key on and off every frame. Both are
// against the NORMALISED component, so a straight push reads 1.0 on one axis
// and 0.0 on the other, and a clean diagonal reads 0.707 on both — which is
// the eight-way behaviour a keyboard player already has.
constexpr float kPressAt   = 0.45f;
constexpr float kReleaseAt = 0.30f;

void SetHeld(int dir, bool want) {
    if (want) {
        if (g_held[dir] != 0) return;
        const int scan = ScancodeFor(dir);
        if (scan == 0) return;
        g_held[dir] = scan;
        acc::key_inject::Send(scan, /*down=*/true);
        acclog::Write("Pad.move", "press %s (scan 0x%02x)", kDirName[dir], scan);
    } else {
        if (g_held[dir] == 0) return;
        acc::key_inject::Send(g_held[dir], /*down=*/false);
        acclog::Write("Pad.move", "release %s (scan 0x%02x)", kDirName[dir],
                      g_held[dir]);
        g_held[dir] = 0;
    }
}

// One axis component -> a pair of opposed directions, with hysteresis.
void DriveAxis(float v, int negDir, int posDir) {
    if (v >= kPressAt)        SetHeld(posDir, true);
    else if (v <= kReleaseAt) SetHeld(posDir, false);
    if (v <= -kPressAt)        SetHeld(negDir, true);
    else if (v >= -kReleaseAt) SetHeld(negDir, false);
}

}  // namespace

void ReleaseAll() {
    for (int d = 0; d < kDirCount; ++d) SetHeld(d, false);
}

void Tick() {
    // KOTOR 2 walks itself — unless its engine never saw the pad, in which case
    // nothing does and this is the only thing that will. EnginePadDead() is
    // always true on KOTOR 1, so this is the old gate plus one case.
    if (!acc::pad::EnginePadDead()) return;

    // pad::InWorld() is the shared predicate — the mod's own overlays do NOT
    // make it false, because the live action menu deliberately runs over a
    // running world and taking movement away there would be exactly the pause
    // it exists to avoid.
    //
    // The quick menu is the one overlay that DOES stand the stick down. Its
    // entries are one-shot game actions chosen in a moment, and walking off
    // into a firefight while browsing them would be nobody's intent.
    if (!acc::pad::Connected() || !acc::hotkeys::IsForegroundGame()) {
        ReleaseAll();
        return;
    }

    // A swoop race steers on its own bind, MGActionLeft/Right, and nothing
    // else: the walk and camera binds are not A/D on a vanilla ini, so they
    // would not steer, and forward would press W — the race's shift key
    // (swoop_race Tick polls it), turning a push up the stick into a shift.
    // Checked ahead of the world gate, which is not the question in a race.
    if (acc::swoop_race::IsActive()) {
        for (int d = kFwd; d <= kTurnR; ++d) SetHeld(d, false);
        float sx = 0.0f, sy = 0.0f;
        if (acc::pad::StickVector(sx, sy)) {
            DriveAxis(sx, kSteerL, kSteerR);
        } else {
            SetHeld(kSteerL, false);
            SetHeld(kSteerR, false);
        }
        return;
    }
    SetHeld(kSteerL, false);
    SetHeld(kSteerR, false);

    if (!acc::pad::InWorld() || acc::pad::quickmenu::IsOpen()) {
        ReleaseAll();
        return;
    }

    float x = 0.0f, y = 0.0f;
    if (acc::pad::StickVector(x, y)) {
        DriveAxis(y, kBack,    kFwd);      // +y = up = forward
        DriveAxis(x, kStrafeL, kStrafeR);
    } else {
        SetHeld(kFwd, false);
        SetHeld(kBack, false);
        SetHeld(kStrafeL, false);
        SetHeld(kStrafeR, false);
    }

    // The camera turn stands down entirely while camera_orient is running its
    // snap-turn: that feature holds the same key and releases it predictively
    // against a yaw target, so a second writer would overshoot it or strand
    // its key. The stick gets the camera back the moment the snap finishes.
    float rx = 0.0f, ry = 0.0f;
    if (!acc::camera_orient::IsActive() &&
        acc::pad::RightStickVector(rx, ry)) {
        DriveAxis(rx, kTurnL, kTurnR);
    } else {
        SetHeld(kTurnL, false);
        SetHeld(kTurnR, false);
    }
}

}  // namespace acc::pad::movement
