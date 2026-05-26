// KOTOR Accessibility — Tab leader-change announce.
//
// Pillar 2 sub-feature. When the user presses Tab the engine cycles
// control to the next living party member; we read the resulting leader
// creature and speak its name. Repetition is intentional: in solo-mode
// (one party member, or a story beat that strips companions), Tab on the
// same creature still speaks the name — confirming "still solo" rather
// than swallowing the keypress silently.
//
// No UI-block gate: Tab cycles the leader in-world AND in panels (the
// engine's strip panels re-bind to the new leader, so the user wants the
// name announce in both contexts).
//
// Win32-polled (GetAsyncKeyState VK_TAB) — Tab is logical(0xce), reaches
// the manager hook directly, but the announce belongs at the per-tick
// dispatcher level (state observation, per the hook-vs-poll principle).

#pragma once

namespace acc::party_leader_announce {

// Per-frame tick from core_tick::Dispatch. Reads Tab rising-edge, applies
// the UI-block gate, then speaks the controlled creature's name. No-op
// when Tab is not pressed, KOTOR window isn't foreground, or the player
// is not yet loaded.
void Tick();

}  // namespace acc::party_leader_announce
