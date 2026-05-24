// Swoop race minigame accessibility.
//
// Layer: gameplay/ (runs from core_tick::Dispatch). Polls
// CSWCArea.mini_game (+0x264) once per tick. On a null→non-null transition
// the player just entered a swoop / turret minigame; we speak the entry
// opener + the keybind cheat sheet via Prism, and on the inverse
// transition speak the exit cue. Continuous proximity cues for obstacles
// and accelerator pads are layered on top once we lock the obstacle
// array's offset inside CSWMiniGame (a first-fire diagnostic dumps the
// candidate bytes — see swoop_race.cpp::EmitDiagnosticDump).
//
// Why poll CSWCArea instead of detecting via area-tag prefix matching:
// the engine sets the mini_game pointer when the minigame controller is
// constructed (at module-load time for *_mg.rim modules), so the
// transition is one-shot and unambiguous. Tag-based detection
// ("tar_m03mg", "manm26mg", "tat_m17mg") would miss user-added modules
// and would fire on dialog-only swoop screens like tar_m03af that look
// like the platform but aren't the actual race.
//
// Engine surfaces (from docs/llm-docs/re/swkotor.exe.h):
//
//   CSWCArea
//     +0x264   CSWMiniGame *mini_game        // null when no minigame active
//
//   CSWMiniGame
//     +0x24    CSWMiniPlayer *player         // bike + tunnel state
//     +0x28    CSWCArea      *area           // back-pointer (same as ours)
//     +0x30    enemy_count
//     +0x48    obstacle_count
//     +0x84    type                          // 0=swoop, 1=turret (suspected)
//     +0xbc    lateral_acceleration
//
//   CSWMiniPlayer  (extends CSWTrackFollower extends CSWMiniGameObject)
//     +0x1c4   Vector offset                 // current lateral / vertical
//                                            //   (offset in tunnel frame)
//     +0x1d8   float  min_speed
//     +0x1dc   float  max_speed
//
// All engine reads are SEH-guarded so a torn pointer during area
// transition can't fastfail the process.

#pragma once

namespace acc::swoop_race {

// Per-tick entry. No-op when not in a minigame. Cheap when idle (one
// chain read + one compare).
void Tick();

// True iff CSWCArea.mini_game is currently non-null. Other subsystems
// (e.g. menus/cycle/passive_narrate) can use this to suppress noise
// during the race — none currently do, but the predicate is cheap and
// the gate keeps the surface area shared.
bool IsActive();

}  // namespace acc::swoop_race
