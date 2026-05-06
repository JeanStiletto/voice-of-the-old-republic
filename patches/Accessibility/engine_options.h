// CClientOptions read/write helpers — currently scoped to the "Mouse Look"
// toggle (the user-facing swkotor.ini "Mouse Look=N" setting).
//
// Layer: engine/ (pure read/write helpers, SEH-guarded; no engine re-entry,
// no menu-side state). Mirrors engine_player.cpp's chain-walk pattern.
//
// Address chain:
//
//   *kAddrAppManagerPtr → AppManager wrapper
//     → wrapper +0x4 → CClientExoApp* (real app instance)
//       → +0x4 → CClientExoAppInternal*
//         → +0x4 → CClientOptions* (kClientAppOptionsOffset)
//           → +0x8 (int bitfield) bit 1 (mask 0x2) = mouse_look
//
// Source: Lane's GoG SARIF (`docs/llm-docs/re/swkotor.exe.h:21796` for
// CClientOptions, `:21220` for CClientExoAppInternal). The bitfield layout
// is:
//
//   field0_0x0     : undefined4 — at +0x0
//   difficulty     : undefined4 — at +0x4
//   auto_level     : 1 bit — bit 0 of int @+0x8
//   mouse_look     : 1 bit — bit 1 of int @+0x8 (mask 0x2)
//   autosave       : 1 bit — bit 2
//   minigame_yaxis : 1 bit — bit 3
//   combat_movement: 1 bit — bit 4
//
// CClientOptions size in the SARIF is 0xa0; the long-term plan's hint that
// `mouseCameraRotateToggle` lives near +0xb0 was pointing at a *different*
// struct (CSWCameraOnAStick.mouseCameraRotateToggle @+0xb0 — runtime camera
// state, not the user setting). The user-facing swkotor.ini "Mouse Look=N"
// matches CClientOptions.mouse_look exclusively. If toggling the
// CClientOptions field doesn't change runtime behaviour, the next probe
// pivots to CSWCameraOnAStick.

#pragma once

namespace acc::engine {

// Read the current "Mouse Look" toggle (CClientOptions.mouse_look, the
// swkotor.ini-backed user setting). Returns false on chain failure (no
// app, no options, SEH-caught fault); writes to *out only on success.
bool GetMouseLook(bool& out);

// Write the "Mouse Look" toggle. Sets/clears bit 1 of CClientOptions's
// int bitfield @+0x8. Returns false on chain failure.
bool SetMouseLook(bool enabled);

// Read-modify-write convenience. Returns false on read OR write failure;
// on success writes the *new* value to *outNew.
bool ToggleMouseLook(bool& outNew);

}  // namespace acc::engine

// CClientExoAppInternal.client_options @+0x4 → CClientOptions*. Per Lane's
// type DB.
constexpr unsigned int kClientAppOptionsOffset = 0x4;

// CClientOptions: int bitfield @+0x8 holds mouse_look at bit 1. Read as
// 32-bit int, mask with kClientOptionsMouseLookMask. The five bits in the
// bitfield (auto_level / mouse_look / autosave / minigame_yaxis /
// combat_movement) all live in the same int — preserve the other bits on
// write.
constexpr unsigned int kClientOptionsBitFieldOffset = 0x8;
constexpr unsigned int kClientOptionsMouseLookMask  = 0x2;
