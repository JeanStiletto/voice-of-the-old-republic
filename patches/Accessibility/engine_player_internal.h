// internal seam between the three TUs the player module is split across.
//
// This is NOT public API. engine_player.h stays the public surface; this
// header exists only because the Phase-1 structure pass (refactoring
// candidate 5) split engine_player.cpp into:
//
//   engine_player.cpp            core position / facing / camera reads
//   engine_player_party.cpp      party + leader resolution
//   engine_player_inputlock.cpp  input-disable + auto-restore state machine
//
// GetPlayerServerObject was file-static while all three lived in one TU and
// is used by all three, so it is the one thing that had to be published.
// The typedefs travel with it because they describe the same chain walk.

#pragma once

namespace acc::engine {

typedef void* (__thiscall* PFN_GetPlayerCreature)(void* this_);
typedef void* (__thiscall* PFN_CSWSObjectGetArea)(void* this_);
typedef void* (__thiscall* PFN_GetPlayerCharacterName)(void* this_);

// Walk *kAddrAppManagerPtr -> CClientExoApp -> GetPlayerCreature() ->
// server_object (+0xf8) and return the CSWSObject*. nullptr at any failure
// point along the chain or on any raised exception.
//
// Centralising the chain walk means the per-field readers each pay one SEH
// frame instead of three; the cost is one extra call per read, negligible
// at the rates the per-tick consumers invoke these.
//
// Defined in engine_player.cpp.
void* GetPlayerServerObject();

}  // namespace acc::engine
