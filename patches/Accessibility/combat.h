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
bool IsCombatActive();

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
