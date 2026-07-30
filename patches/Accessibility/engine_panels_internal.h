// internal seam between engine_panels.cpp and engine_panels_state.cpp.
//
// This is NOT public API. engine_panels.h stays the public surface; this
// header exists only because the Phase-1 structure pass (refactoring
// candidate 6) split the panel-identity registry from the
// foreground/UI-blocking + input-class state readers, and both halves walk
// the same CGuiInGame resolution chain.
//
// Values are unchanged RE facts, moved verbatim from engine_panels.cpp.

#pragma once

#include <cstddef>
#include <cstdint>

// The first links of the CGuiInGame chain — kAddrAppManagerPtr,
// kAppManagerClientAppOffset and kClientExoAppInternalOffset — come from
// here. They are the same walk the player module already does.
//
// This header used to carry its own copies of all three, under names one
// letter different from the canonical ones ("...ClientOff" vs
// "...ClientAppOffset"), which is how the same address ended up declared
// in three places. The copies were created by the Phase-1 panels split,
// which moved them verbatim rather than consolidating — correct for a
// behaviour-preserving move, wrong to leave behind.
#include "engine_player.h"

namespace acc::engine {

// Last link of the chain, specific to the panels module: CClientExoApp
// internal -> CGuiInGame. Verified against the struct definitions in
// docs/llm-docs/re/swkotor.exe.h.
inline constexpr size_t    kClientExoAppGuiInGameOff = 0x40;

// CGuiInGame -> CSWGuiMainInterface. Canonical home for what engine_radial,
// engine_actionbar, engine_picker and combat_diag each declared privately.
inline constexpr size_t    kGuiInGameMainInterfaceOff = 0x90;

// Walk that chain and return the CGuiInGame*. Defined in
// engine_panels.cpp next to the identity registry; the state readers in
// engine_panels_state.cpp are its only callers outside that file.
void* ResolveGuiInGame();

}  // namespace acc::engine
