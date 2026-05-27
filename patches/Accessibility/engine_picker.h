// Engine action picker — drive the game's context-sensitive action
// dispatcher for an arbitrary target.
//
// Engine-only: sets up engine state, calls engine entry points, returns
// observation. No menu state, no speech.
//
// Mechanism: when the cursor hovers an in-world object, the engine's
// DoPassiveSelection + GetDefaultActions pipeline computes the
// appropriate default action (open / talk / Security / Bash / Disable
// Trap / ...). A left-click dispatches via HandleMouseClickInWorld
// reading a descriptor at this+0x4c8.
//
// Our cycle picks targets without moving the cursor, so +0x4c8 stays
// tied to the last hover. We replicate the cursor setup explicitly:
//   1. CGuiInGame.main_interface.field1_0x64 = targetClient
//   2. GetDefaultActions(this) — populates +0x4c8 / +0x4cc
//   3. Engine click gate (last_target = last_clicked_on_target =
//      hover_target = X)
//   4. HandleMouseClickInWorld(this) — engine runs the picker
//
// Engine surfaces:
//   HandleMouseClickInWorld @0x00620350
//   GetDefaultActions       @0x00620620
//   SetMainInterfaceTarget  @0x0062b000
// CSWGuiInterfaceAction layout:
//   +0x00 CExoString label    ("Sicherheit", "Öffnen", ...)
//   +0x08 ulong     action_id (0x404 noop, 0x3ea talk, 0x3f7 use,
//                              0x3f5 bash, 0x3f2 door toggle,
//                              0x3f4 disable mine, ...)
//   +0x0c void*     action_function
//   +0x1c ulong     target_id (client-side, with 0x80000000)
//   +0x20 CResRef   icon

#pragma once

#include <cstdint>

namespace acc::picker {

// Snapshot of the first descriptor at +0x4c8 after GetDefaultActions.
struct ActionSnapshot {
    uint32_t action_id;
    uint32_t target_id;
    int      count;            // descriptor entry count; the gate value
    char     label[64];
    char     icon[16];
    bool     valid;            // descriptor non-null AND count > 0
    bool     radial_opened;    // Drive opened the radial via
                               // PopulateMenus (count==0 or forceRadial)
                               // instead of dispatching. Caller should
                               // announce "Aktionsmenü".
};

// targetServerHandle = server-side id (engine::GetObjectHandle). Picker
// ORs the client-side high bit (0x80000000) before writing engine slots.
//
// forceRadial bypasses default-action dispatch and always opens the
// radial (vanilla right-click semantics). Bound to Shift+Enter. Without
// it the radial only opens on count==0, which misses the most useful
// case: a locked door with Bash + Security in the radial but a single
// "Öffnen" default.
//
// Returns true iff descriptor populated AND HandleMouseClickInWorld
// called. False on chain failure, invalid target, empty descriptor,
// SEH fault.
//
// Side effect: writes engine state (main-interface target, last_*,
// +0x4c8 array). Reversible by normal cursor hover.
bool Drive(uint32_t targetServerHandle, ActionSnapshot* outSnapshot,
           bool forceRadial = false);

// Read +0x4c8 without driving anything — observe the engine's own picker
// (cursor-hover or passive-selection-driven) for diagnostics or refined
// narration of what Enter would pick now.
bool ReadCurrent(ActionSnapshot* outSnapshot);

}  // namespace acc::picker
