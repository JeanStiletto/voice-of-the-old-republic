// CClientOptions helpers — currently the "Mouse Look" toggle.
//
// Chain: *kAddrAppManagerPtr → +0x4 CClientExoApp* → +0x4 Internal* →
// +0x4 CClientOptions* → +0x8 int bitfield, bit 1 (mask 0x2) = mouse_look.
//
// Bitfield layout at +0x8 (5 bits in one int — preserve siblings on write):
//   bit 0 auto_level
//   bit 1 mouse_look
//   bit 2 autosave
//   bit 3 minigame_yaxis
//   bit 4 combat_movement
//
// User-facing swkotor.ini "Mouse Look=N" matches CClientOptions.mouse_look
// exclusively. CSWCameraOnAStick.mouseCameraRotateToggle @+0xb0 is a
// different struct (runtime camera state) — distinct from the user setting.

#pragma once

namespace acc::engine {

// False on chain failure or SEH; out untouched.
bool GetMouseLook(bool& out);

// Resolved CClientOptions* for diagnostic probes. Production code uses
// Get/Set/ToggleMouseLook.
void* GetClientOptions();

bool SetMouseLook(bool enabled);

// Read-modify-write. False on either failure; on success outNew = new value.
bool ToggleMouseLook(bool& outNew);

}  // namespace acc::engine

constexpr unsigned int kClientAppOptionsOffset      = 0x4;
constexpr unsigned int kClientOptionsBitFieldOffset = 0x8;
constexpr unsigned int kClientOptionsMouseLookMask  = 0x2;
