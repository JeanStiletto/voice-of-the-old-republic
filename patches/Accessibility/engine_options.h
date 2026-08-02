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

#include "engine_offsets_select.h"

namespace acc::engine {

// False on chain failure or SEH; out untouched.
bool GetMouseLook(bool& out);

// "Action Menu" auto-pause option (AutoPause options screen). Stored in
// CClientOptions::bit_flags_2 (+0x14), bit 0xf (mask 0x8000) — a DIFFERENT
// bitfield from the +0x8 mouse_look one above. Mirrors the engine's own gate
// in CSWGuiMainInterface::OnTargetUpArrowPressed / OnActionUpArrowPressed,
// which only call SetAutoPaused when this bit is set. Off by default (see
// CClientOptions::SetDefaultAutopauseOptions). False on chain failure or SEH;
// out untouched.
bool GetActionMenuAutoPause(bool& out);

// Resolved CClientOptions* for diagnostic probes. Production code uses
// Get/Set/ToggleMouseLook.
void* GetClientOptions();

bool SetMouseLook(bool enabled);

// Read-modify-write. False on either failure; on success outNew = new value.
bool ToggleMouseLook(bool& outNew);

}  // namespace acc::engine

// K2 witnessed 2026-08-02 off the exe bytes: CClientExoApp::GetOptions twin
// 0x0072FB00 does [this+4] -> internal, [internal+4] -> CClientOptions (thin
// caller 0x0079F6C0 feeds the INI writer 0x007B5F50 with that result), and
// the writer reads `[options+0x8] >> 1 & 1` right before pushing the
// "Mouse Look" key string — chain and bitfield identical to KOTOR 1.
const unsigned int kClientAppOptionsOffset      = acc::off::Same(0x4);
const unsigned int kClientOptionsBitFieldOffset = acc::off::Same(0x8);
constexpr unsigned int kClientOptionsMouseLookMask  = 0x2;

// AutoPause options bitfield (CClientOptions::bit_flags_2). K1: +0x14, action-
// menu pause bit 0xf; siblings at 0xb..0x10 (see action-menu-and-combat.md).
// K2 witnessed 2026-08-02: BOTH the field and the bit moved — the "Action Menu"
// INI load site (0x007B9FF0 region) writes `[options+0x1c]` masking 0xffffbfff
// then or-ing `(v & 1) << 0xe`, and the save site (0x007B78F1) reads
// `[options+0x1c] >> 0xe & 1`. So +0x1c with mask 0x4000 on KOTOR 2.
const unsigned int kClientOptionsAutoPauseFlagsOffset    = acc::off::Pick(0x14, 0x1c);
const unsigned int kClientOptionsActionMenuAutoPauseMask =
    static_cast<unsigned int>(acc::off::Pick(0x8000, 0x4000));
