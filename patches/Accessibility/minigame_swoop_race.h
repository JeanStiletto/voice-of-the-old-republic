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
//   +0x48 obstacle_count, +0x80 type (1=swoop, 2=turret — engine-
//   confirmed from CSWMiniGame::Load, which only ever sets type to 1
//   or 2; the turret/gunner game is handled by turret_game.cpp),
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

// Shift up a gear. KOTOR 2: run the player bike's OnAccelerate track script
// exactly as the engine's client input handler does on its own events.
// KOTOR 1: press the player's MGshoot key (Space by default), the engine's own
// shift. Return false when there is no active swoop race to act on.
//
// Callers: Tick polls the SwoopShift hotkey (W) on both games; the input
// pipeline calls it for KOTOR 2's pad-B event; the pad layer calls it for B
// when the mod drives the controller (KOTOR 1, or a KOTOR 2 engine that never
// saw the pad).
bool TryShift();

// KOTOR 2 only (KOTOR 1 has no jump): run the OnBrake track script, the jump.
// False when there is no active K2 swoop race. The input pipeline calls it for
// Space / pad A (the K2 engine's own jump events, run here so they are logged).
bool TryJump();

}  // namespace acc::swoop_race
