// Specials-queue heartbeat — peripheral "you can act now" cue.
//
// Layer: combat/ — purely poll-based (no engine hooks). Runs from
// core_tick.cpp after combat-mode poll so the in-combat gate is
// fresh.
//
// Design (signed off 2026-05-14, see chat transcript "softer system"
// thread):
//
//   - Walks every party member's combat_round.actions each tick and
//     counts "specials" party-wide. "Special" = anything that isn't a
//     routine auto-attack against a hostile creature:
//       * action_type != 1                          → special
//       * action_type == 1 && attack_feat != 0      → special (feat-
//                                                     driven attack
//                                                     like Power Attack)
//       * action_type == 1 && target kind != Creature
//                                                   → special (Bash on
//                                                     door, attack on
//                                                     placeable)
//       * everything else                           → routine
//     The 0xFF placeholder slot the engine keeps at the head of every
//     queue is filtered out — same convention as combat_queue.cpp.
//
//   - Edge trigger: when the count transitions ≥1 → 0 while in combat,
//     fire a short UI cue (gui_actqueue) **immediately** — zero delay,
//     so the player hears it on the same engine frame their last
//     special executed.
//
//   - Repeat heartbeat: if specials stay at 0 in continued combat,
//     re-fire the cue ~every 6s (one combat round). The first repeat
//     fires 6s after the edge cue, so the natural cadence becomes "ping
//     when you exhaust + ping every round you stay idle".
//
//   - First-round gate: no ticks fire during the first ~6s of combat
//     entry, so "Kampf beginnt" gets clean air.
//
//   - Auto-resets across combat enter/exit edges.

#pragma once

namespace acc::combat::special_watch {

// Per-tick driver. Cheap when out of combat (one chain walk). Should
// be called from core_tick.cpp's Dispatch() after combat::TickCombatMode.
void Tick();

}  // namespace acc::combat::special_watch
