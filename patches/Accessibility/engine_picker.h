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

// Walk-to-act world verbs that GetDefaultActions emits for targets the PC must
// approach before acting: 0x3ea talk, 0x3f7 use/open, 0x3f5 bash, 0x3f2 door
// toggle, 0x3f4 disable mine. These share one composite shape — the engine
// enqueues a walk-to-the-object segment, then the act. Disabling player input
// (→ SwitchMode(player,0)) suppresses that server-side approach, so a target
// out of immediate range never gets walked to and the queued action drains
// silently (the distant_npc_dialogue_stuck mechanism — originally only fixed
// for talk). Drive() leaves input ENABLED for all of them and interact arms its
// approach watchdog instead, which announces "way blocked" if the walkmesh
// won't let the PC reach interaction range.
//
// 0x404 (noop) is deliberately excluded — it does nothing and never walks.
inline bool IsWalkToActVerb(uint32_t action_id) {
    switch (action_id) {
        case 0x3ea:  // talk (dialogue)
        case 0x3f2:  // door toggle
        case 0x3f4:  // disable mine
        case 0x3f5:  // bash
        case 0x3f7:  // use / open container (loot)
            return true;
        default:
            return false;
    }
}

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
// populateOnly: run only the read half — SetMainInterfaceTarget +
// GetDefaultActions + snapshot, plus the radial fallback (PopulateMenus) when
// there's no default action — and SKIP the default-action dispatch
// (last_*/+0x4a4 click gate + input-disable + HandleMouseClickInWorld). The
// caller inspects outSnapshot and dispatches itself: use-equivalent verbs
// (talk/open) go through the robust AddUseObjectAction primitive
// (guidance::UseObject), the rest re-call Drive with populateOnly=false to use
// the engine click pipeline. Lets us keep the picker as the verb/​radial source
// of truth while choosing the dispatch primitive per verb.
//
// Returns true iff: (radial opened) OR (populateOnly && descriptor populated)
// OR (!populateOnly && HandleMouseClickInWorld called). False on chain failure,
// invalid target, empty descriptor, SEH fault.
//
// Side effect: writes engine state (main-interface target, last_*,
// +0x4c8 array). Reversible by normal cursor hover.
bool Drive(uint32_t targetServerHandle, ActionSnapshot* outSnapshot,
           bool forceRadial = false, bool populateOnly = false);

// Re-assert the radial's target and rebuild the engine target-action
// menu for it, WITHOUT dispatching. Mirrors Drive's force-radial setup
// (SetMainInterfaceTarget + GetDefaultActions + PopulateMenus) but lean —
// no diagnostic dumps — so the radial input handler can call it on every
// keypress. The engine re-derives the target-action menu from the mouse
// cursor on every mouse-move, so for a keyboard-only / windowed user a
// drifting cursor silently re-points it off our target between Shift+Enter
// and Enter (see memory project_radial_cursor_coupling). Re-anchoring at
// the top of each radial keypress overrides that. targetServerHandle is
// the server-side id; the client high bit (0x80000000) is OR'd internally.
// Returns true iff the populate chain ran without faulting.
bool ReanchorRadial(uint32_t targetServerHandle);

// Read +0x4c8 without driving anything — observe the engine's own picker
// (cursor-hover or passive-selection-driven) for diagnostics or refined
// narration of what Enter would pick now.
bool ReadCurrent(ActionSnapshot* outSnapshot);

// Start a conversation with the target DIRECTLY via CSWCCreature::Action-
// InitiateDialog @0x0060f620 — the function HandleMouseClickInWorld's confirm
// branch calls. Calling it ourselves bypasses HandleMouseClickInWorld's
// first-click-vs-confirm gate, which otherwise needs two Enter presses to talk
// (first press opens the empty in-world menu, second confirms). Decompile-
// verified: the function ignores its two stack params and acts entirely on
// `this` = the TARGET NPC's client creature (ClearAllActions on it, orient it
// toward party char 0, SendPlayerToServerInput_Dialog(its id), SetGlobalDialog-
// State(1)); the engine then walks-then-talks server-side, so the caller leaves
// player input ENABLED. This is the dialogue analogue of routing loot through
// AddUseObjectAction (guidance::UseObject).
//
// targetServerHandle is the server id; the client high bit (0x80000000) is OR'd
// internally to resolve the client creature. Returns true iff the engine call
// ran without faulting (engine-side readiness gates may still no-op it; the
// interact watchdog covers the unreachable case).
bool InitiateDialog(uint32_t targetServerHandle);

}  // namespace acc::picker
