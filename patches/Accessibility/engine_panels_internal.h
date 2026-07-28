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

namespace acc::engine {

// CGuiInGame resolution chain. Address values verified against Lane's
// SARIF (CAppManager_vtable @ 0x007A39FC). Field offsets from the struct
// definitions in docs/llm-docs/re/swkotor.exe.h.
inline constexpr uintptr_t kAddrAppManagerPtr        = 0x007A39FC;
inline constexpr size_t    kAppManagerClientOff      = 0x04;
inline constexpr size_t    kClientExoAppInternalOff  = 0x04;
inline constexpr size_t    kClientExoAppGuiInGameOff = 0x40;

// Walk that chain and return the CGuiInGame*. Defined in
// engine_panels.cpp next to the identity registry; the state readers in
// engine_panels_state.cpp are its only callers outside that file.
void* ResolveGuiInGame();

}  // namespace acc::engine
