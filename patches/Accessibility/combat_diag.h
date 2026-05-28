// Combat-queue diagnostic probe.
//
// Per-press snapshot + per-tick delta of the four state bits that decide
// chain vs overwrite in DoPersonalAction / DoTargetAction:
//
//   cm  = CSWCCreature.field200_0x440 & 1     (combat-mode bit)
//   qs  = CSWSCombatRound.actions list size   (queue depth, ignoring 0xFF placeholder)
//   ap  = CClientExoApp::GetAutoPaused()
//   ps  = CClientExoApp::GetPauseState(0)
//   tgt = main_interface.field1_0x64           (current target handle)
//
// Read-only — no engine state is mutated. Logs to acclog under
// "Combat.Diag". Off when not in a world (no player position).
//
// Wiring points:
//   * Tick()              — per-frame from core_tick. Emits a DELTA line
//                           whenever any tracked bit changes.
//   * LogPreFire(label)   — called from each combat-key press path BEFORE
//                           the engine dispatches.
//   * LogPostFire(label)  — called AFTER the engine returns (Shift+1..3
//                           and Shift+4..7 only; bare 1..7 fires after our
//                           prologue hook returns, so we get the "post"
//                           state from the next Tick's DELTA line).

#pragma once

namespace acc::combat_diag {

void Tick();

// `label` is a short tag: "bare-1", "bare-5", "shift+1-enter", "shift+5-enter".
// PRE is called before the dispatch; POST after.
void LogPreFire(const char* label);
void LogPostFire(const char* label);

}  // namespace acc::combat_diag

// Hook entry points — detoured into combat-round mutators so we get
// deterministic ADD / CLEAR events instead of racing with a list walk.
//   * AddAction @0x4d3660       — generic adder, called by every Add*Action
//                                  in CSWSCombatRound. Fires once per queued
//                                  combat-round action regardless of type.
//   * RemoveAllActions @0x4d3770 — queue clear (the engine's "overwrite"
//                                  primitive — called from DoPersonalAction
//                                  / DoTargetAction overwrite branches).
//
// Each handler logs one Combat.Diag line tagged ADD or CLEAR, with the
// combat-round `this` pointer + a PLAYER/other marker so we can filter to
// just the player's combat round when reading the log.
extern "C" {
    void __cdecl OnCombatRoundAddAction(void* this_combatRound,
                                        void* esp_action_addr,
                                        void* esp_param2_addr);
    void __cdecl OnCombatRoundRemoveAllActions(void* this_combatRound);

    // Consumer-side hooks — fired by the engine when it dispatches /
    // drains an action from the queue.
    //   SetCurrentAction(byte) — moves an action into the "currently
    //                            dispatching" slot at +0x9d0. The byte
    //                            is the index/type the engine just
    //                            stored. Fires once per dispatched
    //                            action.
    //   RemoveLastAction()     — pops the tail of CSWSCombatRound.actions.
    //                            If we see this fire after a press, the
    //                            engine is silently culling queued
    //                            entries.
    void __cdecl OnCombatRoundSetCurrentAction(void* this_combatRound,
                                               void* esp_byte_addr);
    void __cdecl OnCombatRoundRemoveLastAction(void* this_combatRound);
}
