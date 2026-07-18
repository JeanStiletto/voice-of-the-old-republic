// Combat event narration.
//
// Channels:
//   - Combat mode entry/exit — debounced via the turn_announce stability
//     pattern; speaks "Kampf beginnt" / "Kampf beendet".
//   - Combat log — live narration via the OnAppendToMsgBuffer hook
//     (see the msg_router subscriber at the bottom of combat.cpp).
//     TickCombatLog polls CSWGuiInGameMessages.messages_listbox for
//     diagnostic logging only; speech is OFF there because the engine
//     fills that listbox lazily when the review screen opens.
//
// Each Tick is cheap and idle when nothing is happening.

#pragma once

namespace acc::combat {

// Cross-feature gating read — false on chain fault (treat as not-in-combat).
// IsCombatActive mirrors only the *controlled leader's* combat bit, so a Tab to
// a not-yet-engaged member reads peace mid-encounter. IsPartyInCombat is the
// encounter-level truth (OR of every party member's per-creature combat bit),
// immune to which member is controlled — the same signal TickCombatMode's
// begin/end cue and menu auto-close already trust. Prefer it for anything that
// must not flip just because the player switched leaders during a fight.
bool IsCombatActive();
bool IsPartyInCombat();

void TickCombatMode();
void TickCombatLog();

// Flushes a debounced burst of damage-absorption lines into one spoken
// total. Cheap and idle when no absorb burst is pending.
void TickCombatAbsorb();

// Flushes debounced blaster-deflection bursts into one spoken line per
// deflector ("<actor> reflektiert N Schüsse"). Cheap and idle when none
// is pending.
void TickCombatDeflect();

// Flushes debounced per-target ability/grenade/force-power effects into one
// merged spoken line each. Cheap and idle when no effect is pending.
void TickCombatEffects();

}  // namespace acc::combat
