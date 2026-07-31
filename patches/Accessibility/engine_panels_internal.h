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

// The first links of the CGuiInGame chain come from engine_app.h, which owns
// the whole AppManager walk (Phase-3 B1). This header used to carry its own
// copies under names one letter different from the canonical ones
// ("...ClientOff" vs "...ClientAppOffset"), which is how the same address
// ended up declared in three places — a Phase-1 split that moved constants
// verbatim rather than consolidating them.
#include "engine_app.h"

namespace acc::engine {

// Last link of the chain, specific to the panels module: CClientExoApp
// internal -> CGuiInGame. Verified against the struct definitions in
// docs/llm-docs/re/swkotor.exe.h.
//
// Identical on KOTOR 2, and directly witnessed there (2026-08-01): the
// forwarder at 0x0078C330 does `MOV ECX,[this+0x40]` and calls the CGuiInGame
// panel creator, so this offset is what produces the very object the K2 slot
// table was read out of. See engine_app.h's note on the chain above it.
inline constexpr size_t    kClientExoAppGuiInGameOff = acc::off::Same(0x40);

// CGuiInGame -> CSWGuiMainInterface. Canonical home for what engine_radial,
// engine_actionbar, engine_picker and combat_diag each declared privately.
// KOTOR 2 value witnessed in SetSWGuiStatus @0x007C9C40, which adds/removes
// the [this+0x98] panel exactly where KOTOR 1's uses main_interface (+0x90).
inline const size_t        kGuiInGameMainInterfaceOff = acc::off::Pick(0x90, 0x98);

// Walk that chain and return the CGuiInGame*. Defined in
// engine_panels.cpp next to the identity registry; the state readers in
// engine_panels_state.cpp are its only callers outside that file.
void* ResolveGuiInGame();

}  // namespace acc::engine
