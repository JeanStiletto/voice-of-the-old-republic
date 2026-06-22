// Shared minigame aim-assist facility — see minigame_aim.h for the design and
// the engine grounding (CSWMiniPlayer.offset +0x1c4 is the per-tick-integrated
// aim/lane field; writing it after Control runs steers the controlled object).

#include "minigame_aim.h"

#include <windows.h>

namespace acc::minigame {

bool ReadOffsetVector(void* player, Vector& out) {
    if (!player) return false;
    __try {
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(player) +
            kMiniPlayerOffsetVectorOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void WriteOffsetVector(void* player, const Vector& v) {
    if (!player) return;
    __try {
        *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(player) +
            kMiniPlayerOffsetVectorOffset) = v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

float MagnetGain(float t, const MagnetParams& p) {
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    // t² keeps the far end gentle (a guide) and the near end strong (sticky).
    return p.gainFar + t * t * (p.gainNear - p.gainFar);
}

float MagnetStep(float offsetVal, float mappedErr, float gain,
                 const MagnetParams& p) {
    float step = -mappedErr * gain;
    if (step >  p.maxStep) step =  p.maxStep;
    else if (step < -p.maxStep) step = -p.maxStep;
    return offsetVal + step;
}

}  // namespace acc::minigame
