// Controller device census — what the machine ACTUALLY has, as opposed to what
// pad_input managed to bind to.
//
// Why this is a separate module from pad_input
// --------------------------------------------
// pad_input asks one question and answers it as cheaply as it can: "which
// XInput slot do I read this frame?" It takes the first slot that answers and
// stays there. On an ordinary desktop with one controller that is complete
// information. On a handheld it is not:
//
//   * The vendor layer (Armoury Crate on the ROG Ally, and its equivalents)
//     can present a controller of its own ALONGSIDE the physical one, so more
//     than one XInput slot answers and only one of them is the pad in the
//     player's hands. Binding to the wrong one looks exactly like "the
//     controller does nothing" — and produces no log line at all, because from
//     pad_input's point of view everything worked.
//   * The same vendor layer can put the pad into a desktop / keyboard-and-mouse
//     emulation mode, where the sticks and buttons emit keystrokes and mouse
//     motion and NO controller exists at either API. That also looks like "the
//     controller does nothing", and it is a device setting, not a bug — but
//     only a log that says "neither API sees a controller" can tell the two
//     apart.
//
// So this module reports the whole picture, both APIs, once per session and
// again whenever it changes:
//
//   XInput      — all four slots, whether pad_input chose them or not, with
//                 each slot's capabilities and (where the runtime offers it)
//                 the USB vendor/product ids behind it. This is what pad_input
//                 itself can use, and on KOTOR 1 it is the ONLY path there is.
//   DirectInput — the same enumeration KOTOR 2's engine performs at input init
//                 (DI8DEVCLASS_GAMECTRL, attached only), which is where its own
//                 gamepad support gets its device list. Run through whatever
//                 dinput8 the process already has, so it sees exactly what the
//                 engine saw. Its payload is device NAMES, which is the one
//                 thing XInput cannot give us and the fastest way to tell a
//                 physical pad from a virtual one in a tester's log.
//
// Nothing here drives input. It reads, it prints, it never acquires a device
// and never holds one open.

#pragma once

#include <windows.h>

namespace acc::pad::diag {

// XInput's own XINPUT_DEVSUBTYPE_GAMEPAD. Mirrored rather than included for
// the same reason pad_input mirrors the XInput structures.
constexpr int kSubTypeGamepad = 1;

// Capability sub-type reported for `slot`, or -1 when the slot is empty, the
// runtime has no XInputGetCapabilities, or the query fails. `xinput` is the
// module pad_input resolved.
int SlotSubType(HMODULE xinput, unsigned slot);

// The USB vendor/product ids behind `slot`. False when the slot is empty, the
// runtime has no XInputGetCapabilitiesEx (anything older than xinput1_4), or
// the query fails — `*vid` / `*pid` are left untouched then, and the caller
// has to treat "cannot tell" as its own answer rather than as a match.
//
// Exposed because the ids settle a question the census is not the only reader
// of: whether two occupied slots are two devices or one device presented
// twice. pad_input's idle-slot guard turns on exactly that.
bool SlotVendorIds(HMODULE xinput, unsigned slot, unsigned* vid, unsigned* pid);

// Vendor id Valve Corporation. Steam Input's emulated pad — the "Steam Virtual
// Gamepad", 0x28DE:0x11FF — reports it, as does everything else Steam
// synthesises, so a slot carrying this vid is Steam's layer standing in front
// of the controller rather than the controller itself.
constexpr unsigned kVendorValve = 0x28DE;

// Emit the full census. `why` names the occasion and is printed verbatim.
void LogDeviceCensus(HMODULE xinput, const wchar_t* xinputName, const char* why);

// The same, but only when the XInput picture differs from the last census —
// slot occupancy, sub-types or vendor/product ids. Cheap enough to call on
// every throttled rescan: the fingerprint is XInput-only, and the expensive
// half (creating a DirectInput instance and enumerating it) runs only when a
// census is actually emitted.
void LogDeviceCensusIfChanged(HMODULE xinput, const wchar_t* xinputName,
                              const char* why);

}  // namespace acc::pad::diag
