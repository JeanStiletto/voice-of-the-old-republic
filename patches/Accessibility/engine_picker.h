// Engine action picker — drive the game's own context-sensitive action
// dispatcher for an arbitrary target.
//
// Layer: engine/ (engine-only — sets up engine-side state, calls engine
// entry points, returns observation; no menu state, no Tolk speech).
//
// Premise (see docs/engine-action-picker.md):
//   When the cursor hovers an in-world object, the engine's
//   `CClientExoAppInternal::DoPassiveSelection` + `GetDefaultActions`
//   pipeline computes the appropriate default action for the
//   leader/target combo (open / talk / Security / Bash / Disable Trap /
//   …). A left-click is then dispatched by
//   `CClientExoAppInternal::HandleMouseClickInWorld` which reads a
//   descriptor pointer at `this+0x4c8` and calls
//   `(descriptor[+0xc])(descriptor[+0x8], playerCharId)`.
//
//   Our cycle (Q/E/Tab + ,/.) selects targets *without* moving the
//   cursor over them, so `+0x4c8` stays tied to whatever the cursor
//   last hovered. We replicate the cursor's setup work explicitly:
//     1. Set `CGuiInGame.main_interface.field1_0x64 = targetClient`
//     2. Call `GetDefaultActions(this)` — populates +0x4c8 / +0x4cc
//     3. Set the engine click gate
//        (`last_target == last_clicked_on_target == hover_target == X`)
//     4. Call `HandleMouseClickInWorld(this)` — engine runs the picker
//
// Result: the engine picks the same default action it would have picked
// for a sighted player who clicked the target — no per-kind logic in
// our patch.
//
// Verified surfaces (from `docs/llm-docs/re/k1_win_gog_swkotor.exe.gzf`
// decompile, 2026-05-05):
//   CClientExoAppInternal::HandleMouseClickInWorld @0x00620350
//   CClientExoAppInternal::GetDefaultActions       @0x00620620
//   CGuiInGame::SetMainInterfaceTarget             @0x0062b000
//   field292_0x4c8 / field293_0x4cc on CClientExoAppInternal hold the
//   descriptor pointer + count.
//   CSWGuiInterfaceAction layout (decompiled from GetDefaultActions):
//     +0x00 CExoString label    (engine-localised, e.g. "Sicherheit")
//     +0x08 ulong     action_id (0x404 noop, 0x3ea talk, 0x3f7 use,
//                                0x3f5 bash, 0x3f2 door toggle, 0x3f4
//                                disable mine, …)
//     +0x0c void*     action_function
//     +0x1c ulong     target_id (client-side handle, with 0x80000000)
//     +0x20 CResRef   icon      ("i_dialog", "i_opendoor", …)

#pragma once

#include <cstdint>

namespace acc::picker {

// Snapshot of the engine's first descriptor at +0x4c8 — captured AFTER
// we call GetDefaultActions on a target. Use to refine narration.
struct ActionSnapshot {
    uint32_t action_id;     // descriptor +0x08
    uint32_t target_id;     // descriptor +0x1c (client-side handle)
    int      count;         // CClientExoAppInternal.field293_0x4cc
                            // (number of descriptor entries; gate value)
    char     label[64];     // CExoString text from descriptor +0x00
    char     icon[16];      // CResRef text from descriptor +0x20 (16 bytes)
    bool     valid;         // true when descriptor pointer was non-null
                            // AND count > 0
};

// Drive the engine's default-action picker for `targetServerHandle`.
//
// targetServerHandle: server-side game-object id (the form
//   `acc::engine::GetObjectHandle` returns). The picker converts to the
//   client-side form (high bit 0x80000000 set) internally — server ids
//   are NOT what the cursor system uses, so we OR the bit before
//   writing the engine's hover/click slots.
//
// outSnapshot: optional. Populated whenever a descriptor was produced
//   (even if we ultimately don't dispatch). Caller can use the label
//   field for screen-reader pre-roll instead of our hard-coded
//   per-category strings, which is the "free win" item 5 from
//   docs/engine-action-picker.md.
//
// Returns true when the engine descriptor was populated AND we called
// HandleMouseClickInWorld. False on:
//   - chain failure (no app loaded yet),
//   - target == 0 / kInvalidObjectId,
//   - GetDefaultActions returned an empty descriptor (engine has no
//     valid action for this leader/target — happens when the engine's
//     own picker would also do nothing),
//   - any SEH fault during the call sequence.
//
// Side effect: writes engine state (main-interface target,
// last_target, last_clicked_on_target, hover_target, +0x4c8 array).
// All writes are reversible by normal cursor hover (the next
// DoPassiveSelection tick re-derives them).
bool Drive(uint32_t targetServerHandle, ActionSnapshot* outSnapshot);

// Read the current descriptor at +0x4c8 without driving anything. Used
// to observe the engine's own picker selection (cursor-hover or
// passive-selection-driven) for diagnostics or refined narration of
// what the engine *would* pick if Enter were pressed right now.
//
// Returns true if outSnapshot.valid was set true.
bool ReadCurrent(ActionSnapshot* outSnapshot);

}  // namespace acc::picker
