// Turret (space-combat gunner) minigame accessibility.
//
// The turret minigame shares the CSWMiniGame struct (and vtable) with
// swoop racing — both are polled via CSWCArea.mini_game (+0x264). They
// are distinguished by CSWMiniGame.type (+0x80): swoop is type 1, the
// turret/gunner game is type 2 (engine-confirmed — CSWMiniGame::Load only
// ever sets type to 1 or 2). This module owns the type==2 case;
// swoop_race.cpp gates itself to type==1 so the two never both fire on the
// same minigame. (We previously read +0x84 = axis_x and matched 0/3 by
// coincidence.)
//
// Behaviour: entry/exit announce (the native control hint — WASD aims,
// Space fires) plus a 3D crosshair cue on the Q/E-selected fighter. The aim
// is CSWMiniPlayer.offset (+0x1c4): offset.x = elevation (W/S), offset.z =
// azimuth (A/D), in degrees — engine-confirmed (Control integrates it and
// pushes it into the rotating gun/camera models). The crosshair is the gun's
// bullethook0 node world direction (the literal bolt fire line); aim-assist
// (magnetism by default, full lock-on under the Autoaiming toggle) steers by
// WRITING offset, which the engine re-integrates so the write sticks. See
// turret_game.cpp.
//
// Detection latch mirrors swoop_race.cpp: the area chain stops resolving
// the minigame mid-game during the entry transition, so the CSWMiniGame
// pointer is latched on first detect and validated per-tick by vtable.
//
// All reads SEH-guarded.

#pragma once

namespace acc::turret_game {

// No-op when not in the turret minigame; cheap when idle.
void Tick();

bool IsActive();

}  // namespace acc::turret_game
