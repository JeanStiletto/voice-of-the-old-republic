// Turret (space-combat gunner) minigame accessibility.
//
// The turret minigame shares the CSWMiniGame struct (and vtable) with
// swoop racing — both are polled via CSWCArea.mini_game (+0x264). They
// are distinguished by CSWMiniGame.type (+0x84): swoop is type 0, the
// turret/gunner game is type 3 (live-confirmed in the patch log). This
// module owns the type==3 case; swoop_race.cpp gates itself to type==0
// so the two never both fire on the same minigame.
//
// Behaviour: entry/exit announce (the native control hint — WASD aims,
// Space fires; both confirmed native) plus a per-fighter 3D engine-loop
// "approach cue" so the player can hear incoming fighters and aim before
// they open fire. Aiming is rotational (CSWMiniPlayer.offset +0x1c4: z =
// azimuth/A-D, x = elevation/W-S) and fully native, so we synthesise no
// input. The vanilla fighters carry no engine sound of their own in this
// encounter, so the loop uses the game's own engine sample (mgs_engine_0Nl)
// attached by us. Only the nearest few fighters loop at once (all-in-range
// clumped into one drone), timbre-varied per slot. See turret_game.cpp.
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
