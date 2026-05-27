// Swoop race minigame accessibility.
//
// Polls CSWCArea.mini_game (+0x264) once per tick. Null→non-null
// transition speaks the entry opener + keybind cheat sheet; inverse
// speaks the exit cue. Continuous obstacle / accelerator-pad cues layer
// on top once the obstacle array's offset inside CSWMiniGame is locked
// (a first-fire diagnostic dump captures candidate bytes).
//
// Detection via the engine pointer (not area-tag prefix): the engine
// sets mini_game when the minigame controller constructs at module-load,
// so the transition is one-shot and unambiguous. Tag matching would
// miss user-added modules and fire on dialog-only swoop screens that
// look like the platform but aren't the actual race.
//
// CSWMiniGame: +0x24 player, +0x28 area, +0x30 enemy_count,
//   +0x48 obstacle_count, +0x84 type (0=swoop, 1=turret suspected),
//   +0xbc lateral_acceleration.
// CSWMiniPlayer (extends CSWTrackFollower → CSWMiniGameObject):
//   +0x1c4 Vector offset (lateral/vertical in tunnel frame),
//   +0x1d8/+0x1dc min/max_speed.
//
// All reads SEH-guarded.

#pragma once

namespace acc::swoop_race {

// No-op when not in a minigame; cheap when idle.
void Tick();

bool IsActive();

}  // namespace acc::swoop_race
